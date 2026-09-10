#include "boost/locale/encoding.hpp"
#include "duckdb.hpp" // IWYU pragma: keep
#include "dcmtk2duckdb_logger.hpp"
#include "dcmtk/dcmdata/dcfilefo.h"
#include "dcmtk/dcmdata/dcjson.h"
#include "dicom_read.hpp"
#include "dicom_stream.hpp"
#include "duckdb/common/enums/order_preservation_type.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/storage/external_file_cache/caching_file_system.hpp"

namespace duckdb {

unique_ptr<FunctionData> ReadDicomFuncBind(ClientContext &context, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<Identifier> &names) {
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

	// read options
	for (const auto &kv : input.named_parameters) {
		if (kv.first == "load_pixel_data") {
			result->options.load_pixel_data = BooleanValue::Get(kv.second);
		} else {
			throw InvalidInputException("Unknown input parameter " + kv.first);
		}
	}

	names.push_back("filename");
	return_types.push_back(LogicalType::VARCHAR);

	names.push_back("dicom_content");
	return_types.push_back(LogicalType::JSON());

#ifdef INSPECT_READ_PERFORMANCE
	names.push_back("thread_id");
	return_types.push_back(LogicalType::UBIGINT);

	names.push_back("read_ops");
	return_types.push_back(LogicalType::UINTEGER);

	names.push_back("num_direct_reads");
	return_types.push_back(LogicalType::UINTEGER);

	names.push_back("num_buffer_hits");
	return_types.push_back(LogicalType::UINTEGER);

	names.push_back("num_buffer_reloads");
	return_types.push_back(LogicalType::UINTEGER);
#endif

	return std::move(result);
}

unique_ptr<GlobalTableFunctionState> ReadDicomGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	Value batch_size_val;
	size_t batch_size = (context.TryGetCurrentSetting(Identifier(READ_DICOM_BATCH_SIZE_SETTING), batch_size_val)
	                         ? batch_size_val.GetValue<uint64_t>()
	                         : DEFAULT_BATCH_SIZE);
	auto &bind_data = input.bind_data->Cast<ReadDicomBindData>();
	return make_uniq<ReadDicomGlobalState>(bind_data.files.size(), batch_size);
}

unique_ptr<LocalTableFunctionState> ReadDicomLocalInit(ExecutionContext &context, TableFunctionInitInput &input,
                                                       GlobalTableFunctionState *global_state_p) {
	Value internal_buffer_val;
	size_t internal_buffer_size =
	    (context.client.TryGetCurrentSetting(Identifier(READ_DICOM_INTERNAL_BUFFER_SIZE_SETTING), internal_buffer_val)
	         ? internal_buffer_val.GetValue<uint32_t>()
	         : DEFAULT_BUFFER_SIZE);
	return make_uniq<ReadDicomLocalState>(internal_buffer_size);
}

void ReadDicomFunc(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &global_state = data.global_state->Cast<ReadDicomGlobalState>();
	auto &local_state = data.local_state->Cast<ReadDicomLocalState>();
	auto &bind_data = data.bind_data->Cast<ReadDicomBindData>();

	string dicom_logtype = "dicom";

	idx_t start_idx, end_idx;
	if (!global_state.GetWorkItem(start_idx, end_idx)) {
		output.SetChildCardinality(0);
		return;
	}

	auto &read_options = bind_data.options;

	idx_t count = end_idx - start_idx;
	auto &path_vector = output.data[0];
	auto &content_vector = output.data[1];

	auto path_data = FlatVector::GetDataMutable<string_t>(path_vector);
	auto content_data = FlatVector::GetDataMutable<string_t>(content_vector);

#ifdef INSPECT_READ_PERFORMANCE
	auto &read_ops_vector = output.data[3];
	auto &num_direct_reads_vector = output.data[4];
	auto &num_buffer_hits_vector = output.data[5];
	auto &num_buffer_reloads_vector = output.data[6];

	auto read_ops_data = FlatVector::GetDataMutable<uint32_t>(read_ops_vector);
	auto num_direct_reads_data = FlatVector::GetDataMutable<uint32_t>(num_direct_reads_vector);
	auto num_buffer_hits_data = FlatVector::GetDataMutable<uint32_t>(num_buffer_hits_vector);
	auto num_buffer_reloads_data = FlatVector::GetDataMutable<uint32_t>(num_buffer_reloads_vector);
#endif

	thread_local std::ostringstream jsonStream;

	auto fs = CachingFileSystem::Get(context);

	idx_t actual_count = 0;
	for (idx_t i = 0; i < count; i++) {
		idx_t file_index = start_idx + i;

		// path column
		auto file_path = bind_data.files[file_index].path;
		path_data[i] = StringVector::AddString(path_vector, file_path);

		// dicom_content column
		DuckDBDicomInputFileStream stream(fs, file_path, local_state.buffer);

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

			string dicom_content;

			// unsupported SpecificCharacterSet are converted to UTF-8 using boost
			const char *char_set_ptr = nullptr;
			OFCondition find_char_set = dataset->findAndGetString(DcmTagKey(0x0008, 0x0005), char_set_ptr);
			if (find_char_set.good()) {
				string char_set(char_set_ptr);
				if (char_set == "ISO 2022 IR 100") {
					dicom_content = boost::locale::conv::to_utf<char>(jsonStream.str(), "Latin1");
				} else {
					dicom_content = jsonStream.str();
				}
			} else {
				dicom_content = jsonStream.str();
			}

			content_data[i] = StringVector::AddString(content_vector, "{" + dicom_content + "}");
		} else {
			auto &logger = Logger::Get(context);
			logger.WriteLog(dicom_logtype.c_str(), LogLevel::LOG_WARNING, "Could not read file " + file_path);
			FlatVector::SetNull(content_vector, i, true);
		}

#ifdef INSPECT_READ_PERFORMANCE
		stream.PopulateReadPerformanceMetrics();
		read_ops_data[i] = stream.num_reads_ops_;
		num_direct_reads_data[i] = stream.num_direct_reads_;
		num_buffer_hits_data[i] = stream.num_buf_reads_;
		num_buffer_reloads_data[i] = stream.num_buf_reloads_;
#endif
		actual_count += 1;
	}

#ifdef INSPECT_READ_PERFORMANCE
	auto &thread_id_vector = output.data[2];
	thread_id_vector.Reference(Value::UBIGINT(local_state.thread_id), count_t(actual_count));
#endif

	output.SetChildCardinality(actual_count);
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
	TableFunction read_dicom_func(Identifier("read_dicom"), {LogicalType::VARCHAR}, ReadDicomFunc, ReadDicomFuncBind,
	                              ReadDicomGlobalInit, ReadDicomLocalInit);
	read_dicom_func.named_parameters["load_pixel_data"] = LogicalType::BOOLEAN;
	read_dicom_func.order_preservation_type = OrderPreservationType::NO_ORDER;

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

	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	config.AddExtensionOption(Identifier(READ_DICOM_BATCH_SIZE_SETTING),
	                          "Number of files to read in each batch of read_dicom.", LogicalType::UINTEGER,
	                          DEFAULT_BATCH_SIZE, nullptr, SetScope::SESSION);
	config.AddExtensionOption(Identifier(READ_DICOM_INTERNAL_BUFFER_SIZE_SETTING),
	                          "The size of the internal buffer (in kB) used to cache remote read when calling "
	                          "read_dicom over remote storage.",
	                          LogicalType::UINTEGER, DEFAULT_BUFFER_SIZE, nullptr, SetScope::SESSION);
}

} // namespace duckdb
