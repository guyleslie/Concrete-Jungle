// =====================================================================================
//  Bounded physical recovery for traffic knocked off its lane. This header is usable
//  from vehicle.h; the planner sets controls and never writes a real vehicle's pose.
// =====================================================================================
#pragma once
#include "raylib.h"
#include <cstdint>

class Game;

enum class RecoveryReason : uint8_t {
    Assessing, Forward, Reverse, GearChange, NoFeasibleManoeuvre, Hazard, Disabled
};

// Rejoin diagnostics distinguish real contact from the configured clearance margin.
enum class RejoinCause : uint8_t {
    Unavailable, MissingSnapshot, Capacity, InitialContact, InitialClearance,
    UnsafeSweep, IncompleteStop, Clear
};

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
};

struct RecoveryStats {
    int frames = 0, plans = 0, rejected = 0, holds = 0;
    double averageMs = 0, p95Ms = 0, worstMs = 0;
    int percentileSamples = 0;
};

// Capture once before ANY driver updates; fixtures call this before their AI step.
void RecoveryBeginFrame(Game& g);
void RecoveryDrive(Game& g, int idx, Vector2 laneOrigin, Vector2 laneForward, float dt);
bool RecoveryCanRejoin(Game& g, int idx, float dt);
void RecoveryReset(RecoveryState& state);
const char* RecoveryReasonText(RecoveryReason reason);
const char* RejoinCauseText(RejoinCause cause);
void RecoveryResetStats();
RecoveryStats RecoveryGetStats();
void RecoveryLogStats();
void RecoveryRecordDecisionTime(double ms);  // full driver stage, recorded once before physics
RecoveryStats RecoveryGetDecisionStats();
