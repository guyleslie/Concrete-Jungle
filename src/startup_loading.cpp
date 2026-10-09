// =====================================================================================
//  Frozen startup accounting and main-thread presentation dispatch. See startup_loading.h.
//  Time is used only for diagnostics; completion always comes from reported work.
// =====================================================================================
#include "startup_loading.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace startup {
namespace {
bool Resolved(Outcome o) { return o == Outcome::Ready || o == Outcome::ReadyWithFallback || o == Outcome::Skipped; }
bool ValidWeight(double w) { return std::isfinite(w) && w > 0; }
}

const char* OutcomeName(Outcome o) {
    switch (o) {
        case Outcome::Pending: return "pending";
        case Outcome::Running: return "running";
        case Outcome::Ready: return "ready";
        case Outcome::ReadyWithFallback: return "fallback";
        case Outcome::Skipped: return "skipped";
        case Outcome::Failed: return "failed";
        case Outcome::Cancelled: return "cancelled";
    }
    return "unknown";
}
const char* StateName(State s) {
    switch (s) {
        case State::Bootstrapping: return "bootstrap";
        case State::Discovering: return "discovery";
        case State::Loading: return "loading";
        case State::Ready: return "ready";
        case State::Failed: return "failed";
        case State::Cancelled: return "cancelled";
    }
    return "unknown";
}
int Snapshot::Percentage() const {
    if (state == State::Ready) return 100;
    return std::clamp((int)std::floor(std::clamp(fraction, 0.0, 1.0) * 100.0 + 1e-9), 0, 99);
}

