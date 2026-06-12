#include "duckdb.hpp"

namespace duckdb {

LogicalType DICOM_TAG();

void DicomTagToVarchar(Vector &source, Vector &result, idx_t count);
bool ToVarcharCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

void VarcharToDicomTagCast(Vector &source, Vector &result, idx_t count);
bool FromVarcharCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

void GroupScalarFunc(DataChunk &args, ExpressionState &state, Vector &result);
void ElementScalarFunc(DataChunk &args, ExpressionState &state, Vector &result);
void TagNameScalarFunc(DataChunk &args, ExpressionState &state, Vector &result);

void RegisterDicomTypes(ExtensionLoader &loader);

} // namespace duckdb
