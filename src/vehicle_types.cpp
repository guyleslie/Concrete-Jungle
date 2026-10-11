#include "vehicle_types.h"
#include "datafile.h"
#include "config.h"
#include "raylib.h"
#include <cctype>

std::vector<VehicleSpec>& VehicleClasses() { static std::vector<VehicleSpec> v; return v; }

const VehicleSpec& Spec(VClass c) {
    auto& v = VehicleClasses();
    static VehicleSpec fallback;
    if (c < 0 || c >= (int)v.size()) return fallback;
    return v[c];
}

VClass FindVehicleClass(const std::string& name) {
    auto& v = VehicleClasses();
    for (size_t i = 0; i < v.size(); i++) {
        const std::string& a = v[i].name;
        if (a.size() != name.size()) continue;
        bool eq = true;
        for (size_t k = 0; k < a.size() && eq; k++) eq = tolower((unsigned char)a[k]) == tolower((unsigned char)name[k]);
        if (eq) return (int)i;
    }
    return -1;
}

void LoadVehicleClasses(const std::vector<DataRecord>& records) {
    auto& classes = VehicleClasses();
    classes.clear();
    const float M = cfg::PX_PER_METER;
    for (const DataRecord& r : records) {
        if (!r.Is("CLASS") || r.size() < 13) continue;
        VehicleSpec s;
        s.name = r[1];
        s.length = r.F(2) * M;
        s.height = r.F(3) * M;
        s.maxSpeed = r.F(4) / 3.6f * M;
        s.accel = r.F(5) * M;
        s.brake = r.F(6) * M;
        s.reverseMax = r.F(7) / 3.6f * M;
        s.steerRate = r.F(8);
        s.grip = r.F(9);
        s.mass = r.F(10);
        s.health = r.F(11);
        s.trafficWeight = r.F(12);
        for (size_t k = 13; k < r.size(); k++) {
            const std::string& f = r[k];
            if (f == "police") s.flags |= VF_POLICE;
            else if (f == "emergency") s.flags |= VF_EMERGENCY;
            else if (f == "large") s.flags |= VF_LARGE;
            else if (f == "two_wheeler") s.flags |= VF_TWO_WHEELER;
            else TraceLog(LOG_WARNING, "vehicles.cfg line %d: unknown flag '%s'", r.line, f.c_str());
        }
        int existing = FindVehicleClass(s.name);
        if (existing >= 0) classes[existing] = s; else classes.push_back(s);
    }
    // Turning circles; a class without one gets 2.3 lengths (about 11 m for a 4.8 m car).
    // Collision widths; a class without one takes its sprite's width (vehicle.cpp).
    for (const DataRecord& r : records) {
        bool turn = r.Is("TURN");
        if ((!turn && !r.Is("WIDTH")) || r.size() < 3) continue;
        int c = FindVehicleClass(r[1]);
        if (c < 0) { TraceLog(LOG_WARNING, "vehicles.cfg line %d: %s for unknown class '%s'", r.line, r[0].c_str(), r[1].c_str()); continue; }
        (turn ? classes[c].turnCircle : classes[c].width) = r.F(2) * M;
    }
    for (VehicleSpec& s : classes) if (s.turnCircle <= 0) s.turnCircle = s.length * 2.3f;
}
