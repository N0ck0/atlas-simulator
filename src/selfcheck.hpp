#pragma once

namespace atlas {

// Stand in for real unit tests, which arrive with GoogleTest in S2.
// Each returns true on success and prints its own PASS/FAIL line.
bool check_event_queue();
bool check_cluster();
bool check_random_source();
bool check_workload();
bool check_simulator();
bool check_metrics();

}  // namespace atlas
