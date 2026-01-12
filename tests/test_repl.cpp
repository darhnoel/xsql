#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "test_harness.h"

#include "repl/commands/registry.h"
#include "repl/commands/summarize_content_command.h"
#include "repl/core/line_editor.h"
#include "repl/plugin_manager.h"
#include "repl/core/repl.h"

namespace {

struct StreamCapture {
  std::ostream* stream = nullptr;
  std::ostringstream buffer;
  std::streambuf* original = nullptr;

  explicit StreamCapture(std::ostream& target)
      : stream(&target), original(target.rdbuf(buffer.rdbuf())) {}
  ~StreamCapture() {
    if (stream && original) {
      buffer.flush();
      stream->rdbuf(original);
    }
  }

  std::string str() const { return buffer.str(); }
};

}  // namespace

static void test_summarize_content_basic() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "xsql> ", 6);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  sources["doc"] = xsql::cli::LoadedSource{
      "inline", std::optional<std::string>("<html><body><div>Hello Khmer World</div></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  auto handler = xsql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content", ctx);
  expect_true(handled, "summarize_content should handle command");
  std::string output = capture.str();
  expect_true(output.find("hello") != std::string::npos, "output should include token 'hello'");
}

static void test_summarize_content_khmer_requires_plugin() {
  StreamCapture capture(std::cerr);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "xsql> ", 6);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  sources["doc"] = xsql::cli::LoadedSource{
      "inline", std::optional<std::string>("<html><body><div>សូមអរគុណ</div></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  auto handler = xsql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content --lang khmer", ctx);
  expect_true(handled, "summarize_content should handle khmer command");
  std::string output = capture.str();
  expect_true(output.find(".plugin install khmer_segmenter") != std::string::npos,
              "missing plugin should suggest .plugin install khmer_segmenter");
}

static void test_summarize_content_max_tokens() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "xsql> ", 6);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  sources["doc"] = xsql::cli::LoadedSource{
      "inline",
      std::optional<std::string>("<html><body><h3>Alpha Alpha Beta</h3><p>Gamma</p></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  auto handler = xsql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content --max_tokens 1", ctx);
  expect_true(handled, "summarize_content should handle max_tokens");
  std::string output = capture.str();
  expect_true(output.find("alpha") != std::string::npos, "output should include token 'alpha'");
  expect_true(output.find("beta") == std::string::npos, "output should not include token 'beta'");
  expect_true(output.find("gamma") == std::string::npos, "output should not include token 'gamma'");
}

void register_repl_tests(std::vector<TestCase>& tests) {
  tests.push_back({"summarize_content_basic", test_summarize_content_basic});
  tests.push_back({"summarize_content_khmer_requires_plugin",
                   test_summarize_content_khmer_requires_plugin});
  tests.push_back({"summarize_content_max_tokens",
                   test_summarize_content_max_tokens});
}
