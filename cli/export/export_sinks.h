#pragma once

#include <string>

#include "xsql/xsql.h"

namespace xsql::cli {

bool export_result(const xsql::QueryResult& result, std::string& error);
bool write_csv(const xsql::QueryResult& result, const std::string& path, std::string& error);
bool write_parquet(const xsql::QueryResult& result, const std::string& path, std::string& error);
bool write_table_csv(const xsql::QueryResult::TableResult& table,
                     const std::string& path,
                     std::string& error,
                     bool table_has_header);
bool write_table_parquet(const xsql::QueryResult::TableResult& table,
                         const std::string& path,
                         std::string& error);

}  // namespace xsql::cli
