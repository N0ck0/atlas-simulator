#include "atlas/io/trace.hpp"

#include <cstdint>
#include <cstdio>
#include <format>
#include <type_traits>
#include <variant>

namespace atlas {

std::string TraceWriter::to_jsonl(const Event& e) {
    std::string line = std::format(R"({{"t":{},"seq":{})", e.time, e.seq);

    line += std::visit(
        [](const auto& p) -> std::string {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, JobArrival>) {
                return std::format(R"(,"ev":"JobArrival","job":{})",
                                   static_cast<std::uint32_t>(p.job));
            } else if constexpr (std::is_same_v<T, JobFinish>) {
                return std::format(R"(,"ev":"JobFinish","job":{},"node":{})",
                                   static_cast<std::uint32_t>(p.job),
                                   static_cast<std::uint32_t>(p.node));
            } else if constexpr (std::is_same_v<T, SimEnd>) {
                return R"(,"ev":"SimEnd")";
            } else {
                static_assert(kAlwaysFalse<T>, "Unhandled event type in trace writer");
            }
        },
        e.payload);

    line += "}";
    return line;
}

TraceWriter::TraceWriter(const Options& opt) {
    const char* path = opt.trace_path;
    if (path == nullptr) return;
    out_ = std::fopen(path, "w");
    if (out_ == nullptr) {
        std::perror(path);
        return;
    }
    write_line(std::format(
        R"({{"ev":"header","seed":{},"nodes":{},"jobs":{},"arrival_rate":{},"scheduler":"{}","queue":"{}","padding":{}}})",
        opt.seed, opt.nodes, opt.jobs, opt.arrival_rate, opt.scheduler, opt.queue,
        opt.estimate_padding));
}

// Every record goes through here, so the one-object-per-line invariant is
// enforced in a single place rather than at each call site.
void TraceWriter::write_line(const std::string& line) {
    if (out_ == nullptr) return;
    std::fputs(line.c_str(), out_);
    std::fputs("\n", out_);
}

TraceWriter::~TraceWriter() {
    if (out_ != nullptr) std::fclose(out_);
}

void TraceWriter::write(const Event& e) {
    if (out_ == nullptr) return;
    write_line(to_jsonl(e));
}

void TraceWriter::job_spec(const Job& j) {
    if (out_ == nullptr) return;
    write_line(std::format(
        R"({{"ev":"job","job":{},"submit":{},"dur":{},"est":{},"cores":{},"mem_mb":{}}})",
        static_cast<std::uint32_t>(j.id), j.submit_time, j.duration, j.estimated_duration,
        j.request.cores, j.request.memory_mb));
}

void TraceWriter::placement(Tick t, JobId job, NodeId node) {
    if (out_ == nullptr) return;
    write_line(std::format(R"({{"t":{},"ev":"JobStart","job":{},"node":{}}})", t,
                           static_cast<std::uint32_t>(job), static_cast<std::uint32_t>(node)));
}

}  // namespace atlas
