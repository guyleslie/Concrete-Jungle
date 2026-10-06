// =====================================================================================
//  Driver incidents (CJ-016): a collision can make an aggressive driver stop, get out
//  on a safe side, walk to the other driver, argue, and possibly fight; afterwards the
//  same person walks back and drives the same car on. Every step is a visible action
//  with an explicit cause; nothing is decided by a timer that removes or moves anyone.
//
//  Ownership: the car keeps a handle (index + serial) to its driver on foot, the
//  driver keeps a handle to the car, so recycled slots can never become someone else's
//  car or opponent. A car whose driver is out stays protected from recycling until the
//  driver is back, or until an explicit reason (car lost, driver dead) ends it.
//
//  Tuning: INCIDENT records in assets/data/traffic.cfg.
// =====================================================================================
#pragma once
#include <cstdint>

class Game;
struct Vehicle;
struct Pedestrian;
struct ImpactEvent;

enum class DriverMood : uint8_t { Calm, Normal, Aggressive };

struct IncidentStats {
    int started = 0, exits = 0, confrontations = 0, fights = 0, returns = 0;
    int noSafeExit = 0, carLost = 0, driverDead = 0, interrupted = 0, ignoredCalm = 0;
    int shouts = 0;                          // shouts by drivers who got out
};

void IncidentsReset();
// Personality of a newly placed traffic driver, drawn from the configured shares.
void IncidentAssignMood(Vehicle& v);
// A vehicle-vehicle contact: may start an incident between the two drivers.
void IncidentOnImpact(Game& g, const ImpactEvent& e);
// Once per frame after the impacts, before pedestrian AI.
void IncidentsUpdate(Game& g, float dt);
// The driver is stopping for an incident (rail and recovery controllers hold the car).
bool IncidentHoldsVehicle(const Vehicle& v);
// Pedestrian AI hooks for drivers on foot.
bool IncidentFoePos(const Game& g, const Pedestrian& p, float* reach, float* x, float* y);
bool IncidentCarDoor(const Game& g, const Pedestrian& p, float* x, float* y);
void IncidentPunch(Game& g, int attacker);
const char* DriverMoodText(DriverMood mood);
IncidentStats IncidentGetStats();
int IncidentActiveCount();
void IncidentLogStats();
