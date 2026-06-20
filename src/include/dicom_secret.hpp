#include "duckdb.hpp"

namespace duckdb {

void CheckFileExists(KeyValueSecret &, FileSystem &, string);
unique_ptr<BaseSecret> CreateDicomSecretFunction(ClientContext &, CreateSecretInput &);
void RegisterDicomSecret(ExtensionLoader &);

} // namespace duckdb
