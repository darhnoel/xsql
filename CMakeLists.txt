cmake_minimum_required(VERSION 3.16)
project(xsql LANGUAGES CXX)

option(XSQL_BUILD_CLI "Build the xsql CLI" ON)
option(XSQL_BUILD_TESTS "Build tests" ON)
option(XSQL_WITH_LIBXML2 "Use libxml2 for HTML parsing" ON)
option(XSQL_WITH_NLOHMANN_JSON "Use nlohmann/json for CLI output" ON)
option(XSQL_WITH_CURL "Use libcurl for URL fetching" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xsql_core
  core/src/ast.cpp
  core/src/query_parser.cpp
  core/src/html_parser.cpp
  core/src/executor.cpp
  core/src/xsql.cpp
)

target_include_directories(xsql_core
  PUBLIC
    core/include
)

target_compile_features(xsql_core PUBLIC cxx_std_20)

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

if (XSQL_BUILD_CLI)
  add_executable(xsql
    cli/main.cpp
    cli/render/duckbox_renderer.cpp
  )
  target_link_libraries(xsql PRIVATE xsql_core)
  target_include_directories(xsql PRIVATE core/src cli)
  if (XSQL_WITH_NLOHMANN_JSON)
    find_package(nlohmann_json CONFIG)
    if (nlohmann_json_FOUND)
      target_link_libraries(xsql PRIVATE nlohmann_json::nlohmann_json)
      target_compile_definitions(xsql PRIVATE XSQL_USE_NLOHMANN_JSON)
    else()
      message(WARNING "nlohmann_json not found; using built-in JSON output.")
    endif()
  endif()
endif()

if (XSQL_BUILD_TESTS)
  enable_testing()
  add_executable(xsql_tests
    tests/test_main.cpp
    cli/render/duckbox_renderer.cpp
  )
  target_link_libraries(xsql_tests PRIVATE xsql_core)
  target_include_directories(xsql_tests PRIVATE cli)
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
    duckbox_basic_table
    duckbox_truncate_cells
    duckbox_maxrows_truncate
    duckbox_numeric_alignment
    duckbox_null_rendering
  )
  foreach(test_name IN LISTS XSQL_TESTS)
    add_test(NAME xsql_${test_name} COMMAND xsql_tests ${test_name})
  endforeach()
endif()
