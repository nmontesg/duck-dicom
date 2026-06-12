#pragma once

#include "duckdb.hpp"

namespace duckdb {

class DicomExtension : public Extension {
public:
	void Load(ExtensionLoader &) override;
	std::string Name() override;
	std::string Version() const override;
};

} // namespace duckdb
