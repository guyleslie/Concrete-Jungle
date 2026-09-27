#include "vehicle_types.h"
#include "datafile.h"
#include "config.h"
#include "raylib.h"
#include <cctype>

// Built-in copy of assets/data/vehicles.cfg (used if the file is missing).
static const char* DEFAULT_CFG = R"(
# name       len  h     km/h  acc   brake rev  steer grip  mass hp   weight flags
CLASS Stinger   4.6 1.25 202  43    90    63   3.1   10    1.0  100  10
CLASS Viper     4.6 1.20 214  45.5  90    63   3.2   9.5   1.0  95   4
CLASS Bruiser   4.8 1.35 198  43.5  81    63   2.9   7.0   1.15 115  10
CLASS Taxi      4.8 1.50 162  31    81    58   2.9   9.0   1.1  110  12
CLASS Pickup    5.3 1.90 157  30    75    56   2.6   8.0   1.5  130  10
CLASS Van       5.0 2.00 144  26    72    54   2.4   8.0   1.6  140  10
CLASS Limo      6.8 1.45 160  26    72    45   2.2   8.5   2.0  150  2
CLASS Ambulance 6.0 2.60 166  31    78    54   2.5   8.5   1.8  150  2   emergency
CLASS Police    4.9 1.50 207  45    94    67   3.2   10    1.25 150  0   police emergency
CLASS Bus       12.0 3.2 108  15    56    40   1.6   9.0   4.5  320  5   large
CLASS BoxTruck  7.5 3.50 121  20    59    45   2.0   9.0   2.8  230  6   large
CLASS Semi      8.8 3.60 126  18    56    40   1.8   9.0   3.5  270  4   large
CLASS FireTruck 8.2 3.30 130  20    60    40   1.9   9.0   3.8  300  1   large emergency
CLASS Garbage   8.0 3.40 110  16    55    40   1.8   9.0   3.6  280  3   large
CLASS Sportbike 2.1 1.25 212  54    94    34   3.8   12    0.45 55   3   two_wheeler
CLASS Chopper   2.4 1.20 185  45    85    30   3.4   11    0.5  60   2   two_wheeler
CLASS Scooter   1.8 1.20 95   30    70    20   3.6   12    0.3  45   3   two_wheeler
)";

const char* DefaultVehiclesCfg() { return DEFAULT_CFG; }

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

void LoadVehicleClasses() {
    auto& classes = VehicleClasses();
    classes.clear();
    const float M = cfg::PX_PER_METER;
    for (const DataRecord& r : ReadDataFile("assets/data/vehicles.cfg", DEFAULT_CFG)) {
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
    if (classes.empty()) classes.push_back(VehicleSpec{ "Default" });
}
