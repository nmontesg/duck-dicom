#pragma once

#include "duckdb.hpp" // IWYU pragma: keep
#include "dcmtk/dcmdata/dcistrma.h"
#include "dcmtk/ofstd/offile.h"

namespace duckdb {

class DuckDBDicomProducer : public DcmProducer {
public:
	DuckDBDicomProducer(FileSystem &fs, const string &fp)
	    : handle(fs.OpenFile(fp, FileOpenFlags::FILE_FLAGS_READ)), file_size(0), location(0) {
		if (handle) {
			file_size = handle->GetFileSize();
		}
	}

	~DuckDBDicomProducer() override {
		if (handle) {
			handle->Close();
		}
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
		handle->Read(buf, buflen, location);
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
	unique_ptr<FileHandle> handle;
	idx_t file_size;
	idx_t location;
};

class DuckDBDicomInputFileStream : public DcmInputStream {
public:
	DuckDBDicomInputFileStream(FileSystem &fs, const string &fp) : DcmInputStream(&producer_), producer_(fs, fp) {};
	~DuckDBDicomInputFileStream() override {};

	DcmInputStreamFactory *newFactory() const override {
		return nullptr;
	}

private:
	DuckDBDicomProducer producer_;
};

} // namespace duckdb
