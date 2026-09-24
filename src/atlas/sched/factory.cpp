#include "atlas/sched/factory.hpp"

#include <array>

#include "atlas/sched/first_fit.hpp"
#include "atlas/sched/round_robin.hpp"

namespace atlas {
namespace {

// A fixed table rather than a self-registering registry. Self-registration --
// each scheduler defining a static object that adds itself at load time -- is
// the usual textbook answer, and it is the wrong one here: the order of static
// initialization across translation units is unspecified, so a container filled
// that way has an order the standard does not pin down. Determinism is this
// project's headline property, and a registry whose iteration order depends on
// link order is exactly the kind of thing that breaks a golden trace on someone
// else's machine. Four entries in a fixed order costs one line per scheduler.
constexpr std::array<std::string_view, 2> kNames{"first_fit", "round_robin"};

}  // namespace

std::span<const std::string_view> scheduler_names() { return kNames; }

std::unique_ptr<Scheduler> make_scheduler(std::string_view name) {
    if (name == "first_fit") return std::make_unique<FirstFitScheduler>();
    if (name == "round_robin") return std::make_unique<RoundRobinScheduler>();
    return nullptr;
}

}  // namespace atlas
