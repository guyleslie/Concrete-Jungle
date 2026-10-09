// =====================================================================================
//  CJ-030 headless regression tests for startup accounting and lifecycle contracts.
//  Compile this file with src/startup_loading.cpp and -Isrc; no raylib is required.
// =====================================================================================
#include "startup_loading.h"
#include "startup_input.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using startup::ChildSpec;
using startup::Outcome;
using startup::Reporter;
using startup::Snapshot;
using startup::State;
using startup::TaskSpec;

namespace {
int cases = 0, checks = 0, failures = 0;

void Require(bool condition, const char* explanation) {
    checks++;
    if (!condition) throw std::runtime_error(explanation);
}

void Near(double actual, double expected, const char* explanation) {
    Require(std::isfinite(actual) && std::fabs(actual - expected) < 1e-10, explanation);
}

void Case(const char* name, const std::function<void()>& body) {
    cases++;
    try {
        body();
        std::printf("PASS %s\n", name);
    } catch (const std::exception& error) {
        failures++;
        std::printf("FAIL %s: %s\n", name, error.what());
    }
}

TaskSpec Task(const char* id, double weight = 1, bool mandatory = true,
              std::vector<std::string> dependencies = {}) {
    TaskSpec task;
    task.id = id;
    task.label = std::string("Completed ") + id;
    task.headline = std::string("Preparing ") + id;
    task.detail = std::string("Contents of ") + id;
    task.weight = weight;
    task.mandatory = mandatory;
    task.dependencies = std::move(dependencies);
    return task;
}

void Plan(Reporter& reporter, const std::vector<TaskSpec>& tasks, double gateWeight = 1) {
    for (const TaskSpec& task : tasks) Require(reporter.Register(task), "Valid task registration failed");
    Require(reporter.Register(Task("startup.ready", gateWeight)), "Final gate registration failed");
    Require(reporter.Freeze(), "Valid plan did not freeze");
}

void Complete(Reporter& reporter, const char* id) {
    Require(reporter.Begin(id, nullptr, 1, "work units"), "Valid task did not begin");
    Require(reporter.Progress(1), "Completed obligation did not advance");
    Require(reporter.Finish(), "Completed task did not finish");
}

void CompleteGate(Reporter& reporter) {
    Complete(reporter, "startup.ready");
    Require(reporter.View().state != State::Ready, "Gate completion bypassed readiness validation");
    Require(reporter.View().Percentage() < 100, "Gate completion displayed 100 before Ready");
}

void WeightedProgress() {
    std::vector<double> observed;
    Reporter reporter([&](const Snapshot& snapshot, bool) {
        observed.push_back(snapshot.fraction);
        return true;
    });
    Plan(reporter, { Task("small", 2), Task("large", 7) }, 1);
    Near(reporter.TotalWeight(), 10, "Frozen top-level weight is incorrect");
    Require(reporter.Begin("small", "Preparing textures", 4, "textures"), "Counted task failed to begin");
    Require(reporter.View().countText == "0 / 4 textures", "Initial count lost its unit");
    Require(reporter.Progress(1), "First counted checkpoint failed");
    Near(reporter.View().fraction, 0.05, "Counted checkpoint has incorrect weighted credit");
    Require(reporter.View().Percentage() == 5, "Weighted percentage is incorrect");
    Require(reporter.View().countText == "1 / 4 textures", "Progress did not refresh qualified count");
    Require(reporter.Progress(4), "Final counted checkpoint failed");
    Require(reporter.Finish(), "Fully counted small task did not finish");
    Near(reporter.View().fraction, 0.2, "Small task weight is incorrect");
    Require(reporter.View().countText.empty(), "Completed group leaked its old local count");
    Require(reporter.Begin("large", nullptr, 10, "atlases"), "Large task failed to begin");
    Require(reporter.Progress(5), "Large task midpoint failed");
    Near(reporter.View().fraction, 0.55, "Unequal weights were reduced to a task count");
    Require(reporter.Progress(10), "Large task completion failed");
    Require(reporter.Finish(), "Large task did not finish");
    Near(reporter.View().fraction, 0.9, "Gate reserve is incorrect");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Fully resolved plan was rejected");
    Require(reporter.View().state == State::Ready && reporter.View().Percentage() == 100, "Ready did not permit 100");
    Near(reporter.View().fraction, 1, "Ready fill does not match its percentage");
    for (size_t i = 1; i < observed.size(); i++)
        Require(observed[i] + 1e-12 >= observed[i - 1], "Observed progress moved backwards");
}

void ScalableSnapshots(int groupCount) {
    Reporter reporter;
    std::vector<TaskSpec> tasks;
    for (int i = 0; i < groupCount; i++) tasks.push_back(Task(("group." + std::to_string(i)).c_str()));
    Plan(reporter, tasks);
    Require(reporter.TaskCount() == (size_t)groupCount + 1, "Scalable registration lost groups");
    for (int i = 0; i < groupCount; i++) {
        std::string id = "group." + std::to_string(i);
        Require(reporter.Begin(id.c_str(), "Current content", 1, "records processed"), "Scalable group did not begin");
        const Snapshot& active = reporter.View();
        Require(active.activeId == id && active.detail == "Current content", "Snapshot did not identify only current work");
        Require(active.countText == "0 / 1 records processed", "Current group count was not reset");
        Require(active.recentId == (i == 0 ? "" : "group." + std::to_string(i - 1)), "Snapshot retained a completion queue");
        Require(reporter.Progress(1) && reporter.Finish(), "Scalable group did not complete");
        Require(reporter.View().recentId == id && reporter.View().recent == "Completed " + id,
                "Single recent-completion field was not replaced");
        Require(reporter.View().countText.empty(), "Scalable snapshot retained a finished group count");
    }
    CompleteGate(reporter);
    Require(reporter.Ready(), "Expanded startup plan did not reach readiness");
}

void CappedOverallKeepsLocalWork() {
    Reporter reporter;
    Plan(reporter, { Task("early", 999), Task("late", 1) }, 0.001);
    Complete(reporter, "early");
    Require(reporter.View().Percentage() == 99, "Late-work fixture did not reach the readiness cap");
    Require(reporter.Begin("late", nullptr, 2, "records"), "Late counted work did not begin");
    Near(reporter.CurrentTaskFraction(), 0, "Local completion leaked from the previous group");
    Require(reporter.Progress(1), "Late checkpoint failed");
    Near(reporter.CurrentTaskFraction(), 0.5, "Overall readiness cap concealed real local work");
    Require(reporter.View().Percentage() == 99, "Local checkpoint bypassed readiness cap");
    Require(reporter.Progress(2) && reporter.Finish(), "Late work could not finish");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Capped plan could not become ready after validation");
}

void UnknownWork() {
    Reporter reporter;
    Plan(reporter, { Task("unknown", 4) });
    Require(reporter.Begin("unknown", "Discovering child work", -1), "Unknown group did not begin");
    Require(!reporter.View().determinate, "Unknown work displayed a numeric percentage");
    double before = reporter.View().fraction;
    Require(reporter.Pulse("Still discovering", true), "Unknown work could not service presentation");
    Near(reporter.View().fraction, before, "Activity pulse invented completed work");
    Require(reporter.PlanChildren({ { "a", 1 }, { "b", 3 } }), "Unknown group could not freeze its real child plan");
    Require(reporter.View().determinate, "Resolved child plan remained indeterminate");
    Near(reporter.TotalWeight(), 5, "Child discovery changed the top-level denominator");
    Require(reporter.ChildDone("a"), "Discovered child did not complete");
    Near(reporter.View().fraction, 0.2, "Discovered child earned incorrect credit");
    Require(reporter.ChildDone("b") && reporter.Finish(), "Discovered work did not resolve");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Known work did not transition to readiness");
}

void NoTimedCredit() {
    Reporter reporter;
    Plan(reporter, { Task("work", 9) });
    Require(reporter.Begin("work", nullptr, 10, "records processed"), "Timed-credit fixture did not begin");
    Require(reporter.Progress(3), "Observed checkpoint did not advance");
    double before = reporter.View().fraction;
    int percentage = reporter.View().Percentage();
    for (int i = 0; i < 100; i++) Require(reporter.Pulse("Working", i % 7 == 0), "Pulse was unexpectedly rejected");
    reporter.RecordAtomic("decode", "large texture", 60000);
    Near(reporter.View().fraction, before, "Pulses or elapsed-time diagnostics advanced completion");
    Require(reporter.View().Percentage() == percentage, "Percentage changed without completed work");
    Require(reporter.View().countText == "3 / 10 records processed", "Activity pulse changed actual work count");
}

void DiscoveryReportsWithoutCredit() {
    int presentations = 0;
    Reporter reporter([&](const Snapshot& snapshot, bool) {
        presentations++;
        Require(!snapshot.determinate && snapshot.fraction == 0, "Discovery showed measured completion");
        return true;
    });
    Require(reporter.Discover("Inspecting content definitions"), "Initial discovery could not begin");
    Require(reporter.View().state == State::Discovering && reporter.View().detail == "Inspecting content definitions",
            "Discovery did not expose its real work caption");
    Require(reporter.View().activeId.empty() && reporter.View().countText.empty(), "Discovery pretended an active counted task existed");
    Require(reporter.TaskCount() == 0 && reporter.TotalWeight() == 0, "Discovery registered imaginary work");
    for (int i = 0; i < 32; i++) Require(reporter.Pulse("Inspecting metadata"), "Discovery pulse could not service events");
    Require(presentations >= 33, "Discovery pulses were not forwarded to the presenter");
    Near(reporter.View().fraction, 0, "Discovery activity invented completion credit");
    Require(reporter.View().Percentage() < 100 && !reporter.Frozen(), "Discovery permitted a completed frozen plan");
}

void DiscoveryBeforeFreeze() {
    Reporter reporter;
    Require(reporter.Discover(), "Default discovery did not begin");
    Require(reporter.Register(Task("work")) && reporter.Register(Task("startup.ready")), "Discovery fixture registration failed");
    Require(reporter.Discover("Inspecting remaining definitions"), "Unfrozen registered plan rejected discovery");
    Require(reporter.TaskCount() == 2 && reporter.TotalWeight() == 0 && !reporter.View().determinate,
            "Unfrozen discovery changed measured task budgets");
    Require(reporter.Freeze(), "Completed discovery could not freeze its real work plan");
    Complete(reporter, "work");
    CompleteGate(reporter);
    Require(reporter.Ready(), "A properly frozen discovery plan failed readiness");
}

void DiscoveryAfterFreezeRejected() {
    Reporter reporter;
    Plan(reporter, { Task("work", 9) });
    Require(reporter.Begin("work", nullptr, 4, "records") && reporter.Progress(1), "Late-discovery fixture failed");
    double weight = reporter.TotalWeight(), confirmed = reporter.View().fraction;
    Require(!reporter.Discover("Late metadata"), "Frozen measured work returned to discovery");
    Near(reporter.TotalWeight(), weight, "Late discovery changed frozen budgets");
    Near(reporter.View().fraction, confirmed, "Late discovery advanced or reversed completion");
    Require(!reporter.View().determinate && !reporter.Ready(), "Late discovery retained a successful work contract");
}

void DiscoveryCancellation() {
    bool stop = false;
    Reporter reporter([&](const Snapshot&, bool) { return !stop; });
    Require(reporter.Discover("Inspecting definitions"), "Discovery cancellation fixture failed");
    stop = true;
    Require(!reporter.Pulse("Inspecting file metadata") && reporter.Cancelled(), "Discovery presenter rejection did not cancel work");
    Require(!reporter.Discover() && !reporter.Register(Task("work")) && !reporter.Ready(), "Cancelled discovery resumed scheduling");
    Require(reporter.TaskCount() == 0 && reporter.View().Percentage() < 100, "Cancelled discovery invented measured completion");
}

void DiscoveryCannotBecomeReady() {
    Reporter reporter;
    Require(reporter.Discover("Inspecting definitions") && reporter.Pulse(), "Readiness fixture discovery failed");
    Require(!reporter.Ready() && reporter.View().state == State::Failed, "Discovery alone became ready");
    Require(reporter.View().Percentage() < 100, "Discovery failure displayed completed startup");
}

void GateCannotStartEarly() {
    Reporter reporter;
    Plan(reporter, { Task("work") });
    Require(!reporter.Begin("startup.ready", nullptr, 1), "Gate began before required work resolved");
    Require(reporter.Result("startup.ready") == Outcome::Pending, "Rejected gate became completed");
    Require(!reporter.View().determinate && reporter.View().Percentage() < 100, "Invalid gate displayed success");
    Require(!reporter.Ready(), "Incomplete gate permitted readiness");
}

void ReadinessRejectsIncompleteWork() {
    Reporter reporter;
    Plan(reporter, { Task("work") });
    Require(reporter.Begin("work", nullptr, 10, "resources"), "Incomplete fixture did not begin");
    Require(reporter.Progress(9), "Partial work did not advance");
    Require(!reporter.Ready(), "Unresolved work permitted 100");
    Require(reporter.View().state == State::Failed && reporter.View().Percentage() < 100, "Rejected readiness did not stop startup");
}

void LateRegistration() {
    Reporter reporter;
    Plan(reporter, { Task("work", 9) });
    Require(reporter.Begin("work", nullptr, 4, "resources") && reporter.Progress(1), "Late-registration fixture failed");
    double weight = reporter.TotalWeight(), progress = reporter.View().fraction;
    size_t count = reporter.TaskCount();
    Require(!reporter.Register(Task("late", 100)), "Frozen plan accepted late top-level work");
    Near(reporter.TotalWeight(), weight, "Late task changed the frozen denominator");
    Near(reporter.View().fraction, progress, "Late task changed confirmed progress");
    Require(reporter.TaskCount() == count && !reporter.View().determinate, "Late task was hidden in a valid-looking plan");
    Require(!reporter.Ready(), "Plan-contract violation permitted readiness");
}

void LateChildPlan() {
    Reporter reporter;
    Plan(reporter, { Task("parent", 9) });
    Require(reporter.Begin("parent", nullptr, 1) && reporter.Fraction(0.25), "Late-child fixture failed");
    double weight = reporter.TotalWeight(), progress = reporter.View().fraction;
    Require(!reporter.PlanChildren({ { "late", 100 } }), "Parent accepted children after earning credit");
    Near(reporter.TotalWeight(), weight, "Late children entered the top-level denominator");
    Near(reporter.View().fraction, progress, "Late children changed credited work");
    Require(!reporter.View().determinate && !reporter.Ready(), "Late child plan remained acceptable");
}

void ChildPlanOnlyOnce() {
    Reporter reporter;
    Plan(reporter, { Task("parent") });
    const std::vector<ChildSpec> children{ { "first" } };
    const std::vector<ChildSpec> replacement{ { "replacement" } };
    Require(reporter.Begin("parent", nullptr, 1) && reporter.PlanChildren(children), "Initial child plan failed");
    Require(!reporter.PlanChildren(replacement), "Zero-credit parent accepted a second child plan");
    Require(!reporter.View().determinate, "Repeated child plan retained a numeric percentage");
}

void WeightedChildren() {
    Reporter reporter;
    Plan(reporter, { Task("parent", 8) }, 2);
    Require(reporter.Begin("parent", nullptr, 1), "Parent did not begin");
    Require(reporter.PlanChildren({ { "small", 1 }, { "large", 3 } }), "Weighted child plan failed");
    Require(reporter.ChildDone("small"), "Small child did not finish");
    Near(reporter.View().fraction, 0.2, "Descendant weights were double-counted globally");
    Require(reporter.Count(300, 999, "records examined"), "Independent local count failed");
    Near(reporter.View().fraction, 0.2, "Display-only count awarded extra global credit");
    Require(reporter.ChildProgress("large", 0.5), "Large child midpoint failed");
    Near(reporter.View().fraction, 0.5, "Child fraction was not weighted within its parent");
    Require(reporter.ChildDone("large") && reporter.Finish(), "Weighted children did not resolve parent");
    Near(reporter.View().fraction, 0.8, "Parent completion changed the frozen global budget");
    Near(reporter.TotalWeight(), 10, "Child weights inflated the top-level denominator");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Weighted child plan did not pass readiness");
}

void OptionalEmptyGroup() {
    Reporter reporter;
    Plan(reporter, { Task("optional", 3, false) });
    Require(reporter.Begin("optional", nullptr, -1) && reporter.PlanChildren({}), "Empty optional plan was rejected");
    Require(!reporter.View().determinate, "Empty optional plan divided an empty child budget");
    Require(reporter.Finish(Outcome::Skipped, "Audio unavailable; continuing silently"), "Empty optional group did not policy-skip");
    Require(reporter.Result("optional") == Outcome::Skipped && reporter.View().recent.empty(), "Absent optional content was labelled loaded");
    Require(!reporter.View().warning.empty(), "Silent fallback warning was lost");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Policy-skipped optional group blocked readiness");
}

void MandatoryEmptyGroup() {
    Reporter reporter;
    Plan(reporter, { Task("mandatory") });
    Require(reporter.Begin("mandatory", nullptr, -1), "Mandatory fixture did not begin");
    Require(!reporter.PlanChildren({}), "Empty mandatory child plan was accepted");
    Require(!reporter.Ready(), "Empty mandatory content permitted readiness");
    Require(reporter.View().Percentage() < 100, "Empty mandatory group displayed completion");
}

void EmptyOptionalCannotClaimLoaded() {
    Reporter reporter;
    Plan(reporter, { Task("optional", 1, false) });
    Require(reporter.Begin("optional", nullptr, 1) && reporter.PlanChildren({}), "Optional fixture did not begin");
    Require(!reporter.Finish(), "Empty optional group claimed loaded content");
    Require(reporter.Result("optional") == Outcome::Running, "Rejected empty completion became ready");
}

void RequiredChildCannotSkip() {
    Reporter reporter;
    Plan(reporter, { Task("parent") });
    const std::vector<ChildSpec> children{ { "required" } };
    Require(reporter.Begin("parent", nullptr, 1) && reporter.PlanChildren(children), "Required-child fixture failed");
    Require(!reporter.ChildDone("required", Outcome::Skipped), "Required child was silently skipped");
    Require(!reporter.Ready(), "Required child omission permitted readiness");
}

void OptionalChildCanSkip() {
    Reporter reporter;
    Plan(reporter, { Task("parent", 4) });
    const std::vector<ChildSpec> children{ { "required" }, { "optional", 1, false } };
    Require(reporter.Begin("parent", nullptr, 1) && reporter.PlanChildren(children),
            "Mixed child plan failed");
    Require(reporter.ChildDone("required") && reporter.ChildDone("optional", Outcome::Skipped) && reporter.Finish(),
            "Optional child policy did not resolve parent");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Resolved optional child blocked readiness");
}

void FallbackResolvesOnce() {
    Reporter reporter;
    Plan(reporter, { Task("parent", 4) });
    const std::vector<ChildSpec> children{ { "resource" } };
    Require(reporter.Begin("parent", nullptr, 1) && reporter.PlanChildren(children), "Fallback fixture failed");
    Require(reporter.ChildDone("resource", Outcome::ReadyWithFallback) && reporter.Finish(), "Usable fallback did not resolve parent");
    Require(reporter.Result("parent") == Outcome::ReadyWithFallback, "Parent lost fallback disposition");
    Near(reporter.View().fraction, 0.8, "Fallback earned more than one original obligation");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Usable fallback blocked readiness");
}

void CompletedChildCannotChange(Outcome attempted) {
    Reporter reporter;
    Plan(reporter, { Task("parent", 4) });
    const std::vector<ChildSpec> children{ { "resource" }, { "pending" } };
    Require(reporter.Begin("parent", nullptr, 1) && reporter.PlanChildren(children),
            "Completed-child fixture failed");
    Require(reporter.ChildDone("resource", Outcome::ReadyWithFallback), "Initial fallback did not complete");
    double confirmed = reporter.View().fraction;
    Require(!reporter.ChildDone("resource", attempted), "Completed child changed its disposition");
    Require(reporter.View().state == State::Loading && reporter.Result("parent") == Outcome::Running,
            "Repeated completed-child report changed lifecycle state");
    Near(reporter.View().fraction, confirmed, "Repeated child completion awarded or removed credit");
    Require(!reporter.View().determinate, "Contradictory completion retained a valid numeric percentage");
}

void FailureStopsWork() {
    Reporter reporter;
    Plan(reporter, { Task("first"), Task("second") });
    Require(reporter.Begin("first", nullptr, 1), "Failure fixture did not begin");
    Require(!reporter.Fail("Required texture unavailable"), "Failure returned success");
    Require(reporter.View().state == State::Failed && reporter.Result("first") == Outcome::Failed, "Failure did not stop its task");
    double confirmed = reporter.View().fraction;
    Require(!reporter.Progress(1) && !reporter.Pulse() && !reporter.Finish() && !reporter.Begin("second", nullptr, 1) && !reporter.Ready(),
            "Failed startup continued scheduling or reached readiness");
    Near(reporter.View().fraction, confirmed, "Failed startup gained completion credit");
    Require(reporter.Result("second") == Outcome::Pending && reporter.View().Percentage() < 100, "Failure entered later work");
}

void CancellationStopsWork() {
    Reporter reporter;
    Plan(reporter, { Task("first"), Task("second") });
    Require(reporter.Begin("first", nullptr, 4, "resources") && reporter.Progress(1), "Cancellation fixture did not begin");
    reporter.Cancel();
    Require(reporter.Cancelled() && reporter.Result("first") == Outcome::Cancelled, "Cancellation did not stop active work");
    double confirmed = reporter.View().fraction;
    Require(!reporter.Progress(4) && !reporter.Pulse() && !reporter.Finish() && !reporter.Begin("second", nullptr, 1) && !reporter.Ready(),
            "Cancelled startup continued scheduling or reached readiness");
    Near(reporter.View().fraction, confirmed, "Cancelled startup gained completion credit");
    Require(reporter.Result("second") == Outcome::Pending && reporter.View().Percentage() < 100, "Cancellation entered later work");
}

void PresenterCancellation() {
    bool stop = false;
    Reporter reporter([&](const Snapshot&, bool) { return !stop; });
    Plan(reporter, { Task("work") });
    Require(reporter.Begin("work", nullptr, 1), "Presenter cancellation fixture did not begin");
    stop = true;
    Require(!reporter.Pulse(), "Presenter cancellation was ignored");
    Require(reporter.Cancelled() && reporter.Result("work") == Outcome::Cancelled, "Presenter rejection did not propagate cancellation");
    Require(!reporter.Ready(), "Presenter-cancelled startup reached readiness");
}

void CountNeedsActiveWork(bool afterFinish) {
    Reporter reporter;
    Plan(reporter, { Task("work") });
    if (afterFinish) Complete(reporter, "work");
    Require(!reporter.Count(1, 1, "textures"), "Count accepted an absent or finished active task");
    Require(reporter.View().countText.empty() && !reporter.View().determinate, "Invalid count appeared on screen");
}

void InvalidWeights() {
    const double bad[] = { 0, -1, std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() };
    for (double weight : bad) {
        Reporter reporter;
        Require(!reporter.Register(Task("bad", weight)), "Non-finite or non-positive task weight was accepted");
        Require(reporter.TaskCount() == 0 && !reporter.View().determinate, "Invalid weight entered the measured plan");
        Require(!reporter.Freeze(), "Invalid task weight produced a frozen denominator");
    }
}

void InvalidChildWeights() {
    const double bad[] = { 0, -1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() };
    for (double weight : bad) {
        Reporter reporter;
        Plan(reporter, { Task("parent") });
        Require(reporter.Begin("parent", nullptr, 1), "Invalid-child fixture did not begin");
        Require(!reporter.PlanChildren({ { "bad", weight } }), "Invalid child weight was accepted");
        Near(reporter.TotalWeight(), 2, "Invalid child weight changed the global denominator");
        Require(!reporter.View().determinate, "Invalid child budget retained numeric progress");
    }
}

void DependencyRejection(const std::vector<TaskSpec>& tasks) {
    Reporter reporter;
    for (const TaskSpec& task : tasks) Require(reporter.Register(task), "Dependency fixture registration failed");
    Require(reporter.Register(Task("startup.ready")), "Dependency fixture gate registration failed");
    Require(!reporter.Freeze() && !reporter.Frozen(), "Invalid dependencies formed a frozen startup plan");
    Require(!reporter.View().determinate && reporter.View().Percentage() < 100, "Invalid dependency plan displayed readiness");
}

void DependenciesEnforced() {
    Reporter reporter;
    Plan(reporter, { Task("first"), Task("second", 1, true, { "first" }) });
    Complete(reporter, "first");
    Complete(reporter, "second");
    CompleteGate(reporter);
    Require(reporter.Ready(), "Resolved dependency chain blocked readiness");
}

void InvalidCounts() {
    const int64_t invalid[][2] = { { -1, 4 }, { 5, 4 }, { 0, -1 } };
    for (const auto& count : invalid) {
        Reporter reporter;
        Plan(reporter, { Task("work") });
        Require(reporter.Begin("work", nullptr, 4, "records"), "Invalid-count fixture did not begin");
        Require(!reporter.Count(count[0], count[1], "records"), "Impossible local count was accepted");
        Near(reporter.View().fraction, 0, "Impossible count earned completion credit");
    }
}

void BackwardsProgress() {
    Reporter reporter;
    Plan(reporter, { Task("work", 9) });
    Require(reporter.Begin("work", nullptr, 4, "records") && reporter.Progress(2), "Backwards fixture failed");
    double confirmed = reporter.View().fraction;
    Require(!reporter.Progress(1), "Counter moved backwards");
    Near(reporter.View().fraction, confirmed, "Rejected backwards counter reversed the overall bar");
    Require(!reporter.View().determinate, "Counter contradiction retained numeric progress");
}

void IncompleteCountCannotFinish() {
    Reporter reporter;
    Plan(reporter, { Task("work") });
    Require(reporter.Begin("work", nullptr, 4, "textures") && reporter.Progress(3), "Incomplete counted fixture failed");
    Require(!reporter.Finish(), "Unfinished counted obligations were marked loaded");
    Require(reporter.Result("work") == Outcome::Running, "Incomplete counted task became ready");
}

void DiagnosticAggregation() {
    Reporter reporter;
    Plan(reporter, { Task("work") });
    reporter.RecordAtomic("decode", "first", 2);
    reporter.RecordAtomic("decode", "second", 7);
    reporter.RecordAtomic("decode", "ignored", -1);
    reporter.RecordAtomic("upload", "invalid", std::numeric_limits<double>::quiet_NaN());
    Require(reporter.Atomics().size() == 1 && reporter.Atomics()[0].calls == 2, "Invalid atomic timings entered diagnostics");
    Near(reporter.Atomics()[0].totalMs, 9, "Atomic totals were not aggregated");
    Near(reporter.Atomics()[0].maxMs, 7, "Slowest atomic call was not retained");
    Require(reporter.Atomics()[0].slowest == "second", "Slowest operation name was lost");
    Near(reporter.View().fraction, 0, "Atomic timing diagnostics granted completion credit");
}

void FreshTitleKeys() {
    startup::TitleInputGuard guard;
    Require(guard.AcceptStart(true, false, true, false), "First fresh Enter press was gated");
    Require(guard.AcceptStart(false, true, false, true), "First fresh Space press was gated");
    Require(!guard.AcceptStart(true, true, false, false), "Held keys started without a fresh press");
}

void HeldEnterRearmsOnRelease() {
    startup::TitleInputGuard guard;
    guard.Suppress(true, false);
    Require(!guard.AcceptStart(true, false, true, false), "Startup-held Enter press started gameplay");
    Require(!guard.AcceptStart(true, false, true, false), "Repeated startup-held Enter bypassed suppression");
    Require(!guard.AcceptStart(false, false, false, false), "Enter release itself started gameplay");
    Require(guard.AcceptStart(true, false, true, false), "Fresh Enter after release remained suppressed");
}

void HeldSpaceRearmsOnRelease() {
    startup::TitleInputGuard guard;
    guard.Suppress(false, true);
    Require(!guard.AcceptStart(false, true, false, true), "Startup-held Space press started gameplay");
    Require(!guard.AcceptStart(false, true, false, true), "Repeated startup-held Space bypassed suppression");
    Require(!guard.AcceptStart(false, false, false, false), "Space release itself started gameplay");
    Require(guard.AcceptStart(false, true, false, true), "Fresh Space after release remained suppressed");
}

void UnheldKeyWorksImmediately() {
    startup::TitleInputGuard guard;
    guard.Suppress(true, false);
    Require(guard.AcceptStart(true, true, false, true), "Held Enter incorrectly gated fresh Space");
    guard.Suppress(false, true);
    Require(guard.AcceptStart(true, true, true, false), "Held Space incorrectly gated fresh Enter");
}

void HeldKeysReleaseIndependently() {
    startup::TitleInputGuard guard;
    guard.Suppress(true, true);
    Require(!guard.AcceptStart(true, true, true, true), "Both startup-held keys started gameplay");
    Require(!guard.AcceptStart(false, true, false, true), "Releasing Enter also rearmed held Space");
    Require(guard.AcceptStart(true, true, true, false), "Released Enter remained blocked by held Space");
    Require(!guard.AcceptStart(true, true, false, true), "Space rearmed before its own release");
    Require(!guard.AcceptStart(true, false, false, false), "Space release started gameplay");
    Require(guard.AcceptStart(true, true, false, true), "Released Space did not accept a fresh press");
}

void ReleasedKeysNeedFreshPress() {
    startup::TitleInputGuard guard;
    guard.Suppress(true, true);
    Require(!guard.AcceptStart(false, false, false, false), "Releasing held keys started gameplay");
    Require(!guard.AcceptStart(true, true, false, false), "Rearmed down state auto-started without a press event");
    Require(guard.AcceptStart(true, true, true, false), "Fresh press did not work after both releases");
}

void ReadinessCaptureReplacesSuppression() {
    startup::TitleInputGuard guard;
    guard.Suppress(true, true);
    guard.Suppress(false, true);
    Require(guard.AcceptStart(true, true, true, false), "New readiness capture retained stale Enter suppression");
    Require(!guard.AcceptStart(true, true, false, true), "New readiness capture lost Space suppression");
    guard.Suppress(false, false);
    Require(guard.AcceptStart(false, true, false, true), "Unheld readiness capture added a blanket delay");
}
} // namespace

