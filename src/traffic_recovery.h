// =====================================================================================
//  Bounded physical recovery for traffic knocked off its lane. This header is usable
//  from vehicle.h; the planner sets controls and never writes a real vehicle's pose.
// =====================================================================================
#pragma once
#include "raylib.h"
#include <array>
#include <cstdint>

class Game;
struct Vehicle;

enum class RecoveryReason : uint8_t {
    Assessing, Forward, Reverse, GearChange, NoFeasibleManoeuvre, Hazard, Disabled,
    Queued                                   // aligned behind a stopped vehicle in the lane
};

// Rejoin diagnostics distinguish real contact from the configured clearance margin.
enum class RejoinCause : uint8_t {
    Unavailable, MissingSnapshot, Capacity, InitialContact, InitialClearance,
    UnsafeSweep, IncompleteStop, Clear,
    QueuedBehind                             // aligned and stopped behind a stopped vehicle: rail queue rules apply
};

// One evaluated control rollout of a planning job.
struct RecoveryCandidate {
    int gear = 0;
    float steer = 0, secondSteer = 0, switchTime = 0;
    bool safe = false;
    bool tracking = false;
    bool terminalAligned = false;
    float score = -1e9f, distance = 0, headingGain = 0, lateralGain = 0, laneImprovement = 0;
    int blocker = -1;                        // observed vehicle that rejected the rollout, if any
};
constexpr int RECOVERY_CANDIDATES = 20;      // hold, lane tracking and 18 physical arcs
constexpr int RECOVERY_PERSON_ID = 100000;   // blocker ids above this are people

struct RecoveryState {
    bool initialized = false;
    Vector2 origin{}, forward{};             // frozen lane corridor, not a moving target
    float nextPlan = 0, commit = 0;
    int gear = 0;                            // -1 reverse, 0 hold, +1 forward
    float steer = 0;
    bool tracking = false;                   // committed lane feedback, evaluated each step
    bool rejoinForecastTested = false, rejoinForecastClear = false; // latest rejoin assessment
    RejoinCause rejoinCause = RejoinCause::Unavailable;
    RecoveryReason reason = RecoveryReason::Assessing;
    int plans = 0, rejected = 0, holds = 0;
    float travelled = 0, reverseDistance = 0, lastProgress = 0;
    float stalled = 0, lastLaneError = 0;
    Vector2 lastPos{};
    // Resumable planning job. Rollouts are sliced across frames under a shared step
    // budget; the oldest waiting driver is served first. Meanwhile the committed move
    // continues only while the per-frame immediate check passes, otherwise it holds.
    bool planning = false;
    int planNext = 0, planBest = -1, planWait = 0;
    std::array<RecoveryCandidate, RECOVERY_CANDIDATES> candidates{};
    // A hold is replanned only after its neighbourhood or own pose changes.
    uint64_t holdSignature = 0;
    float holdAge = 0;                       // s since the last full search ended in a hold
    // The last full immediate check validated a few extra frames of the committed move.
    // They are used only while the car and every actor that could reach it in the check
    // horizon move as forecast; any deviation or new actor triggers a full check.
    int checkFrames = 0, checkStep = 0, checkGear = 0, checkActors = 0;
    float checkSteer = 0;
    bool checkTracking = false;
    std::array<Vector2, 4> checkPos{};
    std::array<float, 4> checkAngle{};
    std::array<int, 32> checkId{};
    std::array<Vector2, 32> checkActorPos{}, checkActorVel{};
    // The vehicle that rejected most rollouts of the last hold (-1: geometry/people).
    // It is the wait-for edge used to detect a mutual blockage with a rail car.
    int blockedBy = -1;
    // Actors that rejected rollouts of the current/last job: vehicle index + 1, or
    // RECOVERY_PERSON_ID + person index + 1. A hold is replanned when one of them moves.
    std::array<int, 6> blockIds{}, blockVotes{};
    // Making room (a role in a wait-for cycle): short checked creeps that move the car
    // away from the driver waiting on it, then a hold until that driver is free.
    int roomFor = -1;                        // the vehicle given room; -1: no role
    bool roomPlanning = false;
    int roomNext = 0, roomBest = -1, roomCreeps = 0;
    float roomBestGain = 0, roomTime = 0, roomRetry = 0;
    float moveLeft = -1;                     // s the committed move still lasts; -1: open-ended
};

struct RecoveryStats {
    int frames = 0, plans = 0, rejected = 0, holds = 0;
    int deferredFrames = 0, unchangedHolds = 0, maxWaitFrames = 0, coveredChecks = 0;
    double averageMs = 0, p95Ms = 0, worstMs = 0;
    int percentileSamples = 0;
};

// Validated numeric records "<type> <name> <value>" from the traffic data file
// (assets/data/traffic.cfg unless a measurement run selects another).
extern const char* gTrafficConfigPath;
struct TrafficField { const char* name; float* value; float low, high; };
void LoadTrafficRecords(const char* type, TrafficField* fields, int count);

// Capture once before ANY driver updates; fixtures call this before their AI step.
void RecoveryBeginFrame(Game& g);
void RecoveryDrive(Game& g, int idx, Vector2 laneOrigin, Vector2 laneForward, float dt);
bool RecoveryCanRejoin(Game& g, int idx, float dt);
// Free distance (px) the knocked car can move straight along its axis away from
// 'other', from the common snapshot: how much room it could make.
float RecoveryFreeRoom(Game& g, int idx, int other);
// Take / leave the making-room role for 'other' (a driver waiting on this car).
void RecoveryStartRoom(Vehicle& v, int other);
void RecoveryEndRoom(Vehicle& v);
void RecoveryReset(RecoveryState& state);
const char* RecoveryReasonText(RecoveryReason reason);
const char* RejoinCauseText(RejoinCause cause);
void RecoveryResetStats();
RecoveryStats RecoveryGetStats();
void RecoveryLogStats();
void RecoveryRecordDecisionTime(double ms);  // full driver stage, recorded once before physics
RecoveryStats RecoveryGetDecisionStats();
// Where the driver decision time goes: accumulated per stage, logged per frame.
enum class DecisionStage : uint8_t { Cleanup, Grid, Snapshot, Rail, Knocked, Police, COUNT };
void DecisionStageAdd(DecisionStage stage, double ms);
