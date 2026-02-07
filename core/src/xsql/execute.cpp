#include "xsql/xsql.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>

#include "../executor/executor_internal.h"
#include "../executor.h"
#include "../html_parser.h"
#include "../query_parser.h"
#include "../util/string_util.h"
#include "xsql_internal.h"

namespace xsql {

namespace {

struct FragmentSource {
  std::vector<std::string> fragments;
};

struct DescendantTagFilter {
  struct Predicate {
    Operand::FieldKind field_kind = Operand::FieldKind::Tag;
    std::string attribute;
    CompareExpr::Op op = CompareExpr::Op::Eq;
    std::vector<std::string> values;
  };
  std::vector<Predicate> predicates;
};

std::string normalize_flatten_text(const std::string& value) {
  std::string trimmed = util::trim_ws(value);
  std::string out;
  out.reserve(trimmed.size());
  bool in_space = false;
  for (char c : trimmed) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_space) {
        out.push_back(' ');
        in_space = true;
      }
      continue;
    }
    in_space = false;
    out.push_back(c);
  }
  return out;
}

void collect_descendants_at_depth(const std::vector<std::vector<int64_t>>& children,
                                  int64_t node_id,
                                  size_t depth,
                                  std::vector<int64_t>& out) {
  if (depth == 0) {
    out.push_back(node_id);
    return;
  }
  for (int64_t child : children.at(static_cast<size_t>(node_id))) {
    collect_descendants_at_depth(children, child, depth - 1, out);
  }
}

void collect_descendants_any_depth(const std::vector<std::vector<int64_t>>& children,
                                   int64_t node_id,
                                   std::vector<int64_t>& out) {
  for (int64_t child : children.at(static_cast<size_t>(node_id))) {
    out.push_back(child);
    collect_descendants_any_depth(children, child, out);
  }
}

bool collect_descendant_tag_filter(const Expr& expr, DescendantTagFilter& filter) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    if (cmp.lhs.axis == Operand::Axis::Descendant &&
        (cmp.lhs.field_kind == Operand::FieldKind::Tag ||
         cmp.lhs.field_kind == Operand::FieldKind::Attribute)) {
      DescendantTagFilter::Predicate pred;
      pred.field_kind = cmp.lhs.field_kind;
      pred.attribute = cmp.lhs.attribute;
      pred.op = cmp.op;
      pred.values.reserve(cmp.rhs.values.size());
      if (cmp.lhs.field_kind == Operand::FieldKind::Tag) {
        for (const auto& value : cmp.rhs.values) {
          pred.values.push_back(util::to_lower(value));
        }
      } else {
        pred.values = cmp.rhs.values;
      }
      filter.predicates.push_back(std::move(pred));
      return true;
    }
    return false;
  }
  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    return false;
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  bool left = collect_descendant_tag_filter(bin.left, filter);
  bool right = collect_descendant_tag_filter(bin.right, filter);
  return left || right;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  std::string lower_haystack = util::to_lower(haystack);
  std::string lower_needle = util::to_lower(needle);
  return lower_haystack.find(lower_needle) != std::string::npos;
}

bool contains_all_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (!contains_ci(haystack, token)) return false;
  }
  return true;
}

bool contains_any_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (contains_ci(haystack, token)) return true;
  }
  return false;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    if (start < i) out.push_back(s.substr(start, i - start));
  }
  return out;
}

bool match_descendant_predicate(const HtmlNode& node, const DescendantTagFilter::Predicate& pred) {
  if (pred.field_kind == Operand::FieldKind::Tag) {
    if (pred.op == CompareExpr::Op::In) {
      return executor_internal::string_in_list(node.tag, pred.values);
    }
    if (pred.op == CompareExpr::Op::Eq) {
      return node.tag == pred.values.front();
    }
    return false;
  }
  const auto it = node.attributes.find(pred.attribute);
  if (it == node.attributes.end()) return false;
  const std::string& attr_value = it->second;
  if (pred.op == CompareExpr::Op::Contains) {
    return contains_ci(attr_value, pred.values.front());
  }
  if (pred.op == CompareExpr::Op::ContainsAll) {
    return contains_all_ci(attr_value, pred.values);
  }
  if (pred.op == CompareExpr::Op::ContainsAny) {
    return contains_any_ci(attr_value, pred.values);
  }
  if (pred.op == CompareExpr::Op::In || pred.op == CompareExpr::Op::Eq) {
    if (pred.attribute == "class") {
      auto tokens = split_ws(attr_value);
      if (pred.op == CompareExpr::Op::Eq) {
        return executor_internal::string_in_list(pred.values.front(), tokens);
      }
      for (const auto& token : tokens) {
        if (executor_internal::string_in_list(token, pred.values)) return true;
      }
      return false;
    }
    if (pred.op == CompareExpr::Op::Eq) {
      return attr_value == pred.values.front();
    }
    return executor_internal::string_in_list(attr_value, pred.values);
  }
  return false;
}

