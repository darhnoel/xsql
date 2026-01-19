cmake_minimum_required(VERSION 3.16)
project(xsql LANGUAGES CXX)

option(XSQL_BUILD_CLI "Build the xsql CLI" ON)
option(XSQL_BUILD_TESTS "Build tests" ON)
option(XSQL_BUILD_PYTHON "Build the Python module" OFF)
option(XSQL_WITH_LIBXML2 "Use libxml2 for HTML parsing" ON)
option(XSQL_WITH_NLOHMANN_JSON "Use nlohmann/json for CLI output" ON)
option(XSQL_WITH_CURL "Use libcurl for URL fetching" ON)
option(XSQL_WITH_ARROW "Enable Apache Arrow for Parquet export" ON)
option(XSQL_ENABLE_KHMER_NUMBER "Enable built-in Khmer number module" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xsql_core
  core/src/ast.cpp
  core/src/query_parser.cpp
  core/src/parser/parser.cpp
  core/src/parser/parser_expr.cpp
  core/src/parser/parser_query.cpp
  core/src/parser/parser_select.cpp
  core/src/parser/parser_source.cpp
  core/src/parser/parser_util.cpp
  core/src/parser/lexer.cpp
  core/src/html_parser.cpp
  core/src/html/parser_naive.cpp
  core/src/html/parser_libxml2.cpp
  core/src/executor/executor.cpp
  core/src/executor/filter.cpp
  core/src/executor/order.cpp
  core/src/util/string_util.cpp
  core/src/xsql/execute.cpp
  core/src/xsql/io.cpp
  core/src/xsql/validation.cpp
  core/src/xsql/result_builder.cpp
  core/src/xsql/table_extract.cpp
  core/src/xsql/tfidf.cpp
)

target_include_directories(xsql_core
  PUBLIC
    core/include
)

target_compile_features(xsql_core PUBLIC cxx_std_20)

if (XSQL_ENABLE_KHMER_NUMBER)
  target_sources(xsql_core PRIVATE core/src/khmer_number.cpp)
  target_compile_definitions(xsql_core PUBLIC XSQL_ENABLE_KHMER_NUMBER)
endif()

if (XSQL_WITH_LIBXML2)
  find_package(LibXml2)
  if (LibXml2_FOUND)
    target_include_directories(xsql_core PRIVATE ${LIBXML2_INCLUDE_DIR})
    target_link_libraries(xsql_core PRIVATE ${LIBXML2_LIBRARIES})
    target_compile_definitions(xsql_core PRIVATE XSQL_USE_LIBXML2)
  else()
    message(WARNING "LibXml2 not found; falling back to naive HTML parser.")
  endif()
endif()

if (XSQL_WITH_CURL)
  find_package(CURL CONFIG QUIET)
  if (TARGET CURL::libcurl)
    target_link_libraries(xsql_core PRIVATE CURL::libcurl)
    target_compile_definitions(xsql_core PUBLIC XSQL_USE_CURL)
  else()
    find_package(CURL)
    if (CURL_FOUND)
      target_include_directories(xsql_core PRIVATE ${CURL_INCLUDE_DIRS})
      target_link_libraries(xsql_core PRIVATE ${CURL_LIBRARIES})
      target_compile_definitions(xsql_core PUBLIC XSQL_USE_CURL)
    else()
      message(WARNING "libcurl not found; URL fetching disabled.")
    endif()
  endif()
endif()

if (XSQL_WITH_ARROW)
  find_package(Arrow CONFIG QUIET)
  find_package(Parquet CONFIG QUIET)
  if (TARGET Arrow::arrow_shared)
    set(XSQL_ARROW_TARGET Arrow::arrow_shared)
  elseif (TARGET Arrow::arrow_static)
    set(XSQL_ARROW_TARGET Arrow::arrow_static)
  elseif (TARGET Arrow::arrow)
    set(XSQL_ARROW_TARGET Arrow::arrow)
  elseif (TARGET arrow_shared)
    set(XSQL_ARROW_TARGET arrow_shared)
  elseif (TARGET arrow_static)
    set(XSQL_ARROW_TARGET arrow_static)
  elseif (TARGET arrow)
    set(XSQL_ARROW_TARGET arrow)
  endif()
  if (TARGET Parquet::parquet_shared)
    set(XSQL_PARQUET_TARGET Parquet::parquet_shared)
  elseif (TARGET Parquet::parquet_static)
    set(XSQL_PARQUET_TARGET Parquet::parquet_static)
  elseif (TARGET Parquet::parquet)
    set(XSQL_PARQUET_TARGET Parquet::parquet)
  elseif (TARGET parquet_shared)
    set(XSQL_PARQUET_TARGET parquet_shared)
  elseif (TARGET parquet_static)
    set(XSQL_PARQUET_TARGET parquet_static)
  elseif (TARGET parquet)
    set(XSQL_PARQUET_TARGET parquet)
  endif()
  if (NOT XSQL_ARROW_TARGET OR NOT XSQL_PARQUET_TARGET)
    message(WARNING "Arrow/Parquet not found; TO PARQUET disabled.")
  endif()
