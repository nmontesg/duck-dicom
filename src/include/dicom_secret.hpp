#include "duckdb.hpp"

namespace duckdb {

unique_ptr<BaseSecret> CreateDicomSecretFunction(ClientContext &context, CreateSecretInput &input);
void RegisterDicomSecret(ExtensionLoader &loader);

} // namespace duckdb
