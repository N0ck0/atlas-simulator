// The factory is the seam between a string on the command line and a policy
// object, so the thing worth testing is that the two sides cannot drift: every
// advertised name must construct, and anything else must fail rather than
// silently fall back to a default the user did not ask for.

#include "atlas/sched/factory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

#include "atlas/io/options.hpp"
#include "atlas/sched/scheduler.hpp"

namespace atlas {
namespace {

TEST(SchedulerFactory, EveryAdvertisedNameConstructs) {
    ASSERT_FALSE(scheduler_names().empty());

    for (std::string_view name : scheduler_names()) {
        const std::unique_ptr<Scheduler> sched = make_scheduler(name);
        ASSERT_NE(sched, nullptr) << "advertised but not constructible: " << name;

        // The round trip matters: --scheduler X must produce something that
        // calls itself X, or the run report and the trace header would record a
        // policy other than the one that ran.
        EXPECT_EQ(sched->name(), name);
    }
}

TEST(SchedulerFactory, UnknownNameReturnsNull) {
    EXPECT_EQ(make_scheduler("does_not_exist"), nullptr);
    EXPECT_EQ(make_scheduler(""), nullptr);
    EXPECT_EQ(make_scheduler("First_Fit"), nullptr) << "names are case-sensitive";
}

TEST(SchedulerFactory, DefaultOptionsNameIsConstructible) {
    EXPECT_NE(make_scheduler(Options{}.scheduler), nullptr);
}

}  // namespace
}  // namespace atlas