bool looks_like_html_fragment(const std::string& value) {
  return value.find('<') != std::string::npos && value.find('>') != std::string::npos;
}

std::optional<std::string> field_value_string(const QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return std::to_string(row.node_id);
  if (field == "count") return std::to_string(row.node_id);
  if (field == "tag") return row.tag;
  if (field == "text") return row.text;
  if (field == "inner_html") return row.inner_html;
  if (field == "parent_id") {
    if (!row.parent_id.has_value()) return std::nullopt;
    return std::to_string(*row.parent_id);
  }
  if (field == "sibling_pos") return std::to_string(row.sibling_pos);
  if (field == "max_depth") return std::to_string(row.max_depth);
  if (field == "doc_order") return std::to_string(row.doc_order);
  if (field == "source_uri") return row.source_uri;
  if (field == "attributes") return std::nullopt;
  auto computed = row.computed_fields.find(field);
  if (computed != row.computed_fields.end()) return computed->second;
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return std::nullopt;
  return it->second;
}

QueryResult build_meta_result(const std::vector<std::string>& columns,
                              const std::vector<std::vector<std::string>>& rows) {
  QueryResult out;
  out.columns = columns;
  for (const auto& values : rows) {
    QueryResultRow row;
    for (size_t i = 0; i < columns.size() && i < values.size(); ++i) {
      const auto& col = columns[i];
      const auto& value = values[i];
      if (col == "source_uri") {
        row.source_uri = value;
      } else {
        row.attributes[col] = value;
      }
    }
    out.rows.push_back(std::move(row));
  }
  return out;
}

