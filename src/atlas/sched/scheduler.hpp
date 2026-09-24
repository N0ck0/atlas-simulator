#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "atlas/engine/ids.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/node.hpp"

namespace atlas {
class Scheduler {
public:
    virtual ~Scheduler() = default;
    virtual std::optional<NodeId> place(const Job&, std::span<const Node>) = 0;
    virtual std::string_view name() const = 0;
};
}  // namespace atlas