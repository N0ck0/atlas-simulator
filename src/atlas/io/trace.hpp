#pragma once

#include <cstdio>
#include <string>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/event.hpp"
#include "atlas/engine/ids.hpp"
#include "atlas/io/options.hpp"
#include "atlas/model/job.hpp"

namespace atlas {

// JSONL event trace: one JSON object per line, so a run is diffable and can be
// processed a record at a time rather than parsed whole. Writes nothing when
// Options::trace_path is null.
//
// The trace records observed facts, never derived ones: placement is logged
// where it happens rather than reconstructed as finish minus duration, so a
// golden trace can catch a placement bug instead of restating the assumption
// under test. Timestamps stay integer ticks for the same reason.
//
// Output is block-buffered, so the file is only complete once the writer is
// destroyed; scope it accordingly if something else is going to read it.
class TraceWriter {
public:
    explicit TraceWriter(const Options& opt);
    ~TraceWriter();
    TraceWriter(const TraceWriter&) = delete;
    TraceWriter& operator=(const TraceWriter&) = delete;

    void write(const Event& e);

    // A job's static properties, logged once before the clock starts so that
    // analysis can group by job size without replaying the seed.
    void job_spec(const Job& j);

    // Placement is not an event: drain() assigns a job to a node as a side
    // effect of handling an arrival or a finish, so nothing reaches write().
    // Logging it here is what lets the trace separate queue wait from service
    // time. These records carry no seq -- they never entered the queue -- so
    // file order, not the seq field, is what orders a tick's lines.
    void placement(Tick t, JobId job, NodeId node);

private:
    static std::string to_jsonl(const Event& e);
    void write_line(const std::string& line);

    std::FILE* out_ = nullptr;
};

}  // namespace atlas
