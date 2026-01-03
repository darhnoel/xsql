#include "parser_impl.h"

#include "../util/string_util.h"

#ifdef XSQL_USE_LIBXML2

#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

namespace xsql {

namespace {

/// Appends text nodes to all open elements in the stack.
/// MUST preserve document order and MUST ignore null/empty text.
/// Inputs are doc/stack/text; outputs are updated node.text values.
void append_text_to_stack(HtmlDocument& doc, const std::vector<int64_t>& stack, const char* text) {
  if (!text) return;
  std::string value(text);
  if (value.empty()) return;
  for (int64_t node_id : stack) {
    doc.nodes[static_cast<size_t>(node_id)].text += value;
  }
}

/// Serializes a node's children into an inner HTML string.
/// MUST return empty for null nodes and MUST not mutate the document.
/// Inputs are libxml2 nodes; outputs are HTML strings with no side effects.
std::string dump_inner_html(xmlNode* node) {
  if (!node || !node->children) return "";
  xmlBufferPtr buffer = xmlBufferCreate();
  if (!buffer) return "";
  for (xmlNode* child = node->children; child != nullptr; child = child->next) {
    xmlNodeDump(buffer, node->doc, child, 0, 0);
  }
  std::string out;
  const xmlChar* content = xmlBufferContent(buffer);
  if (content) {
    out = reinterpret_cast<const char*>(content);
  }
  xmlBufferFree(buffer);
  return out;
}

/// Walks libxml2 nodes to build the HtmlDocument representation.
/// MUST keep traversal deterministic and MUST preserve parent relationships.
/// Inputs are doc/node/stack; outputs are appended nodes in doc.
void walk_node(HtmlDocument& doc, xmlNode* node, std::vector<int64_t>& stack) {
  for (xmlNode* cur = node; cur != nullptr; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      HtmlNode out;
      out.id = static_cast<int64_t>(doc.nodes.size());
      out.tag = util::to_lower(reinterpret_cast<const char*>(cur->name));
      if (!stack.empty()) {
        out.parent_id = stack.back();
      }
      for (xmlAttr* attr = cur->properties; attr != nullptr; attr = attr->next) {
        std::string name = util::to_lower(reinterpret_cast<const char*>(attr->name));
        xmlChar* value = xmlNodeListGetString(cur->doc, attr->children, 1);
        if (value) {
          out.attributes[name] = reinterpret_cast<const char*>(value);
          xmlFree(value);
        } else {
          out.attributes[name] = "";
        }
      }
      out.inner_html = dump_inner_html(cur);
      doc.nodes.push_back(out);
      stack.push_back(out.id);
      if (cur->children) {
        walk_node(doc, cur->children, stack);
      }
      stack.pop_back();
    } else if (cur->type == XML_TEXT_NODE || cur->type == XML_CDATA_SECTION_NODE) {
      append_text_to_stack(doc, stack, reinterpret_cast<const char*>(cur->content));
    } else if (cur->children) {
      walk_node(doc, cur->children, stack);
    }
  }
}

}  // namespace

/// Parses HTML with libxml2 into the internal HtmlDocument representation.
/// MUST recover from malformed HTML and MUST avoid executing scripts.
/// Inputs are HTML strings; outputs are HtmlDocument with no side effects.
HtmlDocument parse_html_libxml2(const std::string& html) {
  HtmlDocument doc;
  // WHY: recovery mode handles malformed HTML commonly found on the web.
  htmlDocPtr html_doc = htmlReadMemory(
      html.data(),
      static_cast<int>(html.size()),
      nullptr,
      nullptr,
      HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
  if (!html_doc) {
    return doc;
  }

  xmlNode* root = xmlDocGetRootElement(html_doc);
  std::vector<int64_t> stack;
  if (root) {
    walk_node(doc, root, stack);
  }
  xmlFreeDoc(html_doc);
  return doc;
}

}  // namespace xsql

#else

namespace xsql {

HtmlDocument parse_html_libxml2(const std::string&) {
  return {};
}

}  // namespace xsql

#endif
