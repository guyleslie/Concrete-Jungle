// =====================================================================================
//  Startup work accounting: frozen weighted tasks, local child budgets and snapshots.
//  This module has no raylib or gameplay dependency. Subsystems report completed work;
//  the main-thread presenter services events only at safe graphics boundaries.
// =====================================================================================
#pragma once
#include <cstdint>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace startup {

enum class State { Bootstrapping, Discovering, Loading, Ready, Failed, Cancelled };
enum class Outcome { Pending, Running, Ready, ReadyWithFallback, Skipped, Failed, Cancelled };

struct TaskSpec {
    std::string id, label, headline, detail;
    double weight = 1;
    bool mandatory = true;
    std::vector<std::string> dependencies;
};

struct ChildSpec {
    std::string id;
    double weight = 1;
    bool mandatory = true;
};

struct Snapshot {
    State state = State::Bootstrapping;
    std::string activeId, headline = "Preparing startup...", detail = "Optional cosmetic resources";
    std::string countText, recentId, recent, warning, error;
    double fraction = 0;                 // confirmed weighted completion; never wall-clock progress
    bool determinate = false;
    int Percentage() const;
};

struct AtomicTiming {
    std::string category, slowest;
    uint64_t calls = 0;
    double totalMs = 0, maxMs = 0;
};

struct TaskTiming { std::string id; double milliseconds = 0; Outcome outcome = Outcome::Pending; };

class Reporter {
public:
    using Presenter = std::function<bool(const Snapshot&, bool force)>;
    explicit Reporter(Presenter presenter = {});
    bool Discover(const char* detail = nullptr);
    bool Register(const TaskSpec& task);
    bool Freeze(const char* finalGate = "startup.ready");
    bool Begin(const char* id, const char* detail = nullptr, int64_t total = -1, const char* unit = "");
    bool PlanChildren(const std::vector<ChildSpec>& children);
    bool ChildProgress(const char* id, double fraction);
    bool ChildDone(const char* id, Outcome outcome = Outcome::Ready);
    bool Progress(int64_t completed, const char* detail = nullptr);
    bool Fraction(double fraction, const char* detail = nullptr);
    bool Count(int64_t completed, int64_t total, const char* unit);
    bool Pulse(const char* detail = nullptr, bool force = false);
    bool Finish(Outcome outcome = Outcome::Ready, const char* warning = nullptr);
    bool Ready();
    bool Fail(const char* cause);
    void Cancel();
    bool Stopped() const { return snapshot.state == State::Failed || snapshot.state == State::Cancelled; }
    bool Cancelled() const { return snapshot.state == State::Cancelled; }
    bool Frozen() const { return frozen; }
    const Snapshot& View() const { return snapshot; }
    Outcome Result(const char* id) const;
    size_t TaskCount() const { return tasks.size(); }
    double TotalWeight() const { return totalWeight; }
    double CurrentTaskFraction() const { return active < 0 ? 0 : tasks[active].fraction; }
    void RecordAtomic(const char* category, const char* operation, double milliseconds);
    const std::vector<AtomicTiming>& Atomics() const { return atomics; }
    const std::vector<TaskTiming>& Timings() const { return timings; }

private:
    struct Child { ChildSpec spec; double fraction = 0; Outcome outcome = Outcome::Pending; };
    struct Task {
        TaskSpec spec;
        double fraction = 0;
        Outcome outcome = Outcome::Pending;
        int64_t total = -1, completed = 0;
        std::string unit;
        bool childrenPlanned = false;
        std::vector<Child> children;
    };
    std::vector<Task> tasks;
    std::vector<AtomicTiming> atomics;
    std::vector<TaskTiming> timings;
    Presenter presenter;
    Snapshot snapshot;
    int active = -1, gate = -1;
    double totalWeight = 0;
    bool frozen = false, planError = false;
    std::chrono::steady_clock::time_point taskStart;
    int Find(const char* id) const;
    bool Contract(const char* cause);
    bool Present(bool force);
    void Recalculate();
    void RecalculateChildren();
};

// Measures an indivisible library/driver call; it never redraws or awards completion.
class AtomicSpan {
public:
    AtomicSpan(Reporter* reporter, const char* category, const char* operation);
    ~AtomicSpan();
private:
    Reporter* reporter;
    std::string category, operation;
    std::chrono::steady_clock::time_point start;
};

const char* OutcomeName(Outcome outcome);
const char* StateName(State state);

} // namespace startup
