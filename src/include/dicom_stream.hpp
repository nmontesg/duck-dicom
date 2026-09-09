#pragma once

#include "duckdb.hpp" // IWYU pragma: keep
#include "dcmtk/dcmdata/dcistrma.h"
#include "dcmtk/ofstd/offile.h"
#include "duckdb/storage/external_file_cache/caching_file_system.hpp"

#define DEFAULT_BUFFER_SIZE 128

namespace duckdb {

class DuckDBDicomProducer : public DcmProducer {
public:
	DuckDBDicomProducer(CachingFileSystem &fs, const string &fp)
	    : handle(fs.OpenFile(fp, FileOpenFlags::FILE_FLAGS_READ)), file_size(0), location(0), buffer_start(0),
	      buffer_end(0) {
		if (handle) {
			file_size = handle->GetFileSize();
		}
	}

	~DuckDBDicomProducer() override {
	}

	OFBool good() const override {
		return handle != nullptr;
	}

	OFCondition status() const override {
		return handle ? EC_Normal : EC_IllegalParameter;
	}

	OFBool eos() override {
		return (location >= file_size);
	}

	offile_off_t avail() override {
		return (location >= file_size ? 0 : static_cast<offile_off_t>(file_size - location));
	}

	offile_off_t read(void *buf, offile_off_t buflen) override {
		if (!good() || eos() || buflen <= 0) {
			return 0;
		}
		if (buflen > avail()) {
			buflen = avail();
		}

		// request bigger than internal buffer reads directly from file handler
		if (buflen > INTERNAL_BUFFER_SIZE) {
			FileBufferHandleGroup group = handle->Read(buflen, location);
			group.CopyTo(data_ptr_cast<void>(buf), buflen);
			location += buflen;
			return buflen;
		}

		// internal buffer fully covers the request
		if (location >= buffer_start && location + buflen <= buffer_end) {
			memcpy(buf, internal_buffer + (location - buffer_start), buflen);
			location += buflen;
			return buflen;
		}

		// populate the buffer to serve this request (and hopefully following ones)
		size_t fetch_len = std::min<size_t>(INTERNAL_BUFFER_SIZE, file_size - location);
		FileBufferHandleGroup group = handle->Read(fetch_len, location);
		group.CopyTo(data_ptr_cast<void>(internal_buffer), fetch_len);
		buffer_start = location;
		buffer_end = buffer_start + fetch_len;

		memcpy(buf, internal_buffer, buflen);
		location += buflen;
		return buflen;
	}

	offile_off_t skip(offile_off_t skiplen) override {
		if (!good() || eos() || skiplen <= 0) {
			return 0;
		}
		if (skiplen > avail()) {
			skiplen = avail();
		}
		location += skiplen;
		return skiplen;
	}

	void putback(offile_off_t num) override {
		offile_off_t location_long = static_cast<offile_off_t>(location);
		location = (num > location_long ? 0 : location_long - num);
	}

private:
	unique_ptr<CachingFileHandle> handle;
	idx_t file_size;
	idx_t location;

	// TODO make the internal buffer size a setting
	static constexpr size_t INTERNAL_BUFFER_SIZE = size_t(128 * 1024);
	unsigned char internal_buffer[INTERNAL_BUFFER_SIZE];
	idx_t buffer_start;
	idx_t buffer_end;
};

class DuckDBDicomInputFileStream : public DcmInputStream {
public:
	DuckDBDicomInputFileStream(CachingFileSystem &fs, const string &fp)
	    : DcmInputStream(&producer_), producer_(fs, fp) {};
	~DuckDBDicomInputFileStream() override {};

	DcmInputStreamFactory *newFactory() const override {
		return nullptr;
	}

private:
	DuckDBDicomProducer producer_;
};

} // namespace duckdb