endif()

if (XSQL_BUILD_CLI)
  add_executable(xsql
    cli/main.cpp
    cli/cli_args.cpp
    cli/cli_utils.cpp
    cli/repl/core/line_editor.cpp
    cli/repl/ui/autocomplete.cpp
    cli/repl/state/history.cpp
    cli/repl/input/terminal.cpp
    cli/repl/ui/render.cpp
    cli/repl/input/text_util.cpp
    cli/repl/core/repl.cpp
    cli/repl/config.cpp
    cli/repl/commands/registry.cpp
    cli/repl/commands/help_command.cpp
    cli/repl/commands/display_mode_command.cpp
    cli/repl/commands/mode_command.cpp
    cli/repl/commands/max_rows_command.cpp
    cli/repl/commands/reload_config_command.cpp
    cli/repl/commands/summarize_command.cpp
    cli/repl/commands/load_command.cpp
    cli/repl/commands/plugin_command.cpp
    cli/repl/commands/plugin_install_command.cpp
    cli/repl/commands/plugin_registry.cpp
    cli/repl/commands/summarize_content_command.cpp
    cli/repl/plugin_manager.cpp
    cli/ui/color.cpp
    cli/export/export_sinks.cpp
    cli/render/duckbox_renderer.cpp
  )
  if (XSQL_ENABLE_KHMER_NUMBER)
    target_sources(xsql PRIVATE cli/repl/commands/khmer_number_command.cpp)
  endif()
  target_link_libraries(xsql PRIVATE xsql_core)
  target_include_directories(xsql PRIVATE core/src core/include cli)
  if (UNIX AND NOT APPLE)
    target_link_libraries(xsql PRIVATE dl)
  endif()
  if (XSQL_WITH_NLOHMANN_JSON)
    find_package(nlohmann_json CONFIG)
    if (nlohmann_json_FOUND)
      target_link_libraries(xsql PRIVATE nlohmann_json::nlohmann_json)
      target_compile_definitions(xsql PRIVATE XSQL_USE_NLOHMANN_JSON)
    else()
      message(WARNING "nlohmann_json not found; using built-in JSON output.")
    endif()
  endif()
  if (XSQL_ARROW_TARGET AND XSQL_PARQUET_TARGET)
    target_link_libraries(xsql PRIVATE ${XSQL_ARROW_TARGET} ${XSQL_PARQUET_TARGET})
    target_compile_definitions(xsql PRIVATE XSQL_USE_ARROW)
  endif()
endif()