QueryResult execute_meta_query(const Query& query, const std::string& source_uri) {
  switch (query.kind) {
    case Query::Kind::ShowInput: {
      return build_meta_result({"key", "value"},
                               {{"source_uri", source_uri}});
    }
    case Query::Kind::ShowInputs: {
      return build_meta_result({"source_uri"},
                               {{source_uri}});
    }
    case Query::Kind::ShowFunctions: {
      return build_meta_result(
          {"function", "returns", "description"},
          {
              {"text(tag)", "string", "Text content of a tag"},
              {"inner_html(tag[, depth])", "string", "Minified HTML inside a tag"},
              {"raw_inner_html(tag[, depth])", "string", "Raw inner HTML without minification"},
              {"flatten_text(tag[, depth])", "string[]", "Flatten descendant text at depth into columns"},
              {"flatten(tag[, depth])", "string[]", "Alias of flatten_text"},
              {"trim(inner_html(...))", "string", "Trim whitespace in inner_html"},
              {"count(tag|*)", "int64", "Aggregate node count"},
              {"summarize(*)", "table<tag,count>", "Tag counts summary"},
              {"tfidf(tag|*)", "map<string,double>", "TF-IDF term scores"},
          });
    }
    case Query::Kind::ShowAxes: {
      return build_meta_result(
          {"axis", "description"},
          {
              {"parent", "Parent node"},
              {"child", "Direct child nodes"},
              {"ancestor", "Any ancestor node"},
              {"descendant", "Any descendant node"},
          });
    }
    case Query::Kind::ShowOperators: {
      return build_meta_result(
          {"operator", "description"},
          {
              {"=", "Equality"},
              {"<>", "Not equal"},
              {"IN (...)", "Membership"},
              {"CONTAINS", "Substring or list contains"},
              {"CONTAINS ALL", "Contains all values"},
              {"CONTAINS ANY", "Contains any value"},
              {"IS NULL", "Null check"},
              {"IS NOT NULL", "Not-null check"},
              {"HAS_DIRECT_TEXT", "Direct text predicate"},
              {"~", "Regex match"},
              {"AND", "Logical AND"},
              {"OR", "Logical OR"},
          });
    }
    case Query::Kind::DescribeDoc: {
      return build_meta_result(
          {"column_name", "type", "nullable", "notes"},
          {
              {"node_id", "int64", "false", "Stable node identifier"},
              {"tag", "string", "false", "Lowercase tag name"},
              {"attributes", "map<string,string>", "false", "HTML attributes"},
              {"parent_id", "int64", "true", "Null for root"},
              {"max_depth", "int64", "false", "Max element depth under node"},
              {"doc_order", "int64", "false", "Preorder document index"},
              {"sibling_pos", "int64", "false", "1-based among siblings"},
              {"source_uri", "string", "true", "Empty for RAW/STDIN"},
          });
    }
    case Query::Kind::DescribeLanguage: {
      return build_meta_result(
          {"category", "name", "syntax", "notes"},
          {
              {"clause", "SELECT", "SELECT <tag|*>[, ...]", "Tag list or *"},
              {"clause", "FROM", "FROM <source>", "Defaults to document in REPL"},
              {"clause", "WHERE", "WHERE <expr>", "Predicate expression"},
              {"clause", "ORDER BY", "ORDER BY <field> [ASC|DESC]",
               "node_id, tag, text, parent_id, sibling_pos, max_depth, doc_order; SUMMARIZE uses tag/count"},
              {"clause", "LIMIT", "LIMIT <n>", "n >= 0, max enforced"},
              {"clause", "EXCLUDE", "EXCLUDE <field>[, ...]", "Only with SELECT *"},
              {"output", "TO LIST", "TO LIST()", "Requires one projected column"},
              {"output", "TO TABLE", "TO TABLE([HEADER|NOHEADER][, EXPORT='file.csv'])",
               "Select table tags only"},
              {"output", "TO CSV", "TO CSV('file.csv')", "Export result"},
              {"output", "TO PARQUET", "TO PARQUET('file.parquet')", "Export result"},
              {"source", "document", "FROM document", "Active input in REPL"},
              {"source", "alias", "FROM doc", "Alias for document"},
              {"source", "path", "FROM 'file.html'", "Local file"},
              {"source", "url", "FROM 'https://example.com'", "Requires libcurl"},
              {"source", "raw", "FROM RAW('<html>')", "Inline HTML"},
              {"source", "fragments", "FROM FRAGMENTS(<raw|subquery>)",
               "Concatenate HTML fragments"},
              {"source", "fragments_raw", "FRAGMENTS(RAW('<ul>...</ul>'))", "Raw fragment input"},
              {"source", "fragments_query",
               "FRAGMENTS(SELECT inner_html(...) FROM doc)", "Subquery returns HTML strings"},
              {"field", "node_id", "node_id", "int64"},
              {"field", "tag", "tag", "lowercase"},
              {"field", "attributes", "attributes", "map<string,string>"},
              {"field", "parent_id", "parent_id", "int64 or null"},
              {"field", "sibling_pos", "sibling_pos", "1-based among siblings"},
              {"field", "source_uri", "source_uri", "Hidden unless multi-source"},
              {"function", "text", "text(tag)", "Direct text content; requires WHERE"},
              {"function", "inner_html", "inner_html(tag[, depth])",
               "Minified inner HTML; requires WHERE"},
              {"function", "raw_inner_html", "raw_inner_html(tag[, depth])",
               "Raw inner HTML (no minify); requires WHERE"},
              {"function", "trim", "trim(text(...)) | trim(inner_html(...))",
               "Trim whitespace"},
              {"aggregate", "count", "count(tag|*)", "int64"},
              {"aggregate", "summarize", "summarize(*)", "tag counts table"},
              {"aggregate", "tfidf", "tfidf(tag|*)", "map<string,double>"},
              {"axis", "parent", "parent.<field>", "Direct parent"},
              {"axis", "child", "child.<field>", "Direct child"},
              {"axis", "ancestor", "ancestor.<field>", "Any ancestor"},
              {"axis", "descendant", "descendant.<field>", "Any descendant"},
              {"predicate", "exists", "EXISTS(axis [WHERE expr])", "Existential axis predicate"},
              {"operator", "=", "lhs = rhs", "Equality"},
              {"operator", "<>", "lhs <> rhs", "Not equal"},
              {"operator", "IN", "lhs IN ('a','b')", "Membership"},
              {"operator", "CONTAINS", "lhs CONTAINS 'x'", "Substring or list contains"},
              {"operator", "CONTAINS ALL", "lhs CONTAINS ALL ('a','b')", "All values"},
              {"operator", "CONTAINS ANY", "lhs CONTAINS ANY ('a','b')", "Any value"},
              {"operator", "IS NULL", "lhs IS NULL", "Null check"},
              {"operator", "IS NOT NULL", "lhs IS NOT NULL", "Not-null check"},
              {"operator", "HAS_DIRECT_TEXT", "HAS_DIRECT_TEXT", "Predicate on direct text"},
              {"operator", "~", "lhs ~ 're'", "Regex match"},
              {"operator", "AND", "expr AND expr", "Logical AND"},
              {"operator", "OR", "expr OR expr", "Logical OR"},
              {"meta", "SHOW INPUT", "SHOW INPUT", "Active source"},
              {"meta", "SHOW INPUTS", "SHOW INPUTS", "Last result sources or active"},
              {"meta", "SHOW FUNCTIONS", "SHOW FUNCTIONS", "Function list"},
              {"meta", "SHOW AXES", "SHOW AXES", "Axis list"},
              {"meta", "SHOW OPERATORS", "SHOW OPERATORS", "Operator list"},
              {"meta", "DESCRIBE doc", "DESCRIBE doc", "Document schema"},
              {"meta", "DESCRIBE language", "DESCRIBE language", "Language spec"},
          });
    }
    case Query::Kind::Select:
    default:
      return QueryResult{};
  }
}

