#include "duckdb.hpp" // IWYU pragma: keep
#include "dcmtk2duckdb_logger.hpp"
#include "dcmtk/dcmdata/dcfilefo.h"
#include "dcmtk/dcmdata/dcjson.h"
#include "dicom_read.hpp"
#include "dicom_stream.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

unique_ptr<FunctionData> ReadDicomFuncBind(ClientContext &context, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
	if (input.inputs.empty()) {
		throw InvalidInputException("read_dicom requires at least one argument.");
	}

	RedirectDCMTKLogsToDuckDB(context);

	auto &path_param = input.inputs[0];
	string glob_pattern = StringValue::Get(path_param);
	auto &fs = FileSystem::GetFileSystem(context);
	auto file_list = fs.Glob(glob_pattern);
	if (file_list.empty()) {
		throw IOException("No DICOM files found matching the provided pattern.");
	}

	auto result = make_uniq<ReadDicomBindData>();
	result->files = std::move(file_list);

	// parse options
	for (const auto &kv : input.named_parameters) {
		if (kv.first == "load_pixel_data") {
			result->options.load_pixel_data = BooleanValue::Get(kv.second);
		} else {
			throw InvalidInputException("Unknown input parameter " + StringUtil::Lower(kv.first));
		}
	}

	names.push_back("path");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("dicom_content");
	return_types.push_back(LogicalType::JSON());

	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> ReadDicomGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<ReadDicomBindData>();
	return make_uniq<ReadDicomGlobalState>(bind_data.files.size());
}

unique_ptr<LocalTableFunctionState> ReadDicomLocalInit(ExecutionContext &context, TableFunctionInitInput &input,
                                                       GlobalTableFunctionState *global_state_p) {
	return make_uniq<ReadDicomLocalState>();
}

void ReadDicomFunc(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &global_state = data.global_state->Cast<ReadDicomGlobalState>();
	auto &bind_data = data.bind_data->Cast<ReadDicomBindData>();

	string dicom_logtype = "dicom";

	idx_t start_idx, end_idx;
	if (!global_state.GetWorkItem(start_idx, end_idx)) {
		output.SetCardinality(0);
		return;
	}

	auto &read_options = bind_data.options;

	idx_t count = end_idx - start_idx;
	auto &path_vector = output.data[0];
	auto &content_vector = output.data[1];

	auto path_data = FlatVector::GetData<string_t>(path_vector);
	auto content_data = FlatVector::GetData<string_t>(content_vector);

	thread_local std::ostringstream jsonStream;

	auto &fs = FileSystem::GetFileSystem(context);

	idx_t actual_count = 0;
	for (idx_t i = 0; i < count; i++) {
		idx_t file_index = start_idx + i;

		// path column
		auto file_path = bind_data.files[file_index].path;
		path_data[i] = StringVector::AddString(path_vector, file_path);

		// dicom_content column
		DuckDBDicomInputFileStream stream(fs, file_path);

		DcmFileFormat fileformat;
		OFCondition status;
		if (!read_options.load_pixel_data) {
			status = fileformat.readUntilTag(stream, EXS_Unknown, EGL_noChange, DCM_MaxReadLength,
			                                 DcmTagKey(0x7FE0, 0x0010));
		} else {
			status = fileformat.read(stream);
		}

		if (status.good()) {
			DcmDataset *dataset = fileformat.getDataset();
			jsonStream.str("");
			jsonStream.clear();

			DcmJsonFormatCompact format;
			dataset->writeJson(jsonStream, format);

			content_data[i] = StringVector::AddString(content_vector, "{" + jsonStream.str() + "}");
		} else {
			auto &logger = Logger::Get(context);
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_WARNING, "Could not read file " + file_path);
			FlatVector::SetNull(content_vector, i, true);
		}
		actual_count += 1;
	}

	output.SetCardinality(actual_count);
}

unique_ptr<NodeStatistics> ReadDicomCardinality(ClientContext &context, const FunctionData *bind_data_p) {
	auto &bind_data = bind_data_p->Cast<ReadDicomBindData>();
	return make_uniq<NodeStatistics>(static_cast<idx_t>(bind_data.files.size()));
}

double ReadDicomProgress(ClientContext &context, const FunctionData *bind_data_p,
                         const GlobalTableFunctionState *global_state_p) {
	auto &bind_data = bind_data_p->Cast<ReadDicomBindData>();
	auto &global_state = global_state_p->Cast<ReadDicomGlobalState>();

	if (bind_data.files.empty()) {
		return 100.0;
	}
	idx_t files_processed = bind_data.files.size() - global_state.num_files_left_to_read;
	return 100.0 * static_cast<double>(files_processed) / static_cast<double>(bind_data.files.size());
}

void RegisterDicomRead(ExtensionLoader &loader) {
	TableFunction read_dicom_func("read_dicom", {LogicalType::VARCHAR}, ReadDicomFunc, ReadDicomFuncBind,
	                              ReadDicomGlobalInit, ReadDicomLocalInit);
	read_dicom_func.named_parameters["load_pixel_data"] = LogicalType::BOOLEAN;

	CreateTableFunctionInfo read_dicom_info(read_dicom_func);
	FunctionDescription read_dicom_desc;
	read_dicom_desc.parameter_types = {LogicalType::VARCHAR, LogicalType::BOOLEAN};
	read_dicom_desc.parameter_names = {"path", "load_pixel_data"};
	read_dicom_desc.description =
	    "Load DICOM files into DuckDB. The contents of each file are loaded into a row in JSON format.";
	read_dicom_desc.examples = {"SELECT * FROM read_dicom('/path/to/dicom_files/*');",
	                            "SELECT * FROM read_dicom('/path/to/dicom_files/*', load_pixel_data = true);"};
	read_dicom_desc.categories = {"medical"};
	read_dicom_info.descriptions.push_back(read_dicom_desc);

	read_dicom_func.cardinality = ReadDicomCardinality;
	read_dicom_func.table_scan_progress = ReadDicomProgress;

	loader.RegisterFunction(read_dicom_info);
}

} // namespace duckdb