if (XSQL_BUILD_TESTS)
  enable_testing()
  add_executable(xsql_tests
    tests/test_main.cpp
    tests/test_harness.cpp
    tests/test_utils.cpp
    tests/test_query_basic.cpp
    tests/test_sources_alias.cpp
    tests/test_shorthand.cpp
    tests/test_projections.cpp
    tests/test_functions.cpp
    tests/test_axes.cpp
    tests/test_predicates.cpp
    tests/test_order_by.cpp
    tests/test_duckbox.cpp
    tests/test_exports.cpp
    tests/test_malformed_html.cpp
    tests/test_flatten_text.cpp
    tests/test_repl.cpp
    tests/test_fragments.cpp
    tests/test_guardrails.cpp
    tests/test_meta_commands.cpp
    tests/test_cli_utils.cpp
    cli/repl/core/line_editor.cpp
    cli/repl/ui/autocomplete.cpp
    cli/repl/state/history.cpp
    cli/repl/input/terminal.cpp
    cli/repl/ui/render.cpp
    cli/repl/input/text_util.cpp
    cli/repl/plugin_manager.cpp
    cli/repl/config.cpp
    cli/repl/commands/registry.cpp
    cli/repl/commands/help_command.cpp
    cli/repl/commands/display_mode_command.cpp
    cli/repl/commands/mode_command.cpp
    cli/repl/commands/max_rows_command.cpp
    cli/repl/commands/reload_config_command.cpp
    cli/repl/commands/summarize_command.cpp
    cli/repl/commands/load_command.cpp
    cli/repl/commands/plugin_command.cpp
    cli/repl/commands/plugin_install_command.cpp
    cli/repl/commands/plugin_registry.cpp
    cli/repl/commands/summarize_content_command.cpp
    cli/cli_utils.cpp
    cli/ui/color.cpp
    cli/export/export_sinks.cpp
    cli/render/duckbox_renderer.cpp
  )
  if (XSQL_ENABLE_KHMER_NUMBER)
    target_sources(xsql_tests PRIVATE
      cli/repl/commands/khmer_number_command.cpp
      tests/test_khmer_number.cpp
    )
  endif()
  target_link_libraries(xsql_tests PRIVATE xsql_core)
  target_include_directories(xsql_tests PRIVATE cli core/src core/include tests)
  if (XSQL_ARROW_TARGET AND XSQL_PARQUET_TARGET)
    target_link_libraries(xsql_tests PRIVATE ${XSQL_ARROW_TARGET} ${XSQL_PARQUET_TARGET})
    target_compile_definitions(xsql_tests PRIVATE XSQL_USE_ARROW)
  endif()
  set(XSQL_TESTS
    select_ul_by_id
    class_in_matches_token
    parent_attribute_filter
    multi_tag_select
    select_star
    class_eq_matches_token
    missing_attribute_no_match
    invalid_query_throws
    text_requires_non_tag_filter
    projection_parent_id
    projection_attributes
    projection_tag_field_list
    projection_sibling_pos
    inner_html_function
    inner_html_depth
    trim_inner_html
    child_axis_direct_only
    ancestor_filter_on_a
    limit
    alias_qualifier
    alias_source_only
    shorthand_attribute_filter
    shorthand_qualified_attribute_filter
    ancestor_attribute_filter
    descendant_attribute_filter
    parent_tag_filter
    parent_id_filter
    attributes_is_null
    attributes_is_not_null
    node_id_filter
    select_exclude_single
    select_exclude_list
    to_table_flag
    to_list_flag
    attribute_projection_value
    count_aggregate
    count_star
    summarize_star
    summarize_limit
    summarize_order_by_count
    order_by_tag
    order_by_node_id_desc
    order_by_multi
    not_equal_attribute
    is_not_null_attribute
    is_null_attribute
    text_not_equal
    regex_attribute
    contains_attribute
    contains_all_attribute
    contains_any_attribute
    sibling_pos_filter
    has_direct_text
    parenthesized_predicates
    duckbox_basic_table
    duckbox_truncate_cells
    duckbox_maxrows_truncate
    duckbox_numeric_alignment
    duckbox_null_rendering
    csv_escaping
    csv_export_integration
    raw_source_literal
    fragments_from_raw
    fragments_from_inner_html
    fragments_non_html_error
    guardrails_limit_cap
    guardrails_regex_length_cap
    guardrails_fragments_count_cap
    malformed_missing_closing_tags
    malformed_mismatched_nesting
    malformed_junk_bytes_no_throw
    summarize_content_basic
    summarize_content_khmer_requires_plugin
    summarize_content_max_tokens
    parse_show_describe
    execute_describe_doc
    show_functions_output
    describe_language_output
    show_input_requires_source
    show_inputs_requires_source
    show_inputs_fallback_source
    auto_include_source_uri
    count_table_rows_header
    count_table_rows_no_header
    count_result_rows
  )
  if (XSQL_ENABLE_KHMER_NUMBER)
    list(APPEND XSQL_TESTS
      khmer_number_digits
      khmer_number_scale_boundaries
      khmer_number_decimals_negatives
      khmer_number_concatenated_input
      khmer_number_round_trip_random
      khmer_number_golden_vectors
      khmer_number_command_to_words
      khmer_number_command_compact
    )
  endif()
  if (XSQL_ARROW_TARGET AND XSQL_PARQUET_TARGET)
    list(APPEND XSQL_TESTS parquet_export_smoke)
  endif()
  foreach(test_name IN LISTS XSQL_TESTS)
    add_test(NAME xsql_${test_name} COMMAND xsql_tests ${test_name})
  endforeach()
endif()

if (XSQL_BUILD_PYTHON)
  find_package(Python COMPONENTS Development.Module REQUIRED)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(_core python/xsql/_core.cpp)
  target_link_libraries(_core PRIVATE xsql_core)
  target_include_directories(_core PRIVATE core/include)
  set_target_properties(_core PROPERTIES OUTPUT_NAME "_core")
  if (DEFINED SKBUILD_PLATLIB_DIR)
    set_target_properties(_core PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${SKBUILD_PLATLIB_DIR}/xsql")
  endif()
endif()
