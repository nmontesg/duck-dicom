#include "dicom_types.hpp"
#include "dcmtk/dcmdata/dctag.h"
#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

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

void GroupScalarFunc(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &tags_vector = StructVector::GetEntries(args.data[0]);
	auto &group_vector = *tags_vector[0];

	UnaryExecutor::Execute<uint16_t, string_t>(group_vector, result, args.size(), [&](uint16_t group) {
		char buf[5];
		snprintf(buf, sizeof(buf), "%04X", group);
		return StringVector::AddString(result, buf, 4);
	});
}

void ElementScalarFunc(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &tags_vector = StructVector::GetEntries(args.data[0]);
	auto &elem_vector = *tags_vector[1];

	UnaryExecutor::Execute<uint16_t, string_t>(elem_vector, result, args.size(), [&](uint16_t elem) {
		char buf[5];
		snprintf(buf, sizeof(buf), "%04X", elem);
		return StringVector::AddString(result, buf, 4);
	});
}

void TagNameScalarFunc(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &tags_vector = StructVector::GetEntries(args.data[0]);
	auto &group_vector = *tags_vector[0];
	auto &elem_vector = *tags_vector[1];

	BinaryExecutor::Execute<uint16_t, uint16_t, string_t>(
	    group_vector, elem_vector, result, args.size(), [&](uint16_t group, uint16_t elem) {
		    DcmTag dcmtk_tag = DcmTag(group, elem);
		    const char *tag_name = dcmtk_tag.getTagName();
		    if (StringUtil::StartsWith(tag_name, "Unknown Tag & Data")) {
			    char buf[12];
			    snprintf(buf, sizeof(buf), "%04X,%04X", group, elem);
			    return StringVector::AddString(result, buf);
		    } else {
			    return StringVector::AddString(result, tag_name);
		    }
	    });
}

void RegisterDicomTypes(ExtensionLoader &loader) {
	loader.RegisterType("DICOM_TAG", DICOM_TAG());

	loader.RegisterCastFunction(DICOM_TAG(), LogicalType::VARCHAR, BoundCastInfo(ToVarcharCast), 1);
	loader.RegisterCastFunction(LogicalType::VARCHAR, DICOM_TAG(), BoundCastInfo(FromVarcharCast), 1);

	ScalarFunction group_func("tag_group", {DICOM_TAG()}, LogicalType::VARCHAR, GroupScalarFunc);
	CreateScalarFunctionInfo group_info(group_func);
	FunctionDescription group_desc;
	group_desc.description =
	    "Extracts the group of a DICOM tag, as a VARCHAR containing the group in hexadecimal representation.";
	group_desc.examples = {"SELECT tag_group('0008,01DA')"};
	group_desc.categories = {"medical"};
	group_info.descriptions.push_back(group_desc);
	loader.RegisterFunction(group_func);

	ScalarFunction elem_func("tag_element", {DICOM_TAG()}, LogicalType::VARCHAR, ElementScalarFunc);
	CreateScalarFunctionInfo elem_info(elem_func);
	FunctionDescription elem_desc;
	elem_desc.description =
	    "Extracts the element of a DICOM tag, as a VARCHAR containing the element in hexadecimal representation.";
	elem_desc.examples = {"SELECT tag_element('0008,01DA')"};
	elem_desc.categories = {"medical"};
	elem_info.descriptions.push_back(elem_desc);
	loader.RegisterFunction(elem_func);

	ScalarFunction name_func("tag_name", {DICOM_TAG()}, LogicalType::VARCHAR, TagNameScalarFunc);
	CreateScalarFunctionInfo name_info(name_func);
	FunctionDescription name_desc;
	name_desc.description =
	    "Extracts the name of the DICOM tag. If it can't be found, a VARCHAR with format GGGG,EEEE is returned.";
	name_desc.examples = {"SELECT tag_name('0008,0008')"};
	name_desc.categories = {"medical"};
	name_info.descriptions.push_back(name_desc);
	loader.RegisterFunction(name_func);
}

} // namespace duckdb
