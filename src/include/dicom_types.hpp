#include "duckdb.hpp"

namespace duckdb {

LogicalType DICOM_TAG();

void DicomTagToVarchar(Vector &, Vector &, idx_t);
bool ToVarcharCast(Vector &, Vector &, idx_t, CastParameters &);

void VarcharToDicomTagCast(Vector &, Vector &, idx_t);
bool FromVarcharCast(Vector &, Vector &, idx_t, CastParameters &);

void GroupScalarFunc(DataChunk &, ExpressionState &, Vector &);
void ElementScalarFunc(DataChunk &, ExpressionState &, Vector &);
void TagNameScalarFunc(DataChunk &, ExpressionState &, Vector &);

void RegisterDicomTypes(ExtensionLoader &);

} // namespace duckdb