int main() {
    Case("weighted counted progress and final gate", WeightedProgress);
    Case("99 percent cap retains real local completion", CappedOverallKeepsLocalWork);
    Case("5 groups retain current and one recent snapshot", [] { ScalableSnapshots(5); });
    Case("25 groups retain current and one recent snapshot", [] { ScalableSnapshots(25); });
    Case("100 groups retain current and one recent snapshot", [] { ScalableSnapshots(100); });
    Case("unknown work becomes known without a new global budget", UnknownWork);
    Case("activity and timing award no completion", NoTimedCredit);
    Case("discovery captions and pulses award no completion", DiscoveryReportsWithoutCredit);
    Case("registered discovery can freeze and finish normally", DiscoveryBeforeFreeze);
    Case("discovery cannot resume after work freezes", DiscoveryAfterFreezeRejected);
    Case("discovery cancellation stops scheduling", DiscoveryCancellation);
    Case("discovery pulses cannot reach readiness", DiscoveryCannotBecomeReady);
    Case("gate cannot begin before startup work", GateCannotStartEarly);
    Case("readiness rejects unfinished work", ReadinessRejectsIncompleteWork);
    Case("frozen plan rejects late tasks", LateRegistration);
    Case("credited parent rejects late children", LateChildPlan);
    Case("child plan freezes only once", ChildPlanOnlyOnce);
    Case("weighted descendants consume only parent budget", WeightedChildren);
    Case("empty optional group is policy-skipped", OptionalEmptyGroup);
    Case("empty mandatory group rejects readiness", MandatoryEmptyGroup);
    Case("empty optional group cannot claim loaded content", EmptyOptionalCannotClaimLoaded);
    Case("required child cannot be skipped", RequiredChildCannotSkip);
    Case("optional child skip resolves parent", OptionalChildCanSkip);
    Case("usable fallback resolves original obligation once", FallbackResolvesOnce);
    Case("completed fallback cannot finish twice", [] { CompletedChildCannotChange(Outcome::ReadyWithFallback); });
    Case("completed fallback cannot become failed", [] { CompletedChildCannotChange(Outcome::Failed); });
    Case("completed fallback cannot become cancelled", [] { CompletedChildCannotChange(Outcome::Cancelled); });
    Case("failure stops further work and readiness", FailureStopsWork);
    Case("cancellation stops further work and readiness", CancellationStopsWork);
    Case("presenter cancellation propagates", PresenterCancellation);
    Case("count requires an active task", [] { CountNeedsActiveWork(false); });
    Case("count rejects a finished task", [] { CountNeedsActiveWork(true); });
    Case("task weights must be finite and positive", InvalidWeights);
    Case("child weights must be finite and positive", InvalidChildWeights);
    Case("unknown dependency cannot freeze", [] { DependencyRejection({ Task("work", 1, true, { "absent" }) }); });
    Case("self dependency cannot freeze", [] { DependencyRejection({ Task("work", 1, true, { "work" }) }); });
    Case("cyclic dependencies cannot freeze", [] { DependencyRejection({ Task("a", 1, true, { "b" }), Task("b", 1, true, { "a" }) }); });
    Case("valid dependency chain can finish", DependenciesEnforced);
    Case("impossible qualified counts rejected", InvalidCounts);
    Case("counter cannot move backwards", BackwardsProgress);
    Case("unfinished counted group cannot finish", IncompleteCountCannotFinish);
    Case("atomic diagnostics do not award progress", DiagnosticAggregation);
    Case("fresh title keys work without a startup hold", FreshTitleKeys);
    Case("held Enter rearms only after release", HeldEnterRearmsOnRelease);
    Case("held Space rearms only after release", HeldSpaceRearmsOnRelease);
    Case("an unheld title key works immediately", UnheldKeyWorksImmediately);
    Case("held title keys release independently", HeldKeysReleaseIndependently);
    Case("released title keys still need a fresh press", ReleasedKeysNeedFreshPress);
    Case("readiness capture replaces old key suppression", ReadinessCaptureReplacesSuppression);
    std::printf("CJ030_MODEL_TESTS cases=%d checks=%d failures=%d\n", cases, checks, failures);
    return failures == 0 ? 0 : 1;
}
