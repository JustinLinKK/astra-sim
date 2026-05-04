#include "astra-sim/analytical/AnalyticalResult.hh"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace AstraSim {

namespace {

bool should_write(const std::string& path) {
    return !path.empty() && path != "empty";
}

void ensure_parent_directory(const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

}  // namespace

void AnalyticalTable::add_row(std::vector<std::string> row) {
    if (columns.size() != row.size()) {
        throw std::runtime_error(
            "Analytical table row does not match the declared column count");
    }
    rows.push_back(std::move(row));
}

void AnalyticalResultWriter::write_csv(const AnalyticalTable& table,
                                       const std::string& path) {
    if (!should_write(path)) {
        return;
    }
    ensure_parent_directory(path);

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to open analytical CSV output: " + path);
    }

    for (size_t index = 0; index < table.columns.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << table.columns[index];
    }
    output << "\n";

    for (const auto& row : table.rows) {
        for (size_t index = 0; index < row.size(); ++index) {
            if (index > 0) {
                output << ",";
            }
            output << row[index];
        }
        output << "\n";
    }
}

void AnalyticalResultWriter::write_json(const nlohmann::json& value,
                                        const std::string& path) {
    if (!should_write(path)) {
        return;
    }
    ensure_parent_directory(path);

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to open analytical JSON output: " + path);
    }

    output << value.dump(2) << "\n";
}

}  // namespace AstraSim