void append_document(HtmlDocument& target, const HtmlDocument& source) {
  const int64_t offset = static_cast<int64_t>(target.nodes.size());
  target.nodes.reserve(target.nodes.size() + source.nodes.size());
  for (const auto& node : source.nodes) {
    HtmlNode copy = node;
    copy.id = node.id + offset;
    copy.doc_order = node.doc_order + offset;
    if (node.parent_id.has_value()) {
      copy.parent_id = *node.parent_id + offset;
    }
    target.nodes.push_back(std::move(copy));
  }
}

HtmlDocument build_fragments_document(const FragmentSource& fragments) {
  HtmlDocument merged;
  for (const auto& fragment : fragments.fragments) {
    HtmlDocument doc = parse_html(fragment);
    append_document(merged, doc);
  }
  return merged;
}

FragmentSource collect_fragments(const QueryResult& result) {
  if (result.to_table || !result.tables.empty()) {
    throw std::runtime_error("FRAGMENTS does not accept TO TABLE() results");
  }
  if (result.columns.size() != 1) {
    throw std::runtime_error("FRAGMENTS expects a single HTML string column");
  }
  const std::string& field = result.columns[0];
  FragmentSource out;
  size_t total_bytes = 0;
  for (const auto& row : result.rows) {
    std::optional<std::string> value = field_value_string(row, field);
    if (!value.has_value()) {
      throw std::runtime_error("FRAGMENTS expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    std::string trimmed = util::trim_ws(*value);
    if (trimmed.empty()) {
      continue;
    }
    if (!looks_like_html_fragment(trimmed)) {
      throw std::runtime_error("FRAGMENTS expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    if (trimmed.size() > xsql_internal::kMaxFragmentHtmlBytes) {
      throw std::runtime_error("FRAGMENTS HTML fragment exceeds maximum size");
    }
    total_bytes += trimmed.size();
    if (out.fragments.size() >= xsql_internal::kMaxFragmentCount) {
      throw std::runtime_error("FRAGMENTS exceeds maximum fragment count");
    }
    if (total_bytes > xsql_internal::kMaxFragmentBytes) {
      throw std::runtime_error("FRAGMENTS exceeds maximum total HTML size");
    }
    out.fragments.push_back(std::move(trimmed));
  }
  if (out.fragments.empty()) {
    throw std::runtime_error("FRAGMENTS produced no HTML fragments");
  }
  return out;
}

QueryResult execute_query_ast(const Query& query, const HtmlDocument& doc, const std::string& source_uri) {
  ExecuteResult exec = execute_query(query, doc, source_uri);
  QueryResult out;
  out.columns = xsql_internal::build_columns(query);
  out.columns_implicit = !xsql_internal::is_projection_query(query);
  out.source_uri_excluded =
      std::find(query.exclude_fields.begin(), query.exclude_fields.end(), "source_uri") !=
      query.exclude_fields.end();
  out.to_list = query.to_list;
  out.to_table = query.to_table;
  out.table_has_header = query.table_has_header;
  if (query.export_sink.has_value()) {
    const auto& sink = *query.export_sink;
    if (sink.kind == Query::ExportSink::Kind::Csv) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Csv;
    } else if (sink.kind == Query::ExportSink::Kind::Parquet) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Parquet;
    }
    out.export_sink.path = sink.path;
  }
  if (query.export_sink.has_value() &&
      (query.to_table || xsql_internal::is_table_select(query)) &&
      exec.nodes.size() != 1) {
    throw std::runtime_error(
        "Export requires a single table result; add a filter to select one table");
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Tfidf) {
    out.rows = xsql_internal::build_tfidf_rows(query, exec.nodes);
    return out;
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!query.order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : query.order_by) {
                    int cmp = 0;
                    if (order_by.field == "count") {
                      if (a.second < b.second) cmp = -1;
                      else if (a.second > b.second) cmp = 1;
                    } else {
                      if (a.first < b.first) cmp = -1;
                      else if (a.first > b.first) cmp = 1;
                    }
                    if (cmp == 0) continue;
                    if (order_by.descending) {
                      return cmp > 0;
                    }
                    return cmp < 0;
                  }
                  return false;
                });
    } else {
      std::sort(summary.begin(), summary.end(),
                [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
                });
    }
    if (query.limit.has_value() && summary.size() > *query.limit) {
      summary.resize(*query.limit);
    }
    for (const auto& item : summary) {
      QueryResultRow row;
      row.tag = item.first;
      row.node_id = static_cast<int64_t>(item.second);
      row.source_uri = source_uri;
      out.rows.push_back(std::move(row));
    }
    return out;
  }
  // WHY: table extraction bypasses row projections to preserve table layout.
  if (query.to_table ||
      (query.export_sink.has_value() && xsql_internal::is_table_select(query))) {
    auto children = xsql_internal::build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      xsql_internal::collect_rows(doc, children, node.id, table.rows);
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  const Query::SelectItem* flatten_item = nullptr;
  for (const auto& item : query.select_items) {
    if (item.flatten_text) {
      flatten_item = &item;
      break;
    }
  }
  if (flatten_item != nullptr) {
    auto children = xsql_internal::build_children(doc);
    std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
    for (size_t parent = 0; parent < children.size(); ++parent) {
      const auto& kids = children[parent];
      for (size_t idx = 0; idx < kids.size(); ++idx) {
        sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
      }
    }
    DescendantTagFilter descendant_filter;
    if (query.where.has_value()) {
      collect_descendant_tag_filter(*query.where, descendant_filter);
    }
    std::string base_tag = util::to_lower(flatten_item->tag);
    bool tag_is_alias = query.source.alias.has_value() &&
                        util::to_lower(*query.source.alias) == base_tag;
    bool match_all_tags = tag_is_alias || base_tag == "document";
    struct FlattenRow {
      const HtmlNode* node = nullptr;
      QueryResultRow row;
    };
    std::vector<FlattenRow> rows;
    rows.reserve(doc.nodes.size());
    for (const auto& node : doc.nodes) {
      if (!match_all_tags && node.tag != base_tag) {
        continue;
      }
      if (query.where.has_value()) {
        if (!executor_internal::eval_expr_flatten_base(*query.where, doc, children, node)) {
          continue;
        }
      }
      QueryResultRow row;
      row.node_id = node.id;
      row.tag = node.tag;
      row.text = node.text;
      row.inner_html = node.inner_html;
      row.attributes = node.attributes;
      row.source_uri = source_uri;
      row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
      row.max_depth = node.max_depth;
      row.doc_order = node.doc_order;
      row.parent_id = node.parent_id;

      std::vector<int64_t> descendants;
      bool depth_is_default = !flatten_item->flatten_depth.has_value();
      if (depth_is_default) {
        collect_descendants_any_depth(children, node.id, descendants);
      } else {
        collect_descendants_at_depth(children, node.id, *flatten_item->flatten_depth, descendants);
      }
      std::vector<std::string> values;
      for (int64_t id : descendants) {
        const auto& child = doc.nodes.at(static_cast<size_t>(id));
        bool matched = true;
        for (const auto& pred : descendant_filter.predicates) {
          if (!match_descendant_predicate(child, pred)) {
            matched = false;
            break;
          }
        }
        if (!matched) continue;
        std::string direct = xsql_internal::extract_direct_text_strict(child.inner_html);
        std::string normalized = normalize_flatten_text(direct);
        if (normalized.empty()) {
          direct = xsql_internal::extract_direct_text(child.inner_html);
          normalized = normalize_flatten_text(direct);
        }
        if (depth_is_default && normalized.empty()) {
          continue;
        }
        values.push_back(std::move(normalized));
      }
      for (size_t i = 0; i < flatten_item->flatten_aliases.size(); ++i) {
        if (i < values.size()) {
          row.computed_fields[flatten_item->flatten_aliases[i]] = values[i];
        }
      }
      rows.push_back(FlattenRow{&node, std::move(row)});
    }
    if (!query.order_by.empty()) {
      std::stable_sort(rows.begin(), rows.end(),
                       [&](const auto& left, const auto& right) {
                         for (const auto& order_by : query.order_by) {
                           int cmp = executor_internal::compare_nodes(*left.node, *right.node, order_by.field);
                           if (cmp == 0) continue;
                           if (order_by.descending) {
                             return cmp > 0;
                           }
                           return cmp < 0;
                         }
                         return false;
                       });
    }
    if (query.limit.has_value() && rows.size() > *query.limit) {
      rows.resize(*query.limit);
    }
    out.rows.reserve(rows.size());
    for (auto& entry : rows) {
      out.rows.push_back(std::move(entry.row));
    }
    return out;
  }
  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = source_uri;
      out.rows.push_back(row);
      return out;
    }
  }
  auto inner_html_depth = xsql_internal::find_inner_html_depth(query);
  const Query::SelectItem* trim_item = xsql_internal::find_trim_item(query);
  bool use_text_function = false;
  bool use_inner_html_function = false;
  bool use_raw_inner_html_function = false;
  for (const auto& item : query.select_items) {
    if (item.field.has_value() && *item.field == "text" && item.text_function) {
      use_text_function = true;
    }
    if (item.field.has_value() && *item.field == "inner_html" && item.inner_html_function) {
      use_inner_html_function = true;
      if (item.raw_inner_html_function) {
        use_raw_inner_html_function = true;
      }
    }
  }
  std::optional<size_t> effective_inner_html_depth = inner_html_depth;
  if (!effective_inner_html_depth.has_value() && use_inner_html_function) {
    effective_inner_html_depth = 1;
  }
  auto children = xsql_internal::build_children(doc);
  std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
  for (size_t parent = 0; parent < children.size(); ++parent) {
    const auto& kids = children[parent];
    for (size_t idx = 0; idx < kids.size(); ++idx) {
      sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
    }
  }
  for (const auto& node : exec.nodes) {
    QueryResultRow row;
    row.node_id = node.id;
    row.tag = node.tag;
    row.text = use_text_function ? xsql_internal::extract_direct_text(node.inner_html) : node.text;
    row.inner_html = effective_inner_html_depth.has_value()
                         ? xsql_internal::limit_inner_html(node.inner_html, *effective_inner_html_depth)
                         : node.inner_html;
    if (use_inner_html_function && !use_raw_inner_html_function) {
      row.inner_html = util::minify_html(row.inner_html);
    }
    row.attributes = node.attributes;
    row.source_uri = source_uri;
    row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
    row.max_depth = node.max_depth;
    row.doc_order = node.doc_order;
    if (trim_item != nullptr && trim_item->field.has_value()) {
      const std::string& field = *trim_item->field;
      if (field == "text") {
        row.text = util::trim_ws(row.text);
      } else if (field == "inner_html") {
        row.inner_html = util::trim_ws(row.inner_html);
      } else if (field == "tag") {
        row.tag = util::trim_ws(row.tag);
      } else if (field == "source_uri") {
        row.source_uri = util::trim_ws(row.source_uri);
      } else {
        auto it = row.attributes.find(field);
        if (it != row.attributes.end()) {
          it->second = util::trim_ws(it->second);
        }
      }
    }
    row.parent_id = node.parent_id;
    out.rows.push_back(row);
  }
  return out;
}

