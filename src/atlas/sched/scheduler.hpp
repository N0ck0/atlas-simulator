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

    // Non-copyable. Schedulers are owned by unique_ptr and used through this
    // interface, so copying one by value would slice it to the base and lose
    // whatever state the policy keeps -- RoundRobin's cursor, for instance.
    // Deleting here rather than in each implementation means every scheduler
    // inherits the guarantee, including the ones not written yet.
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    virtual std::optional<NodeId> place(const Job&, std::span<const Node>) = 0;
    virtual std::string_view name() const = 0;

protected:
    // Declaring the copy operations above suppresses the implicit default
    // constructor, which derived classes still need. Protected because only a
    // derived class has any business calling it.
    Scheduler() = default;
};
}  // namespace atlas