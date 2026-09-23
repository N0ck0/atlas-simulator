#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "atlas/engine/ids.hpp"
#include "atlas/model/node.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {

// The machines and the resource accounting over them. Owns no jobs; the
// simulator holds those and passes a Resources request in by value.
class Cluster {
public:
    Cluster(std::uint32_t node_count, Resources per_node) {
        nodes_.reserve(node_count);
        for (std::uint32_t i = 0; i < node_count; ++i) {
            nodes_.push_back(Node{NodeId{i}, per_node, per_node, 0});
        }
    }

    // Lowest-index node that can fit `request`, or nullopt if none can.
    // First-fit is the Stage 1 placeholder; S3 replaces it with a pluggable
    // Scheduler interface.
    std::optional<NodeId> first_fit(const Resources& request) const {
        for (const Node& node : nodes_) {
            if (node.can_fit(request)) {
                return node.id;
            }
        }
        return std::nullopt;
    }

    // Deducts `request` from the node's free pool and counts a running job.
    // Precondition: node(id).can_fit(request).
    void allocate(NodeId id, const Resources& request) {
        Node& cur_node = mutable_node(id);
        assert(cur_node.can_fit(request));

        cur_node.free.cores -= request.cores;
        cur_node.free.memory_mb -= request.memory_mb;
        cur_node.running_jobs++;
    }

    // Returns `request` to the node's free pool. Must be passed the same
    // Resources that were allocated, or free drifts away from capacity.
    // Precondition: the node has at least one running job, and the release
    // cannot push any dimension of free past capacity.
    void release(NodeId id, const Resources& request) {
        Node& cur_node = mutable_node(id);
        assert(cur_node.running_jobs > 0);
        assert(request.cores <= cur_node.capacity.cores - cur_node.free.cores);
        assert(request.memory_mb <= cur_node.capacity.memory_mb - cur_node.free.memory_mb);

        cur_node.free.cores += request.cores;
        cur_node.free.memory_mb += request.memory_mb;
        cur_node.running_jobs--;
    }

    const Node& node(NodeId id) const { return nodes_[static_cast<std::uint32_t>(id)]; }

    std::size_t size() const { return nodes_.size(); }

    Resources total_capacity() const {
        Resources sum;
        for (const Node& n : nodes_) {
            sum.cores += n.capacity.cores;
            sum.memory_mb += n.capacity.memory_mb;
        }
        return sum;
    }

    // Summed free resources. Used below as a leak detector, and by the
    // utilization metrics in S5.
    Resources total_free() const {
        Resources free_sum;
        for (const Node& n : nodes_) {
            free_sum.cores += n.free.cores;
            free_sum.memory_mb += n.free.memory_mb;
        }
        return free_sum;
    }

    // Returns resources currently in use (utilized).
    // Derived from total_capacity() and total_free()
    Resources total_utilized() const {
        Resources utilized{};
        const Resources free = total_free();
        const Resources total = total_capacity();
        utilized.cores = total.cores - free.cores;
        utilized.memory_mb = total.memory_mb - free.memory_mb;
        return utilized;
    }

private:
    Node& mutable_node(NodeId id) { return nodes_[static_cast<std::uint32_t>(id)]; }

    std::vector<Node> nodes_;
};

}  // namespace atlas