void validate_query(const Query& query) {
  if (query.kind != Query::Kind::Select) {
    return;
  }
  xsql_internal::validate_projection(query);
  xsql_internal::validate_order_by(query);
  xsql_internal::validate_to_table(query);
  xsql_internal::validate_export_sink(query);
  xsql_internal::validate_qualifiers(query);
  xsql_internal::validate_predicates(query);
  xsql_internal::validate_limits(query);
}

QueryResult execute_query_with_source(const Query& query,
                                      const std::string& default_html,
                                      const std::string& default_source_uri) {
  std::string effective_source_uri = default_source_uri;
  if (query.source.kind == Source::Kind::RawHtml) {
    if (query.source.value.size() > xsql_internal::kMaxRawHtmlBytes) {
      throw std::runtime_error("RAW() HTML exceeds maximum size");
    }
    HtmlDocument doc = parse_html(query.source.value);
    effective_source_uri = "raw";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  if (query.source.kind == Source::Kind::Fragments) {
    FragmentSource fragments;
    if (query.source.fragments_raw.has_value()) {
      if (query.source.fragments_raw->size() > xsql_internal::kMaxRawHtmlBytes) {
        throw std::runtime_error("FRAGMENTS RAW() input exceeds maximum size");
      }
      fragments.fragments.push_back(*query.source.fragments_raw);
    } else if (query.source.fragments_query != nullptr) {
      const Query& subquery = *query.source.fragments_query;
      validate_query(subquery);
      if (subquery.source.kind == Source::Kind::Path || subquery.source.kind == Source::Kind::Url) {
        throw std::runtime_error("FRAGMENTS subquery cannot use URL or file sources");
      }
      QueryResult sub_result = execute_query_with_source(subquery, default_html, default_source_uri);
      fragments = collect_fragments(sub_result);
    } else {
      throw std::runtime_error("FRAGMENTS requires a subquery or RAW('<...>') input");
    }
    HtmlDocument doc = build_fragments_document(fragments);
    effective_source_uri = "fragment";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  HtmlDocument doc = parse_html(default_html);
  return execute_query_ast(query, doc, effective_source_uri);
}

/// Executes a parsed query over provided HTML and assembles QueryResult.
/// MUST apply validation before execution and MUST propagate errors as exceptions.
/// Inputs are HTML/source/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_html(const std::string& html,
                                    const std::string& source_uri,
                                    const std::string& query) {
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_query(*parsed.query);
  if (parsed.query->kind != Query::Kind::Select) {
    return execute_meta_query(*parsed.query, source_uri);
  }
  return execute_query_with_source(*parsed.query, html, source_uri);
}

}  // namespace

/// Executes a query over in-memory HTML with document as the source label.
/// MUST not perform IO and MUST propagate parse/validation failures.
/// Inputs are HTML/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_document(const std::string& html, const std::string& query) {
  return execute_query_from_html(html, "document", query);
}

/// Executes a query over a file and uses the path as source label.
/// MUST read from disk and MUST propagate IO failures as exceptions.
/// Inputs are path/query; outputs are QueryResult with file IO side effects.
QueryResult execute_query_from_file(const std::string& path, const std::string& query) {
  std::string html = xsql_internal::read_file(path);
  return execute_query_from_html(html, path, query);
}

/// Executes a query over a URL and uses the URL as source label.
/// MUST honor timeout_ms and MUST propagate network failures as exceptions.
/// Inputs are url/query/timeout; outputs are QueryResult with network side effects.
QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms) {
  std::string html = xsql_internal::fetch_url(url, timeout_ms);
  return execute_query_from_html(html, url, query);
}

}  // namespace xsql
