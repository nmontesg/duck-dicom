#include "boost/algorithm/string/classification.hpp"
#include "boost/algorithm/string/split.hpp"
#include "duckdb.hpp" // IWYU pragma: keep
#include "dcmtk/dcmdata/dcdatset.h"
#include "dcmtk/dcmdata/dcelem.h"
#include "dicom_utils.hpp"

namespace duckdb {

void DuckDBDicomUtils::ParseQueryMatchKeys(const Value &input_query, QueryDicomBindData &bind_data) {
	if (input_query.IsNull()) {
		return;
	}
	const auto &map_elements = ListValue::GetChildren(input_query);
	for (const auto &element : map_elements) {
		if (element.IsNull()) {
			continue;
		}
		const vector<Value> &struct_children = StructValue::GetChildren(element);

		string key = struct_children[0].ToString();
		string val = struct_children[1].ToString();

		const string overrideKey = key + "=" + val;
		bind_data.query.push_back(overrideKey.c_str());
	}
}

void DuckDBDicomUtils::ParseQueryRetrieveKeys(const Value &input_query, QueryDicomBindData &bind_data) {
	if (input_query.IsNull()) {
		return;
	}
	const auto &map_elements = ListValue::GetChildren(input_query);
	for (const auto &element : map_elements) {
		if (element.IsNull()) {
			continue;
		}
		const string retrieveKey = StringValue::Get(element);
		bind_data.query.push_back(retrieveKey.c_str());
	}
}

void DuckDBDicomUtils::ParseDicomDataset(const string &qr_level, const string &uid, DcmDataset *dset) {
	DcmTag tag;

	vector<string> qr_levels = DuckDBDicomUtils::GetQueryRetrieveLevels();

	if (qr_level == qr_levels[0]) {        // patient
		tag = DcmTag(0x0010, 0x0020);      // PatientID
	} else if (qr_level == qr_levels[1]) { // study
		tag = DcmTag(0x0020, 0x000d);      // StudyInstanceUID
	} else if (qr_level == qr_levels[2]) { // series
		tag = DcmTag(0x0020, 0x000e);      // SeriesInstanceUID
	} else if (qr_level == qr_levels[3]) { // image
		tag = DcmTag(0x0008, 0x0018);      // SOPInstanceUID
	} else {
		throw InvalidInputException("Unknow Query/Retrieve Level " + qr_level);
	}
	DcmElement *elem = DcmItem::newDicomElement(DcmTag(tag));
	if (elem == nullptr) {
		throw InvalidInputException("Cannot create element for tag (%04x,%04x)", tag.getGroup(), tag.getElement());
	}
	if (elem->putString(uid.c_str()).bad()) {
		throw InvalidInputException("Cannot put tag value in element (%04x,%04x)=%s", tag.getGroup(), tag.getElement(),
		                            uid);
	}
	OFCondition cond = dset->insert(elem, OFTrue);
	if (cond.bad()) {
		throw InvalidInputException("Cannot add DICOM element (%04x,%04x)=%s to query dataset", tag.getGroup(),
		                            tag.getElement(), uid);
	}
}

vector<string> DuckDBDicomUtils::GetCharacterSets(DcmDataset *ds) {
	vector<string> all_charsets;

	const char *char_set_ptr = nullptr;
	OFCondition find_char_set = ds->findAndGetString(DcmTagKey(0x0008, 0x0005), char_set_ptr);

	if (find_char_set.good()) {
		string char_set_value(char_set_ptr);

		vector<string> all_charsets_raw;
		boost::split(all_charsets_raw, char_set_value, boost::is_any_of("\\"));

		for (string &enc : all_charsets_raw) {
			if (!enc.empty()) {
				all_charsets.push_back(enc);
			}
		}
	}

	return all_charsets;
}

} // namespace duckdb
