#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace xsql {

struct Span {
  size_t start = 0;
  size_t end = 0;
};

struct Query;

struct Source {
  enum class Kind { Document, Path, Url, RawHtml, Fragments } kind = Kind::Document;
  std::string value;
  std::optional<std::string> alias;
  std::shared_ptr<Query> fragments_query;
  std::optional<std::string> fragments_raw;
  Span span;
};

struct Operand {
  enum class Axis { Self, Parent, Child, Ancestor, Descendant } axis = Axis::Self;
  enum class FieldKind {
    Attribute,
    AttributesMap,
    Tag,
    Text,
    NodeId,
    ParentId,
    SiblingPos,
    MaxDepth,
    DocOrder
  } field_kind = FieldKind::Attribute;
  std::string attribute;
  std::optional<std::string> qualifier;
  Span span;
};

struct ValueList {
  std::vector<std::string> values;
  Span span;
};

struct CompareExpr {
  enum class Op {
    Eq,
    In,
    NotEq,
    IsNull,
    IsNotNull,
    Regex,
    Contains,
    ContainsAll,
    ContainsAny,
    HasDirectText
  } op = Op::Eq;
  Operand lhs;
  ValueList rhs;
  Span span;
};

struct ExistsExpr;
struct BinaryExpr;
using Expr = std::variant<CompareExpr, std::shared_ptr<ExistsExpr>, std::shared_ptr<BinaryExpr>>;

struct ExistsExpr {
  Operand::Axis axis = Operand::Axis::Self;
  std::optional<Expr> where;
  Span span;
};

struct BinaryExpr {
  enum class Op { And, Or } op = Op::And;
  Expr left;
  Expr right;
  Span span;
};

struct Query {
  enum class Kind {
    Select,
    ShowInput,
    ShowInputs,
    ShowFunctions,
    ShowAxes,
    ShowOperators,
    DescribeDoc,
    DescribeLanguage
  } kind = Kind::Select;
  struct ExportSink {
    enum class Kind { None, Csv, Parquet } kind = Kind::None;
    std::string path;
    Span span;
  };
  struct OrderBy {
    std::string field;
    bool descending = false;
    Span span;
  };
  struct SelectItem {
    enum class Aggregate { None, Count, Summarize, Tfidf } aggregate = Aggregate::None;
    enum class TfidfStopwords { English, None } tfidf_stopwords = TfidfStopwords::English;
    std::string tag;
    std::vector<std::string> tfidf_tags;
    std::optional<std::string> field;
    bool tfidf_all_tags = false;
    size_t tfidf_top_terms = 30;
    size_t tfidf_min_df = 1;
    size_t tfidf_max_df = 0;
    std::optional<size_t> inner_html_depth;
    bool inner_html_function = false;
    bool raw_inner_html_function = false;
    bool text_function = false;
    bool trim = false;
    bool flatten_text = false;
    std::optional<size_t> flatten_depth;
    std::vector<std::string> flatten_aliases;
    Span span;
  };
  std::vector<SelectItem> select_items;
  Source source;
  std::optional<Expr> where;
  std::vector<OrderBy> order_by;
  std::vector<std::string> exclude_fields;
  std::optional<size_t> limit;
  bool to_list = false;
  bool to_table = false;
  bool table_has_header = true;
  std::optional<ExportSink> export_sink;
  Span span;
};

}  // namespace xsql
