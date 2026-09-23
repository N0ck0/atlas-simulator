#pragma once

#include <cstdint>

namespace atlas {

// Distinct types, layout-identical to uint32_t, so a JobId cannot be passed
// where a NodeId is expected. Construct with JobId{7}.
enum class JobId : std::uint32_t {};
enum class NodeId : std::uint32_t {};

}  // namespace atlas
