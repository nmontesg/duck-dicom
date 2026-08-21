#pragma once

#include "duckdb.hpp" // IWYU pragma: keep

namespace duckdb {

void CheckFileExists(KeyValueSecret &, FileSystem &, const string &);
unique_ptr<BaseSecret> CreateDicomSecretFunction(ClientContext &, CreateSecretInput &);
void RegisterDicomSecret(ExtensionLoader &);

} // namespace duckdb
