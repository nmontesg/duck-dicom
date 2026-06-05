#include "dicom_types.hpp"
#include "duckdb.hpp"

namespace duckdb {

LogicalType DICOM_TAG() {
	auto type = LogicalType::STRUCT(
	    {{"group", LogicalType(LogicalTypeId::USMALLINT)}, {"elem", LogicalType(LogicalTypeId::USMALLINT)}});
	type.SetAlias("DICOM_TAG");
	return type;
}

void DicomTagToVarchar(Vector &source, Vector &result, idx_t count) {
	auto &source_children = StructVector::GetEntries(source);
	auto &group_vector = *source_children[0];
	auto &elem_vector = *source_children[1];

	BinaryExecutor::Execute<uint16_t, uint16_t, string_t>(group_vector, elem_vector, result, count,
	                                                      [&](uint16_t group, uint16_t elem) {
		                                                      char buf[12];
		                                                      snprintf(buf, sizeof(buf), "%04X,%04X", group, elem);
		                                                      return StringVector::AddString(result, buf);
	                                                      });
}

bool ToVarcharCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	DicomTagToVarchar(source, result, count);
	return true;
}

void VarcharToDicomTagCast(Vector &source, Vector &result, idx_t count) {
	auto &children = StructVector::GetEntries(result);
	auto &group_vector = *children[0];
	auto &elem_vector = *children[1];

	UnifiedVectorFormat source_format;
	source.ToUnifiedFormat(count, source_format);
	auto source_data = UnifiedVectorFormat::GetData<string_t>(source_format);

	group_vector.SetVectorType(VectorType::FLAT_VECTOR);
	elem_vector.SetVectorType(VectorType::FLAT_VECTOR);
	auto group_data = FlatVector::GetData<uint16_t>(group_vector);
	auto elem_data = FlatVector::GetData<uint16_t>(elem_vector);

	auto &result_validity = FlatVector::Validity(result);
	auto &group_validity = FlatVector::Validity(group_vector);
	auto &elem_validity = FlatVector::Validity(elem_vector);

	for (idx_t i = 0; i < count; i++) {
		idx_t source_idx = source_format.sel->get_index(i);

		if (!source_format.validity.RowIsValid(source_idx)) {
			result_validity.SetInvalid(i);
			group_validity.SetInvalid(i);
			elem_validity.SetInvalid(i);
			continue;
		}

		string_t input_str = source_data[source_idx];
		const char *data = input_str.GetData();
		idx_t len = input_str.GetSize();

		char *endptr;
		uint16_t group_val = 0;
		uint16_t elem_val = 0;

		if (len >= 3 && (data[4] == ',' || data[2] == ',' || data[1] == ',' || data[3] == ',')) {
			// Case 1: Comma separated format (e.g., "0020,000D" or "20,D")
			group_val = static_cast<uint16_t>(std::strtoul(data, &endptr, 16));
			if (*endptr != ',') {
				throw InvalidInputException("Invalid DICOM_TAG group delimiter.");
			}
			if ((endptr - data) > 4) {
				throw InvalidInputException(
				    "Invalid DICOM_TAG format: Group hex string cannot be longer than 4 characters.");
			}

			const char *elem_start = endptr + 1;
			elem_val = static_cast<uint16_t>(std::strtoul(elem_start, &endptr, 16));
			if ((endptr - elem_start) > 4) {
				throw InvalidInputException(
				    "Invalid DICOM_TAG format: Element hex string cannot be longer than 4 characters.");
			}
			if (endptr != data + len) {
				throw InvalidInputException("Invalid DICOM_TAG format: Trailing characters found after element.");
			}
		} else if (len == 8) {
			// Case 2: Strict 8-character format (e.g., "0020000D")
			char buf[5];

			buf[0] = data[0];
			buf[1] = data[1];
			buf[2] = data[2];
			buf[3] = data[3];
			buf[4] = '\0';
			group_val = static_cast<uint16_t>(std::strtoul(buf, nullptr, 16));

			buf[0] = data[4];
			buf[1] = data[5];
			buf[2] = data[6];
			buf[3] = data[7];
			buf[4] = '\0';
			elem_val = static_cast<uint16_t>(std::strtoul(buf, nullptr, 16));
		} else {
			throw InvalidInputException("Invalid DICOM_TAG format length.");
		}

		group_data[i] = group_val;
		elem_data[i] = elem_val;
	}

	if (source.GetVectorType() == VectorType::CONSTANT_VECTOR) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
		group_vector.SetVectorType(VectorType::CONSTANT_VECTOR);
		elem_vector.SetVectorType(VectorType::CONSTANT_VECTOR);
	} else {
		result.SetVectorType(VectorType::FLAT_VECTOR);
	}
}

bool FromVarcharCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	VarcharToDicomTagCast(source, result, count);
	return true;
}

// TODO implement extraction of the group and element as scalar functions: group(tag_column), element(tag_column)

void RegisterDicomTypes(ExtensionLoader &loader) {
	loader.RegisterType("DICOM_TAG", DICOM_TAG());

	loader.RegisterCastFunction(DICOM_TAG(), LogicalType::VARCHAR, BoundCastInfo(ToVarcharCast), 1);
	loader.RegisterCastFunction(LogicalType::VARCHAR, DICOM_TAG(), BoundCastInfo(FromVarcharCast), 1);
}

} // namespace duckdb