Reporter::Reporter(Presenter p) : presenter(std::move(p)) {}
int Reporter::Find(const char* id) const {
    if (!id) return -1;
    for (size_t i = 0; i < tasks.size(); i++) if (tasks[i].spec.id == id) return (int)i;
    return -1;
}
bool Reporter::Present(bool force) {
    if (snapshot.state == State::Cancelled) return false;
    if (presenter && !presenter(snapshot, force)) { Cancel(); return false; }
    return !Stopped();
}
bool Reporter::Contract(const char* cause) {
    planError = true;
    snapshot.determinate = false;
    snapshot.warning = cause ? cause : "Startup work could not be measured";
    Present(true);
    return false;
}
bool Reporter::Register(const TaskSpec& spec) {
    if (Stopped()) return false;
    if (frozen) return Contract("Startup work was added after discovery");
    if (spec.id.empty() || spec.headline.empty() || !ValidWeight(spec.weight) || Find(spec.id.c_str()) >= 0)
        return Contract("Invalid startup task definition");
    Task task; task.spec = spec; tasks.push_back(std::move(task));
    snapshot.state = State::Discovering;
    snapshot.headline = "Finding game content...";
    snapshot.detail = "Preparing the startup work plan";
    return true;
}
bool Reporter::Discover(const char* detail) {
    if (Stopped()) return false;
    if (frozen) return Contract("Discovery resumed after the startup plan froze");
    snapshot.state = State::Discovering;
    snapshot.headline = "Finding game content...";
    snapshot.detail = detail ? detail : "Reading content definitions";
    snapshot.determinate = false;
    return Present(true);
}
bool Reporter::Freeze(const char* finalGate) {
    if (Stopped() || frozen || tasks.empty() || planError) return Contract("Invalid startup work plan");
    gate = Find(finalGate);
    if (gate < 0 || !tasks[gate].spec.mandatory) return Contract("Startup readiness check is missing");
    totalWeight = 0;
    for (const Task& t : tasks) {
        totalWeight += t.spec.weight;
        for (const std::string& d : t.spec.dependencies)
            if (Find(d.c_str()) < 0 || d == t.spec.id) return Contract("Invalid startup task dependency");
    }
    if (!ValidWeight(totalWeight)) return Contract("Invalid startup work budget");
    std::vector<int> visited(tasks.size(), 0);
    std::function<bool(int)> visit = [&](int i) {
        if (visited[i] == 1) return false;
        if (visited[i] == 2) return true;
        visited[i] = 1;
        for (const std::string& d : tasks[i].spec.dependencies) if (!visit(Find(d.c_str()))) return false;
        visited[i] = 2;
        return true;
    };
    for (size_t i = 0; i < tasks.size(); i++) if (!visit((int)i)) return Contract("Cyclic startup dependencies");
    frozen = true;
    snapshot.state = State::Loading;
    snapshot.determinate = true;
    Recalculate();
    return Present(true);
}
bool Reporter::Begin(const char* id, const char* detail, int64_t total, const char* unit) {
    if (Stopped()) return false;
    if (!frozen || planError) return Contract("Startup work began without a valid frozen plan");
    int index = Find(id);
    if (index < 0 || total < -1) return Contract("Unknown startup task or invalid local work total");
    if (active >= 0 && tasks[active].outcome == Outcome::Running) return Contract("Overlapping startup tasks");
    Task& task = tasks[index];
    if (task.outcome != Outcome::Pending) return Contract("Startup task began more than once");
    if (index == gate) for (size_t i = 0; i < tasks.size(); i++)
        if ((int)i != gate && !Resolved(tasks[i].outcome)) return Contract("Final preparation began before startup work completed");
    for (const std::string& dep : task.spec.dependencies)
        if (!Resolved(tasks[Find(dep.c_str())].outcome)) return Contract("Startup dependency has not completed");
    active = index;
    task.outcome = Outcome::Running;
    task.total = total;
    task.unit = unit ? unit : "";
    taskStart = std::chrono::steady_clock::now();
    snapshot.activeId = task.spec.id;
    snapshot.headline = task.spec.headline;
    snapshot.detail = detail ? detail : task.spec.detail;
    snapshot.error.clear(); snapshot.countText.clear();
    snapshot.determinate = total >= 0;
    if (total >= 0 && unit && *unit) Count(0, total, unit);
    return Present(true);
}
bool Reporter::PlanChildren(const std::vector<ChildSpec>& specs) {
    if (Stopped()) return false;
    if (active < 0) return Contract("Child plan has no active startup task");
    Task& task = tasks[active];
    if (task.fraction > 0 || task.childrenPlanned || task.outcome != Outcome::Running)
        return Contract("Local work was added after progress began");
    if (specs.empty() && task.spec.mandatory) return Contract("Mandatory startup group has no planned work");
    std::vector<Child> children;
    double sum = 0;
    for (const ChildSpec& s : specs) {
        if (s.id.empty() || !ValidWeight(s.weight)) return Contract("Invalid startup child budget");
        for (const Child& c : children) if (c.spec.id == s.id) return Contract("Duplicate startup child");
        children.push_back({ s, 0, Outcome::Pending }); sum += s.weight;
    }
    if (!specs.empty() && !ValidWeight(sum)) return Contract("Invalid local startup budget");
    task.children = std::move(children);
    task.childrenPlanned = true;
    snapshot.determinate = !planError && !specs.empty();
    return Present(true);
}
void Reporter::RecalculateChildren() {
    Task& task = tasks[active];
    double total = 0, completed = 0;
    for (const Child& c : task.children) { total += c.spec.weight; completed += c.spec.weight * c.fraction; }
    if (total > 0) task.fraction = completed / total;
    Recalculate();
}
bool Reporter::ChildProgress(const char* id, double fraction) {
    if (Stopped()) return false;
    if (active < 0 || !id || !std::isfinite(fraction) || fraction < 0 || fraction > 1)
        return Contract("Invalid child completion report");
    if (tasks[active].outcome != Outcome::Running) return Contract("Child progress has no running parent");
    for (Child& c : tasks[active].children) if (c.spec.id == id) {
        if (Resolved(c.outcome) || fraction + 1e-12 < c.fraction) return Contract("Child progress contradicted completed work");
        c.outcome = Outcome::Running; c.fraction = fraction;
        RecalculateChildren(); return Present(false);
    }
    return Contract("Unknown startup child");
}
bool Reporter::ChildDone(const char* id, Outcome outcome) {
    if (Stopped()) return false;
    if (active < 0 || !id) return Contract("Child completion has no active task");
    if (tasks[active].outcome != Outcome::Running) return Contract("Child completion has no running parent");
    for (Child& c : tasks[active].children) if (c.spec.id == id) {
        if (Resolved(c.outcome) || c.outcome == Outcome::Failed || c.outcome == Outcome::Cancelled)
            return Contract("Completed startup child cannot change its outcome");
        if (outcome == Outcome::Failed) { c.outcome = outcome; return Fail("Required startup content could not be prepared"); }
        if (outcome == Outcome::Cancelled) { Cancel(); return false; }
        if (!Resolved(outcome) || Resolved(c.outcome) || (outcome == Outcome::Skipped && c.spec.mandatory))
            return Contract("Invalid startup child outcome");
        c.outcome = outcome; c.fraction = 1;
        RecalculateChildren(); return Present(false);
    }
    return Contract("Unknown completed startup child");
}
void Reporter::Recalculate() {
    if (!frozen || totalWeight <= 0) return;
    double done = 0;
    for (const Task& t : tasks) done += t.spec.weight * t.fraction;
    double confirmed = std::clamp(done / totalWeight, 0.0, 0.99);
    snapshot.fraction = std::max(snapshot.fraction, confirmed);
    snapshot.determinate = snapshot.determinate && !planError;
}
bool Reporter::Count(int64_t done, int64_t total, const char* unit) {
    if (Stopped()) return false;
    if (active < 0 || tasks[active].outcome != Outcome::Running) return Contract("Item count has no running startup task");
    if (done < 0 || total < 0 || done > total) return Contract("Invalid startup item count");
    snapshot.countText = unit && *unit ? std::to_string(done) + " / " + std::to_string(total) + " " + unit : "";
    return true;
}
bool Reporter::Progress(int64_t completed, const char* detail) {
    if (Stopped()) return false;
    if (active < 0) return Contract("Progress has no active startup task");
    Task& task = tasks[active];
    if (task.total <= 0 || completed < task.completed || completed > task.total)
        return Contract("Completed work does not match the frozen total");
    task.completed = completed;
    if (!task.unit.empty() && !Count(completed, task.total, task.unit.c_str())) return false;
    return Fraction((double)completed / task.total, detail);
}
bool Reporter::Fraction(double fraction, const char* detail) {
    if (Stopped()) return false;
    if (active < 0 || !std::isfinite(fraction) || fraction < 0 || fraction > 1)
        return Contract("Invalid startup completion fraction");
    Task& task = tasks[active];
    if (!task.children.empty() || fraction + 1e-12 < task.fraction || task.outcome != Outcome::Running)
        return Contract("Progress contradicted its frozen work model");
    task.fraction = fraction;
    snapshot.determinate = !planError;
    if (detail) snapshot.detail = detail;
    Recalculate(); return Present(false);
}
bool Reporter::Pulse(const char* detail, bool force) {
    if (Stopped()) return false;
    if (detail) snapshot.detail = detail;
    return Present(force);
}
bool Reporter::Finish(Outcome outcome, const char* warning) {
    if (Stopped()) return false;
    if (active < 0) return Contract("Task completion has no active startup task");
    Task& task = tasks[active];
    if (outcome == Outcome::Failed) return Fail(warning ? warning : "Startup work could not be prepared");
    if (outcome == Outcome::Cancelled) { Cancel(); return false; }
    if (task.outcome != Outcome::Running || !Resolved(outcome) || (outcome == Outcome::Skipped && task.spec.mandatory))
        return Contract("Invalid startup task outcome");
    if (task.childrenPlanned && task.children.empty() && outcome != Outcome::Skipped)
        return Contract("Empty optional startup group must be policy-skipped");
    if (!task.childrenPlanned && !task.unit.empty() && outcome != Outcome::Skipped && task.completed != task.total)
        return Contract("Startup item total has unfinished obligations");
    for (const Child& c : task.children) {
        if (!Resolved(c.outcome) || (c.spec.mandatory && c.outcome == Outcome::Skipped))
            return Contract("Startup task ended with unfinished required work");
        if (c.outcome == Outcome::ReadyWithFallback && outcome == Outcome::Ready) outcome = Outcome::ReadyWithFallback;
    }
    task.outcome = outcome; task.fraction = 1;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - taskStart).count();
    timings.push_back({ task.spec.id, ms, outcome });
    if (outcome != Outcome::Skipped) {
        snapshot.recentId = task.spec.id; snapshot.recent = task.spec.label;
    }
    if (warning) snapshot.warning = warning;
    snapshot.countText.clear();
    snapshot.determinate = !planError;
    Recalculate(); return Present(true);
}
bool Reporter::Ready() {
    if (Stopped()) return false;
    if (!frozen || planError || gate < 0 || !Resolved(tasks[gate].outcome)) return Fail("Startup readiness is incomplete");
    for (const Task& t : tasks)
        if (!Resolved(t.outcome) || (t.spec.mandatory && t.outcome == Outcome::Skipped))
            return Fail("Required startup work has not completed");
    snapshot.state = State::Ready; snapshot.headline = "Ready";
    snapshot.detail = "The city is ready"; snapshot.countText.clear();
    snapshot.fraction = 1; snapshot.determinate = true;
    return Present(true);
}
bool Reporter::Fail(const char* cause) {
    if (snapshot.state == State::Cancelled) return false;
    if (active >= 0 && tasks[active].outcome == Outcome::Running) tasks[active].outcome = Outcome::Failed;
    snapshot.state = State::Failed; snapshot.error = cause ? cause : "The game could not start";
    snapshot.headline = "Unable to start"; snapshot.detail = snapshot.error;
    snapshot.countText.clear(); snapshot.determinate = false;
    Present(true); return false;
}
void Reporter::Cancel() {
    if (active >= 0 && tasks[active].outcome == Outcome::Running) tasks[active].outcome = Outcome::Cancelled;
    snapshot.state = State::Cancelled; snapshot.headline = "Closing...";
    snapshot.detail = "Releasing startup resources"; snapshot.countText.clear(); snapshot.determinate = false;
}
Outcome Reporter::Result(const char* id) const {
    int i = Find(id); return i < 0 ? Outcome::Pending : tasks[i].outcome;
}
void Reporter::RecordAtomic(const char* category, const char* operation, double milliseconds) {
    if (!category || !std::isfinite(milliseconds) || milliseconds < 0) return;
    auto i = std::find_if(atomics.begin(), atomics.end(), [&](const AtomicTiming& t) { return t.category == category; });
    if (i == atomics.end()) { atomics.push_back({ category, "", 0, 0, 0 }); i = atomics.end() - 1; }
    i->calls++; i->totalMs += milliseconds;
    if (milliseconds >= i->maxMs) { i->maxMs = milliseconds; i->slowest = operation ? operation : ""; }
}
AtomicSpan::AtomicSpan(Reporter* r, const char* c, const char* op)
    : reporter(r), category(c ? c : ""), operation(op ? op : ""), start(std::chrono::steady_clock::now()) {}
AtomicSpan::~AtomicSpan() {
    if (reporter) reporter->RecordAtomic(category.c_str(), operation.c_str(),
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

} // namespace startup
