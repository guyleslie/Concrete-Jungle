// =====================================================================================
//  Vehicle classes - DATA DRIVEN.
//
//  Classes are defined in assets/data/vehicles.cfg (built-in defaults are used if the
//  file is missing). To add a new kind of vehicle, add a CLASS line there and one or
//  more SPRITE (image file) or GEN (procedural) lines - no code changes needed.
//
//    CLASS <name> <length m> <height m> <top km/h> <accel m/s2> <brake m/s2>
//          <reverse km/h> <steer rad/s> <grip> <mass t> <hp> <traffic weight> [flags]
//    flags: police emergency large two_wheeler
// =====================================================================================
#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum VehicleFlag : uint32_t {
    VF_POLICE      = 1u << 0,   // used by the police response (sirens, chasing)
    VF_EMERGENCY   = 1u << 1,   // has a siren / light bar
    VF_LARGE       = 1u << 2,   // trucks & buses: slower AI, wide turns
    VF_TWO_WHEELER = 1u << 3,   // bikes: single headlight, different skids
};

struct VehicleSpec {
    std::string name;
    float    length = 66, height = 24;        // world px
    float    maxSpeed = 700, accel = 500, brake = 1300, reverseMax = 260;  // px/s, px/s^2
    float    steerRate = 3.0f, grip = 9.0f, mass = 1.0f, health = 100;
    float    trafficWeight = 1;
    uint32_t flags = 0;
    bool twoWheeler() const { return flags & VF_TWO_WHEELER; }
    bool police() const { return flags & VF_POLICE; }
    bool emergency() const { return flags & VF_EMERGENCY; }
    bool large() const { return flags & VF_LARGE; }
};

using VClass = int;   // index into VehicleClasses()

std::vector<VehicleSpec>& VehicleClasses();
const VehicleSpec&        Spec(VClass c);
VClass                    FindVehicleClass(const std::string& name);   // -1 if unknown
void                      LoadVehicleClasses();                          // reads vehicles.cfg
const char*               DefaultVehiclesCfg();                          // built-in defaults
