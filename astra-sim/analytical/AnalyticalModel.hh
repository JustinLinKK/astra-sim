#ifndef ASTRASIM_ANALYTICAL_ANALYTICAL_MODEL_HH
#define ASTRASIM_ANALYTICAL_ANALYTICAL_MODEL_HH

#include <memory>
#include <string>
#include <vector>

namespace AstraSim {

class Sys;

struct AnalyticalContext {
    std::vector<Sys*> systems;
    std::string binary_name;
};

class AnalyticalModel {
  public:
    virtual ~AnalyticalModel() = default;

    virtual void run() = 0;
};

}  // namespace AstraSim

#endif  // ASTRASIM_ANALYTICAL_ANALYTICAL_MODEL_HH
