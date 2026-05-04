#ifndef ASTRASIM_ANALYTICAL_ANALYTICAL_RESULT_HH
#define ASTRASIM_ANALYTICAL_ANALYTICAL_RESULT_HH

#include <string>
#include <vector>

#include <json/json.hpp>

namespace AstraSim {

struct AnalyticalTable {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;

    void add_row(std::vector<std::string> row);
};

class AnalyticalResultWriter {
  public:
    static void write_csv(const AnalyticalTable& table, const std::string& path);
    static void write_json(const nlohmann::json& value,
                           const std::string& path);
};

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_ANALYTICAL_RESULT_HH
