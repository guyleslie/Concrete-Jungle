// =====================================================================================
//  Visible, deterministic handling and collision measurements for CJ-002.
//  Targets belong to this frozen fixture, not the production vehicle configuration.
//  A failing baseline is useful evidence; missing phases and measurements never pass.
// =====================================================================================
#include "vehicle_tests.h"
#include "game.h"
#include "traffic.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr float M = cfg::PX_PER_METER;
constexpr float G = 9.81f;
constexpr float RADIUS = 40.0f * M;
constexpr float FIXED_DT = 1.0f / 60.0f;
const Vector2 ORIGIN = { 200.0f * M, 200.0f * M };
constexpr const char* FIXTURE_ID = "cj002-v1";

enum class Profile { Controlled, Sport, Utility, Heavy, Bike };
struct Target {
    const char* name;
    float mass, acceleration, braking, lateral, top;
    Profile profile;
};
const Target TARGETS[] = {
    { "Stinger", 1.45f, 6.0f, 36, 0.95f, 202, Profile::Controlled },
    { "Viper", 1.55f, 4.2f, 35, 1.00f, 214, Profile::Sport },
    { "Bruiser", 1.75f, 6.8f, 42, 0.78f, 198, Profile::Sport },
    { "Taxi", 1.65f, 11, 43, 0.75f, 162, Profile::Controlled },
    { "Pickup", 2.20f, 12, 48, 0.65f, 157, Profile::Utility },
    { "Van", 2.80f, 18, 50, 0.60f, 144, Profile::Utility },
    { "Limo", 2.60f, 14, 46, 0.68f, 160, Profile::Utility },
    { "Ambulance", 4.00f, 16, 51, 0.55f, 166, Profile::Heavy },
    { "Police", 1.85f, 6.5f, 39, 0.90f, 207, Profile::Controlled },
    { "Bus", 13, 45, 62, 0.42f, 108, Profile::Heavy },
    { "BoxTruck", 7.50f, 30, 58, 0.48f, 121, Profile::Heavy },
    { "Semi", 10, 32, 60, 0.45f, 126, Profile::Heavy },
    { "FireTruck", 15, 28, 59, 0.45f, 130, Profile::Heavy },
    { "Garbage", 16, 48, 65, 0.40f, 110, Profile::Heavy },
    { "Sportbike", 0.28f, 3.6f, 39, 0.95f, 212, Profile::Bike },
    { "Chopper", 0.40f, 6.2f, 52, 0.65f, 185, Profile::Bike },
    { "Scooter", 0.22f, 6, 13, 0.60f, 95, Profile::Bike }
};

enum class Kind {
    Accelerate, Brake, TopLow, TopHigh, Skid, RearBrake, Rest, Reverse, Coast,
    GrassBrake, GrassSkid, Glance, HeadOn, RearEnd, TBone, MassPair, RailHit,
    RailShove, FastPair, Pole, Corner, Breakaway, CapCircle, CapBox, RestWall,
    RestCorner, Wedge
};
struct Case {
    Kind kind;
    std::string name, a = "Taxi", b = "Taxi";
    int frames = 90, direction = 1, trial = 0;
    float step = FIXED_DT, speed = 50;
    bool forces = false, consequences = false, closed = false, movingB = false;
    int material = -1;
};
struct Motion {
    Vector2 momentum{};
    double energy = 0, angular = 0, momentumScale = 0;
};
Motion MeasureMotion(const std::vector<Vehicle>& cars) {
    Motion result;
    for (const Vehicle& v : cars) {
        if (!v.active) continue;
        float mass = v.S().mass;
        Vector2 p = v.vel * (mass / M);
        float inertia = VehicleYawInertia(v) / (M * M);
        result.momentum = result.momentum + p;
        result.momentumScale += Len(p);
        result.energy += 0.5 * (mass * Len2(v.vel) / (M * M) + inertia * v.angVel * v.angVel);
        result.angular += inertia * v.angVel + Cross((v.pos - ORIGIN) / M, p);
    }
    return result;
}
float Speed(float kmh) { return kmh / 3.6f * M; }
float Kmh(float px) { return px / M * 3.6f; }
float Restitution(float closing) { return 0.05f + 0.30f * expf(-closing / 120.0f); }
bool Finite(const Vehicle& v) {
    return std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.vel.x) &&
           std::isfinite(v.vel.y) && std::isfinite(v.angle) && std::isfinite(v.angVel);
}
uint32_t ConfigHash() {
    std::ifstream file("assets/data/vehicles.cfg", std::ios::binary);
    uint32_t hash = 2166136261u;
    char c;
    while (file.get(c)) { hash ^= (unsigned char)c; hash *= 16777619u; }
    return hash;
}
float Penetration(const Game& g) {
    float maximum = 0;
    for (size_t i = 0; i < g.vehicles.size(); i++) {
        const Vehicle& a = g.vehicles[i];
        if (!a.active) continue;
        Vector2 normal; float depth;
        for (size_t j = i + 1; j < g.vehicles.size(); j++)
            if (g.vehicles[j].active && OBBOverlap(a.Box(), g.vehicles[j].Box(), normal, depth)) maximum = std::max(maximum, depth);
        for (const Building& b : g.map.buildings)
            if (b.Solid() && OBBOverlap(a.Box(), MakeAABB(b.r), normal, depth)) maximum = std::max(maximum, depth);
        for (const CityObject& o : g.map.objects) {
            if (!o.alive || o.soft || o.radius <= 0) continue;
            bool hit = o.box ? OBBOverlap(a.Box(), o.Box(), normal, depth) : CircleOBB(o.pos, o.radius, a.Box(), normal, depth);
            if (hit) maximum = std::max(maximum, depth);
        }
    }
    return maximum;
}
const Target* FindTarget(const std::string& name) {
    for (const Target& target : TARGETS) if (target.name == name) return &target;
    return nullptr;
}
} // namespace

struct VehicleTests::State {
    bool collision = false, finished = false, invalid = false;
    int frame = 0, index = -1, phaseFrame = 0, checks = 0, failures = 0, completed = 0;
    int physicsSteps = 0, phaseSteps = 0, contacts = 0, deep = 0, nonfinite = 0;
    const Target* target = nullptr;
    std::vector<Case> cases;
    std::vector<Vehicle> initial;
    std::vector<Vector2> trail;
    CityObject materials[3];
    std::string capture, lastResult;
    float elapsed = 0, phaseTime = 0, path = 0, reached = -1, stopDistance = -1;
    float maxPen = 0, settledPen = 0, settledSpeed = 0, settledYaw = 0;
    float drift = 0, heading = 0, minForward = 0, peakSlip = 0, yawTravel = 0;
    float recover = -1, recoveryStable = 0, steerIntegral = 0, speedIntegral = 0;
    float sampleSpeed = 0, sampleAccel = 0, radiusError = 0, speedError = 0, lateralSum = 0, curvatureSum = 0;
    float frontVelocitySlip = 0, rearVelocitySlip = 0, frontTyreSlip = 0, rearTyreSlip = 0;
    int samples = 0, tyreSamples = 0;
    float dryBrake = -1, dryCoast = -1, grassBrake = -1, corner[2] = { -1, -1 };
    int cornerPass[2] = {}, cornerTrial[2] = {};
    float contactTime = -1, railReleased = -1, eventImpulse = 0;
    float peakEnergyRatio = 0, peakMomentumResidual = 0;
    float tboneSpin[2] = {}, tboneDelta[2] = {};
    bool broken = false, expectedBreak = false, touched = false, tunnel = false;
    bool episodeEnded = false;
    int episodes = 0;
    float contactGap = 0;
    Vector2 firstEpisodeVelocity{};
    Vector2 contactPoint{}, contactNormal{}, circleCentre = ORIGIN, previousVelocity{};
    Motion initialMotion;

    const Case& Current() const { return cases[(size_t)index]; }
    void Check(const char* metric, double value, double low, double high) {
        bool pass = std::isfinite(value) && value >= low && value <= high;
        checks++; if (!pass) failures++;
        TraceLog(LOG_INFO, "CJTEST metric fixture=%s phase=%s name=%s value=%.6f min=%.6f max=%.6f result=%s",
                 FIXTURE_ID, Current().name.c_str(), metric, value, low, high, pass ? "PASS" : "FAIL");
        if (!pass) lastResult = std::string(metric) + " FAILED";
    }
    void Near(const char* metric, double value, double expected, double fraction) {
        double tolerance = fabs(expected) * fraction;
        Check(metric, value, expected - tolerance, expected + tolerance);
    }
    void Add(const Case& c) { cases.push_back(c); }
    int Spawn(Game& g, const std::string& name, Vector2 position, float angle) {
        VClass cls = FindVehicleClass(name);
        int skin = -1;
        for (size_t i = 0; i < gAssets.vehicles.size(); i++)
            if (gAssets.vehicles[i].cls == cls && gAssets.vehicles[i].spawnable) { skin = (int)i; break; }
        if (skin < 0) { invalid = true; TraceLog(LOG_ERROR, "CJTEST missing class/sprite %s", name.c_str()); return -1; }
        Vehicle car; InitVehicle(car, skin, position, angle); car.driver = DriverType::Player;
        g.vehicles.push_back(car);
        return (int)g.vehicles.size() - 1;
    }
    void Wall(Game& g, Rectangle r) {
        Building b; b.r = r; b.height = 3 * M; g.map.buildings.push_back(b);
    }
    void MakeHandlingPlan() {
        auto phase = [&](Kind kind, const char* name, int frames) {
            Case c; c.kind = kind; c.name = name; c.a = target->name; c.frames = frames; c.forces = true; Add(c);
        };
        phase(Kind::Accelerate, "acceleration", 3600);
        phase(Kind::Brake, "service-brake", 600);
        phase(Kind::TopLow, "top-from-below", 1200);
        phase(Kind::TopHigh, "top-from-above", 1200);
        for (int side = 0; side < 2; side++) for (int trial = 0; trial < 5; trial++) {
            Case c; c.kind = Kind::Skid; c.a = target->name; c.frames = 480; c.forces = true;
            c.direction = side == 0 ? 1 : -1; c.trial = trial;
            c.name = std::string("skid-") + (side == 0 ? "right-" : "left-") + std::to_string(trial);
            Add(c);
        }
        for (int side = 0; side < 2; side++) {
            Case c; c.kind = Kind::RearBrake; c.a = target->name; c.frames = 600; c.forces = true;
            c.direction = side == 0 ? 1 : -1; c.name = side == 0 ? "rear-brake-right" : "rear-brake-left"; Add(c);
        }
        phase(Kind::Rest, "steering-at-rest", 180);
        phase(Kind::Reverse, "reverse-transition", 240);
        phase(Kind::Coast, "coast-down", 180);
        phase(Kind::GrassBrake, "grass-brake", 600);
        phase(Kind::GrassSkid, "grass-corner", 600);
    }
    void MakeCollisionPlan() {
        for (int interval = 0; interval < 2; interval++) {
            float step = interval == 0 ? FIXED_DT : 0.05f;
            std::string suffix = interval == 0 ? "-60hz" : "-20hz";
            auto add = [&](Kind kind, const char* name, bool closed = false) -> Case& {
                Case c; c.kind = kind; c.name = std::string(name) + suffix; c.step = step; c.closed = closed; Add(c); return cases.back();
            };
            add(Kind::Glance, "glancing-wall");
            add(Kind::HeadOn, "symmetric-head-on", true);
            add(Kind::RearEnd, "rear-end", true);
            add(Kind::TBone, "t-bone-front", true).direction = -1;
            add(Kind::TBone, "t-bone-rear", true).direction = 1;
            add(Kind::MassPair, "car-into-truck", true).b = "BoxTruck";
            Case& truck = add(Kind::MassPair, "truck-into-car", true); truck.b = "BoxTruck"; truck.movingB = true;
            Case& small = add(Kind::MassPair, "scooter-into-garbage", true); small.a = "Scooter"; small.b = "Garbage";
            Case& large = add(Kind::MassPair, "garbage-into-scooter", true); large.a = "Scooter"; large.b = "Garbage"; large.movingB = true;
            add(Kind::RailHit, "rail-hard-hit");
            add(Kind::RailShove, "rail-gentle-shove");
            Case& fast = add(Kind::FastPair, "opposing-fast-pair", true); fast.a = fast.b = "Viper"; fast.speed = 214;
            for (int material = 0; material < 3; material++) for (int high = 0; high < 2; high++) {
                const char* labels[] = { "lamp", "hydrant", "bollard" };
                std::string label = std::string(labels[material]) + (high ? "-high" : "-low");
                Case& c = add(Kind::Breakaway, label.c_str()); c.material = material; c.speed = high ? 60 : 1;
                if (material == 2 && high) c.a = "Garbage";
            }
            add(Kind::CapCircle, "single-point-cap");
            add(Kind::CapBox, "two-point-cap");
            for (const Target& t : TARGETS) {
                Case& pole = add(Kind::Pole, (std::string("thin-pole-") + t.name).c_str()); pole.a = t.name; pole.speed = t.top;
                Case& cornerCase = add(Kind::Corner, (std::string("building-corner-") + t.name).c_str()); cornerCase.a = t.name; cornerCase.speed = t.top;
            }
            for (Kind kind : { Kind::RestWall, Kind::RestCorner, Kind::Wedge }) {
                const char* name = kind == Kind::RestWall ? "rest-wall" : kind == Kind::RestCorner ? "rest-corner" : "wedged-cars";
                Case& c = add(kind, name); c.frames = 270; c.forces = true;
            }
            for (Kind kind : { Kind::Glance, Kind::HeadOn, Kind::TBone, Kind::MassPair }) {
                const char* name = kind == Kind::Glance ? "production-glance" : kind == Kind::HeadOn ? "production-head-on" : kind == Kind::TBone ? "production-t-bone" : "production-car-truck";
                Case& c = add(kind, name); c.frames = 180; c.forces = c.consequences = true;
                if (kind == Kind::MassPair) c.b = "BoxTruck";
            }
        }
    }
    void Start(Game& g) {
        const Case& c = Current();
        phaseFrame = phaseSteps = contacts = deep = nonfinite = 0;
        phaseTime = path = maxPen = settledPen = settledSpeed = settledYaw = 0;
        reached = stopDistance = recover = contactTime = railReleased = -1;
        drift = heading = peakSlip = yawTravel = recoveryStable = steerIntegral = speedIntegral = 0;
        minForward = 0; sampleSpeed = sampleAccel = radiusError = speedError = lateralSum = curvatureSum = 0;
        frontVelocitySlip = rearVelocitySlip = frontTyreSlip = rearTyreSlip = 0; samples = tyreSamples = 0;
        peakEnergyRatio = peakMomentumResidual = eventImpulse = 0;
        broken = expectedBreak = touched = tunnel = episodeEnded = false;
        episodes = 0; contactGap = 0; firstEpisodeVelocity = {};
        trail.clear(); g.vehicles.clear(); g.vehicles.reserve(8); g.fx.Clear();
        bool grass = c.kind == Kind::GrassBrake || c.kind == Kind::GrassSkid;
        g.map.ResetTestGround(grass ? Tile::Grass : Tile::Road);
        g.player.inVehicle = false; g.player.vehicle = -1;
        if (Spawn(g, c.a, ORIGIN, 0) < 0) return;
        if (!collision) {
            Vehicle& v = g.vehicles[0];
            float threshold = target->top < 100 ? 50 : 100;
            if (c.kind == Kind::Brake || c.kind == Kind::GrassBrake) v.vel = v.Fwd() * Speed(threshold);
            if (c.kind == Kind::TopLow) v.vel = v.Fwd() * Speed(target->top * 0.95f);
            if (c.kind == Kind::TopHigh) v.vel = v.Fwd() * Speed(target->top * 1.05f);
            if (c.kind == Kind::RearBrake) v.vel = v.Fwd() * Speed(target->profile == Profile::Bike ? 30 : 50);
            if (c.kind == Kind::Coast) v.vel = v.Fwd() * Speed(50);
            if (c.kind == Kind::Skid || c.kind == Kind::GrassSkid) {
                float lateral = c.kind == Kind::Skid ? target->lateral + (c.trial - 2) * 0.05f : target->lateral;
                v.pos = ORIGIN + V2(RADIUS, 0); v.angle = c.direction > 0 ? PI : 0;
                v.vel = v.Fwd() * (sqrtf(lateral * G * (RADIUS / M)) * M);
                v.angVel = c.direction * v.Speed() / RADIUS;
            }
        } else {
            bool pair = c.kind == Kind::HeadOn || c.kind == Kind::RearEnd || c.kind == Kind::TBone ||
                        c.kind == Kind::MassPair || c.kind == Kind::RailHit || c.kind == Kind::RailShove || c.kind == Kind::FastPair;
            if (pair && Spawn(g, c.b, ORIGIN, -PI * 0.5f) < 0) return;
            Vehicle& a = g.vehicles[0];
            a.angle = PI * 0.5f;
            a.vel = V2(Speed(c.speed), 0);
            if (pair) {
                Vehicle& b = g.vehicles[1];
                float gap = c.kind == Kind::FastPair ? 70.0f : 8.0f;
                a.pos = ORIGIN - V2(a.length * 0.5f + gap * 0.5f, 0);
                b.pos = ORIGIN + V2(b.length * 0.5f + gap * 0.5f, 0);
                if (c.kind == Kind::HeadOn || c.kind == Kind::FastPair) b.vel = V2(-Speed(c.speed), 0);
                if (c.movingB) { a.vel = {}; b.vel = V2(-Speed(c.speed), 0); }
                if (c.kind == Kind::RearEnd || c.kind == Kind::RailHit) b.angle = PI * 0.5f;
                if (c.kind == Kind::TBone) {
                    b.angle = 0; b.pos = ORIGIN;
                    a.pos = ORIGIN + V2(-b.width * 0.5f - a.length * 0.5f - 8, c.direction * 0.75f * M);
                }
                if (c.kind == Kind::RailHit) { b.driver = DriverType::Traffic; b.ai.rail = true; }
                if (c.kind == Kind::RailShove) {
                    a.pos = ORIGIN - V2(a.length * 0.5f + 0.1f, 0); a.vel = {};
                    b.pos = a.pos - V2((a.length + b.length) * 0.5f + 0.1f, 0);
                    b.angle = PI * 0.5f; b.vel = V2(10, 0); b.driver = DriverType::Traffic; b.ai.rail = true;
                    Wall(g, { ORIGIN.x, ORIGIN.y - 10 * M, 2 * M, 20 * M });
                }
            } else if (c.kind == Kind::Glance) {
                a.angle = (90 - 15) * DEG2RAD; a.vel = a.Fwd() * Speed(50);
                float extent = OBBProjectRadius(a.Box(), V2(0, 1));
                a.pos = ORIGIN + V2(-5 * M, extent + 4);
                Wall(g, { ORIGIN.x - 80 * M, ORIGIN.y - 10 * M, 160 * M, 10 * M });
            } else if (c.kind == Kind::Corner || c.kind == Kind::RestCorner) {
                a.angle = 3 * PI * 0.25f;
                a.pos = ORIGIN - Norm(V2(1, 1)) * (a.length * 0.5f + (c.kind == Kind::Corner ? 70.0f : 0.2f));
                a.vel = a.Fwd() * Speed(c.kind == Kind::Corner ? c.speed : 5);
                Wall(g, { ORIGIN.x, ORIGIN.y, 40 * M, 40 * M });
            } else if (c.kind == Kind::RestWall || c.kind == Kind::Wedge) {
                a.pos = ORIGIN - V2(a.length * 0.5f + 0.2f, 0); a.vel = V2(Speed(5), 0);
                Wall(g, { ORIGIN.x, ORIGIN.y - 10 * M, 2 * M, 20 * M });
                if (c.kind == Kind::Wedge) {
                    float length = a.length;
                    Vector2 position = a.pos;
                    Spawn(g, "Taxi", position - V2(length + 0.2f, 0), PI * 0.5f);
                    Spawn(g, "Taxi", position - V2(2 * (length + 0.2f), 0), PI * 0.5f);
                    g.vehicles.back().vel = V2(Speed(10), 0);
                }
            } else {
                CityObject object;
                if (c.kind == Kind::Breakaway) object = materials[c.material];
                else {
                    object.kind = CityObject::Pillar; object.radius = 0.1f * M; object.w = object.l = 0.2f * M;
                    object.h = 2 * M; object.strength = 0; object.mass = 0;
                    if (c.kind == Kind::CapCircle || c.kind == Kind::CapBox) {
                        object.strength = 170; object.mass = 0;
                        if (c.kind == Kind::CapBox) { object.box = true; object.w = 0.2f * M; object.l = 3 * M; object.radius = 1.6f * M; }
                    }
                }
                object.pos = ORIGIN; object.rot = 0; object.alive = true; object.fallen = false;
                float half = object.box ? object.w * 0.5f : object.radius;
                float gap = c.kind == Kind::Pole ? 70.0f : 0.1f * M;
                a.pos = ORIGIN - V2(a.length * 0.5f + half + gap, 0);
                g.map.objects.push_back(object);
                expectedBreak = object.strength > 0 && a.S().mass * a.Speed() > object.strength;
            }
            if (c.kind == Kind::RestWall || c.kind == Kind::RestCorner || c.kind == Kind::Wedge)
                for (Vehicle& v : g.vehicles) v.driver = DriverType::Parked;
        }
        g.map.RebuildTestIndex();
        initial = g.vehicles; initialMotion = MeasureMotion(g.vehicles);
        previousVelocity = g.vehicles[0].vel;
        capture = std::to_string(index) + "-" + c.name;
        TraceLog(LOG_INFO, "CJTEST phase-start index=%d/%d name=%s class=%s frames=%d physics_dt=%.8f forces=%d consequences=%d",
                 index + 1, (int)cases.size(), c.name.c_str(), c.a.c_str(), c.frames, c.step, (int)c.forces, (int)c.consequences);
        for (size_t i = 0; i < initial.size(); i++) {
            const Vehicle& v = initial[i];
            TraceLog(LOG_INFO, "CJTEST initial body=%d class=%s mass_t=%.6f inertia_t_px2=%.6f pos_px=(%.4f,%.4f) velocity_px_s=(%.4f,%.4f) heading_rad=%.6f yaw_rad_s=%.6f",
                     (int)i, v.S().name.c_str(), v.S().mass, VehicleYawInertia(v), v.pos.x, v.pos.y, v.vel.x, v.vel.y, v.angle, v.angVel);
        }
    }
    void CircleControls(Vehicle& v, float wanted, float step) {
        const Case& c = Current();
        Vector2 radial = v.pos - circleCentre;
        float theta = atan2f(radial.y, radial.x);
        float look = 6 * M + v.Speed() * 0.25f;
        float ahead = theta + c.direction * look / RADIUS;
        Vector2 aim = circleCentre + V2(cosf(ahead), sinf(ahead)) * RADIUS;
        float error = WrapAngle(AngleOf(aim - v.pos) - v.angle);
        steerIntegral = Clampf(steerIntegral + error * step * 0.6f, -0.65f, 0.65f);
        v.in.steer = Clampf(error * 2.5f + steerIntegral, -1, 1);
        float delta = wanted - Dot(v.vel, v.Fwd());
        // Integral effort balances drag without a permanent speed error. Conditional
        // integration keeps a saturated throttle from winding up during a slide.
        float rawEffort = delta / (2.0f * M) + speedIntegral;
        if ((rawEffort > -1 && rawEffort < 1) || rawEffort * delta < 0)
            speedIntegral = Clampf(speedIntegral + delta / M * step * 0.4f, -1, 1);
        float effort = Clampf(delta / (2.0f * M) + speedIntegral, -1, 1);
        v.in.throttle = std::max(0.0f, effort);
        v.in.brake = std::max(0.0f, -effort);
    }
    void Controls(Game& g, float step) {
        const Case& c = Current();
        for (Vehicle& v : g.vehicles) v.in = {};
        if (collision) {
            for (Vehicle& v : g.vehicles) if (AIOnRail(v)) {
                v.kinFrom = v.pos; v.kinFromAng = v.angle;
                v.pos = v.pos + (c.kind == Kind::RailShove ? V2(10, 0) : V2(0, 0)) * step;
            }
            return;
        }
        Vehicle& v = g.vehicles[0];
        if (c.kind == Kind::Accelerate || c.kind == Kind::TopLow || c.kind == Kind::TopHigh) v.in.throttle = 1;
        if (c.kind == Kind::Brake || c.kind == Kind::GrassBrake) v.in.brake = stopDistance < 0 ? 1.0f : 0.0f;
        if (c.kind == Kind::Rest) { v.in.steer = 1; v.in.handbrake = true; }
        if (c.kind == Kind::Reverse) { if (phaseTime < 2) v.in.brake = 1; else v.in.throttle = 1; }
        if (c.kind == Kind::Skid || c.kind == Kind::GrassSkid) {
            float lateral = c.kind == Kind::Skid ? target->lateral + (c.trial - 2) * 0.05f : target->lateral;
            CircleControls(v, sqrtf(lateral * G * (RADIUS / M)) * M, step);
        }
        if (c.kind == Kind::RearBrake) {
            v.in.steer = phaseTime < 0.6f ? c.direction * Saturate(phaseTime / 0.25f) : 0;
            v.in.handbrake = phaseTime < 0.6f;
        }
    }
    void Observe(Game& g, const std::vector<Vehicle>& before, float step) {
        const Case& c = Current();
        physicsSteps++; phaseSteps++;
        phaseTime = phaseSteps * step; // Integer ticks own durations and measurement windows.
        for (const Vehicle& v : g.vehicles) if (!Finite(v)) nonfinite++;
        if (nonfinite) { invalid = true; return; }
        float penetration = Penetration(g);
        maxPen = std::max(maxPen, penetration); if (penetration > 3) deep++;
        Vehicle& v = g.vehicles[0];
        path += Dist(v.pos, before[0].pos) / M;
        float forward = Dot(v.vel, v.Fwd());
        minForward = std::min(minForward, forward / M);
        drift = std::max(drift, fabsf(Dot(v.pos - initial[0].pos, RightOf(initial[0].angle))) / M);
        heading = std::max(heading, fabsf(WrapAngle(v.angle - initial[0].angle)) * RAD2DEG);
        if (c.kind != Kind::RearBrake || phaseFrame < 180)
            yawTravel += fabsf(WrapAngle(v.angle - before[0].angle));
        if (!collision) {
            float threshold = Speed(target->top < 100 ? 50 : 100);
            if (c.kind == Kind::Accelerate && reached < 0 && forward >= threshold) {
                float previous = Dot(before[0].vel, before[0].Fwd());
                reached = phaseTime - step + step * Saturate((threshold - previous) / std::max(0.0001f, forward - previous));
                capture = std::to_string(index) + "-target-speed";
            }
            if ((c.kind == Kind::Brake || c.kind == Kind::GrassBrake) && stopDistance < 0 && v.Speed() < 0.1f * M) {
                stopDistance = path; reached = phaseTime; capture = std::to_string(index) + "-stopped";
            }
            if (c.kind == Kind::TopLow || c.kind == Kind::TopHigh) if (phaseFrame >= 900) {
                sampleSpeed += Kmh(v.Speed()); sampleAccel += fabsf(v.Speed() - before[0].Speed()) / step / M; samples++;
            }
            if (c.kind == Kind::Skid || c.kind == Kind::GrassSkid) {
                int beginFrame = c.kind == Kind::Skid ? 300 : 420;
                if (phaseFrame >= beginFrame) {
                    float speed = v.Speed();
                    Vector2 acceleration = (v.vel - before[0].vel) / step;
                    float lateral = fabsf(Cross(Norm(v.vel), acceleration)) / M / G;
                    float radius = Dist(v.pos, circleCentre);
                    float requestedG = c.kind == Kind::Skid ? target->lateral + (c.trial - 2) * 0.05f : target->lateral;
                    float requestedSpeed = sqrtf(requestedG * G * (RADIUS / M)) * M;
                    radiusError = std::max(radiusError, fabsf(radius - RADIUS) / RADIUS);
                    speedError = std::max(speedError, fabsf(speed - requestedSpeed) / requestedSpeed);
                    lateralSum += lateral;
                    curvatureSum += speed > 1 ? fabsf(Cross(v.vel, acceleration)) / (speed * speed * speed) * M : 0;
                    sampleSpeed += Kmh(speed); samples++;
                    float axle = v.length * 0.32f;
                    Vector2 front = v.vel + Perp(v.Fwd() * axle) * v.angVel;
                    Vector2 rear = v.vel - Perp(v.Fwd() * axle) * v.angVel;
                    frontVelocitySlip += atan2f(Dot(front, RightOf(v.angle)), fabsf(Dot(front, v.Fwd())) + 0.01f) * RAD2DEG;
                    rearVelocitySlip += atan2f(Dot(rear, RightOf(v.angle)), fabsf(Dot(rear, v.Fwd())) + 0.01f) * RAD2DEG;
                    if (std::isfinite(v.frontSlipAngle) && std::isfinite(v.rearSlipAngle)) {
                        frontTyreSlip += v.frontSlipAngle * RAD2DEG; rearTyreSlip += v.rearSlipAngle * RAD2DEG; tyreSamples++;
                    }
                }
            }
            if (c.kind == Kind::RearBrake) {
                float sideways = fabsf(Dot(v.vel, RightOf(v.angle))) / M;
                if (phaseTime >= 0.6f && phaseTime <= 2.6f && v.Speed() >= M)
                    peakSlip = std::max(peakSlip, atan2f(sideways, fabsf(forward) / M) * RAD2DEG);
                if (phaseTime >= 0.6f) {
                    bool calm = sideways < 0.5f && fabsf(v.angVel) < 0.2f;
                    recoveryStable = calm ? recoveryStable + step : 0;
                    if (recover < 0 && recoveryStable >= 0.25f) recover = phaseTime - 0.6f - recoveryStable;
                }
            }
        } else {
            int newContacts = 0;
            for (const ImpactEvent& event : g.physics.events) {
                if (event.dvA <= 1e-5f && event.dvB <= 1e-5f && !event.broke) continue;
                contacts++; newContacts++; touched = true;
                if (contactTime < 0) { contactTime = phaseTime; capture = std::to_string(index) + "-first-contact"; }
                broken = broken || event.broke;
                contactPoint = event.point; contactNormal = event.normal;
                if (event.a >= 0) eventImpulse += event.dvA * g.vehicles[event.a].S().mass;
                TraceLog(LOG_INFO, "CJTEST impact phase=%s t_s=%.6f kind=%d a=%d b=%d approach_kmh=%.5f dvA_kmh=%.5f dvB_kmh=%.5f broke=%d point_px=(%.3f,%.3f)",
                         c.name.c_str(), phaseTime, (int)event.kind, event.a, event.b, Kmh(event.approach), Kmh(event.dvA), Kmh(event.dvB), (int)event.broke, event.point.x, event.point.y);
            }
            if (newContacts) {
                if (episodes == 0 || contactGap >= 0.05f) episodes++;
                contactGap = 0;
            } else if (touched) {
                contactGap += step;
                if (!episodeEnded && contactGap >= 0.05f) { episodeEnded = true; firstEpisodeVelocity = v.vel; }
            }
            if ((c.kind == Kind::RailHit || c.kind == Kind::RailShove) && railReleased < 0 && !AIOnRail(g.vehicles[1])) railReleased = phaseTime;
            if (c.closed) {
                Motion motion = MeasureMotion(g.vehicles);
                peakEnergyRatio = std::max(peakEnergyRatio, (float)(motion.energy / std::max(1e-8, initialMotion.energy)));
                peakMomentumResidual = std::max(peakMomentumResidual, (float)(Len(motion.momentum - initialMotion.momentum) / std::max(1e-8, initialMotion.momentumScale)));
            }
            if (c.kind == Kind::Pole) {
                // A centred rigid pole must remain in front of the car after contact.
                if (v.pos.x > ORIGIN.x + v.length * 0.5f + g.map.objects[0].radius) tunnel = true;
            }
            if (c.kind == Kind::FastPair && g.vehicles[0].pos.x > g.vehicles[1].pos.x) tunnel = true;
            if (phaseTime >= 1) settledPen = std::max(settledPen, penetration);
            if (phaseTime >= 3) for (const Vehicle& body : g.vehicles) {
                settledSpeed = std::max(settledSpeed, body.Speed() / M);
                settledYaw = std::max(settledYaw, fabsf(body.angVel));
            }
            if (c.consequences) g.ApplyTestImpacts(step);
        }
        for (Vehicle& car : g.vehicles) { car.speedFwd = Dot(car.vel, car.Fwd()); car.slip = fabsf(Dot(car.vel, RightOf(car.angle))); }
        previousVelocity = v.vel;
        if (phaseSteps % 3 == 0) { trail.push_back(v.pos); if (trail.size() > 1200) trail.erase(trail.begin()); }
    }
    void FinishHandling(Game& g) {
        const Case& c = Current();
        const Vehicle& v = g.vehicles[0];
        switch (c.kind) {
        case Kind::Accelerate:
            Near(target->top < 100 ? "zero_to_50_s" : "zero_to_100_s", reached, target->acceleration, 0.1);
            if (target->top < 100) TraceLog(LOG_INFO, "CJTEST metric phase=%s name=zero_to_100_s result=N/A reason=design_top_below_100", c.name.c_str());
            Near("operating_mass_t", v.S().mass, target->mass, 0.001);
            break;
        case Kind::Brake:
        case Kind::GrassBrake:
            Check("stop_observed", stopDistance >= 0 ? 1 : 0, 1, 1);
            Check("brake_lateral_drift_m", drift, 0, 0.5);
            Check("brake_heading_change_deg", heading, 0, 2);
            Check("minimum_forward_m_s", minForward, -0.1, 1e6);
            if (c.kind == Kind::Brake) {
                Near(target->top < 100 ? "fifty_to_zero_m" : "hundred_to_zero_m", stopDistance, target->braking, 0.1);
                dryBrake = stopDistance;
                if (target->top < 100) TraceLog(LOG_INFO, "CJTEST metric phase=%s name=hundred_to_zero_m result=N/A reason=design_top_below_100", c.name.c_str());
            } else {
                grassBrake = stopDistance;
                Check("grass_brake_longer_than_asphalt", stopDistance >= 0 && dryBrake >= 0 && stopDistance > dryBrake ? 1 : 0, 1, 1);
                TraceLog(LOG_INFO, "CJTEST grass braking_m=%.5f asphalt_m=%.5f", grassBrake, dryBrake);
            }
            break;
        case Kind::TopLow:
        case Kind::TopHigh:
            Check("top_speed_samples", samples, 299, 301);
            Near("steady_top_kmh", samples ? sampleSpeed / samples : -1, target->top, 0.03);
            Check("steady_abs_acceleration_m_s2", samples ? sampleAccel / samples : -1, 0, 0.05);
            break;
        case Kind::Skid: {
            int side = c.direction > 0 ? 0 : 1;
            float mean = samples ? lateralSum / samples : -1;
            float requestedG = target->lateral + (c.trial - 2) * 0.05f;
            // A slower car can trace the circle without reaching the requested tyre load.
            // Require the speed AND measured acceleration for the full observation window.
            bool valid = samples == 180 && radiusError <= 0.05f && speedError <= 0.02f && fabsf(mean - requestedG) <= 0.03f;
            cornerTrial[side]++;
            if (valid) { cornerPass[side]++; corner[side] = std::max(corner[side], mean); }
            TraceLog(LOG_INFO, "CJTEST skid phase=%s requested_g=%.4f measured_g=%.5f radius_error=%.5f speed_error=%.5f mean_kmh=%.4f curvature_m_inv=%.6f samples=%d track=%s front_velocity_slip_deg=%.4f rear_velocity_slip_deg=%.4f tyre_samples=%d front_tyre_slip_deg=%.4f rear_tyre_slip_deg=%.4f",
                     c.name.c_str(), requestedG, mean, radiusError, speedError, sampleSpeed / std::max(1, samples), curvatureSum / std::max(1, samples), samples, valid ? "PASS" : "FAIL",
                     frontVelocitySlip / std::max(1, samples), rearVelocitySlip / std::max(1, samples), tyreSamples,
                     tyreSamples ? frontTyreSlip / tyreSamples : NAN, tyreSamples ? rearTyreSlip / tyreSamples : NAN);
            Check("skid_measurement_samples", samples, 180, 180);
            Check("tyre_slip_measurement_samples", tyreSamples, 180, 180);
            if (c.trial == 4) {
                Check("skid_sweep_bracketed", cornerPass[side] > 0 && cornerPass[side] < cornerTrial[side] ? 1 : 0, 1, 1);
                Check("usable_cornering_g", corner[side], target->lateral - 0.05f, target->lateral + 0.05f);
            }
            break;
        }
        case Kind::RearBrake: {
            float low = 0, high = 0;
            switch (target->profile) {
            case Profile::Sport: low = 20; high = 50; break;
            case Profile::Controlled: low = 12; high = 35; break;
            case Profile::Utility: low = 8; high = 25; break;
            case Profile::Heavy: high = 15; break;
            case Profile::Bike: high = 20; break;
            }
            Check("rear_brake_peak_sideslip_deg", peakSlip, low, high);
            Check("rear_brake_recovery_s", recover, 0, 4);
            Check("rear_brake_yaw_travel_deg", yawTravel * RAD2DEG, 0, 180);
            break;
        }
        case Kind::Rest:
            Check("steer_at_rest_travel_m", path, 0, 0.01);
            Check("steer_at_rest_heading_deg", heading, 0, 0.1); break;
        case Kind::Reverse:
            Check("deliberate_reverse_observed", minForward < -0.1f ? 1 : 0, 1, 1);
            Check("reverse_speed_m_s", -minForward, 0, target->profile == Profile::Bike ? 2.0f : 8.5f); break;
        case Kind::Coast:
            dryCoast = v.Speed() / M;
            Check("coasting_speed_m_s", dryCoast, 0.1, 50.0 / 3.6); break;
        case Kind::GrassSkid:
            Check("grass_corner_samples", samples, 179, 181);
            Check("grass_corner_less_than_asphalt", samples && corner[0] > 0 && lateralSum / samples < corner[0] ? 1 : 0, 1, 1);
            TraceLog(LOG_INFO, "CJTEST grass cornering_g=%.5f asphalt_g=%.5f radius_error=%.5f", lateralSum / std::max(1, samples), corner[0], radiusError); break;
        default: break;
        }
    }
    void FinishCollision(Game& g) {
        const Case& c = Current();
        const Vehicle& a = g.vehicles[0];
        Motion finalMotion = MeasureMotion(g.vehicles);
        Check("contact_observed", touched ? 1 : 0, 1, 1);
        Check("tunnel_count", tunnel ? 1 : 0, 0, 0);
        if (c.closed) {
            Check("peak_energy_ratio", peakEnergyRatio, 0, 1.01);
            Check("peak_momentum_residual", peakMomentumResidual, 0, 0.02);
        }
        TraceLog(LOG_INFO, "CJTEST conservation phase=%s closed=%d energy_initial_kj=%.8f energy_final_kj=%.8f momentum_initial=(%.6f,%.6f) momentum_final=(%.6f,%.6f) angular_initial=%.8f angular_final=%.8f external_impulse=(%.6f,%.6f)",
                 c.name.c_str(), (int)c.closed, initialMotion.energy, finalMotion.energy, initialMotion.momentum.x, initialMotion.momentum.y,
                 finalMotion.momentum.x, finalMotion.momentum.y, initialMotion.angular, finalMotion.angular,
                 finalMotion.momentum.x - initialMotion.momentum.x, finalMotion.momentum.y - initialMotion.momentum.y);
        if (!c.consequences) switch (c.kind) {
        case Kind::Glance:
            Check("first_contact_episode_complete", episodeEnded ? 1 : 0, 1, 1);
            Check("tangential_speed_retained", episodeEnded ? firstEpisodeVelocity.x / initial[0].vel.x : -1, 0.70, 1.01);
            Check("glancing_contact_episodes", episodes, 1, 1); break;
        case Kind::HeadOn: {
            const Vehicle& b = g.vehicles[1];
            float expected = (1 + Restitution(2 * Speed(c.speed))) * Speed(c.speed);
            Near("head_on_delta_v_a_kmh", Kmh(Len(a.vel - initial[0].vel)), Kmh(expected), 0.05);
            Near("head_on_delta_v_b_kmh", Kmh(Len(b.vel - initial[1].vel)), Kmh(expected), 0.05);
            Check("centre_of_mass_speed_m_s", Len(finalMotion.momentum) / (a.S().mass + b.S().mass), 0, 0.1);
            Check("symmetric_yaw_rad_s", std::max(fabsf(a.angVel), fabsf(b.angVel)), 0, 0.1); break;
        }
        case Kind::RearEnd:
        case Kind::RailHit: {
            const Vehicle& b = g.vehicles[1];
            float e = Restitution(Speed(c.speed));
            Near("rear_end_final_a_kmh", Kmh(a.vel.x), c.speed * (1 - e) * 0.5f, 0.05);
            Near("rear_end_final_b_kmh", Kmh(b.vel.x), c.speed * (1 + e) * 0.5f, 0.05);
            if (c.kind == Kind::RailHit) Check("rail_released_before_contact", railReleased, 0, contactTime + 0.0001f);
            break;
        }
        case Kind::TBone: {
            const Vehicle& b = g.vehicles[1];
            Check("offset_spin_sign", b.angVel * -c.direction, 0.0001, 1e6);
            int side = c.direction < 0 ? 0 : 1;
            tboneSpin[side] = fabsf(b.angVel); tboneDelta[side] = Len(b.vel - initial[1].vel);
            if (side == 1) {
                Near("mirrored_spin_rad_s", tboneSpin[1], tboneSpin[0], 0.05);
                Near("mirrored_delta_v_kmh", Kmh(tboneDelta[1]), Kmh(tboneDelta[0]), 0.05);
            }
            break;
        }
        case Kind::MassPair: {
            const Vehicle& b = g.vehicles[1];
            float dvA = Len(a.vel - initial[0].vel), dvB = Len(b.vel - initial[1].vel);
            Near("delta_v_mass_ratio", dvB > 0 ? dvA / dvB : -1, b.S().mass / a.S().mass, 0.05); break;
        }
        case Kind::RailShove:
            Check("rail_release_after_touch_s", railReleased >= 0 && contactTime >= 0 ? railReleased - contactTime : -1, 0, 0.35f + c.step + 0.0001f); break;
        case Kind::Breakaway:
        case Kind::CapCircle:
        case Kind::CapBox:
            Check("breakaway_outcome", broken == expectedBreak ? 1 : 0, 1, 1);
            Check("breakaway_impulse_cap", eventImpulse, 0, g.map.objects[0].strength * 1.01f);
            Check("breakaway_energy_ratio", finalMotion.energy / std::max(1e-8, initialMotion.energy), 0, 1.01);
            TraceLog(LOG_INFO, "CJTEST breakaway expected=%d actual=%d transferred_t_px_s=%.6f cap_t_px_s=%.6f", (int)expectedBreak, (int)broken, eventImpulse, g.map.objects[0].strength); break;
        case Kind::RestWall:
        case Kind::RestCorner:
        case Kind::Wedge:
            Check("penetration_after_one_s_px", settledPen, 0, 0.5);
            Check("speed_after_three_s_m_s", settledSpeed, 0, 0.02);
            Check("yaw_after_three_s_rad_s", settledYaw, 0, 0.02); break;
        default: break;
        }
        for (size_t i = 0; i < g.vehicles.size(); i++) {
            const Vehicle& v = g.vehicles[i];
            TraceLog(LOG_INFO, "CJTEST final phase=%s body=%d class=%s speed_kmh=%.5f yaw_rad_s=%.6f health=%.4f burning=%d wrecked=%d",
                     c.name.c_str(), (int)i, v.S().name.c_str(), Kmh(v.Speed()), v.angVel, v.health, (int)v.burning, (int)v.wrecked);
        }
    }
    void Finish(Game& g) {
        Check("finite_state_violations", nonfinite, 0, 0);
        Check("deep_overlap_steps", deep, 0, 0);
        if (collision) FinishCollision(g); else FinishHandling(g);
        TraceLog(LOG_INFO, "CJTEST phase-end name=%s simulation_s=%.6f physics_steps=%d max_penetration_px=%.6f contacts=%d total_failures=%d",
                 Current().name.c_str(), phaseTime, phaseSteps, maxPen, contacts, failures);
        completed++;
    }
};

VehicleTests::VehicleTests() = default;
VehicleTests::~VehicleTests() = default;

bool VehicleTests::Init(Game& g, const char* scenario, const char* className) {
    std::string name = scenario ? scenario : "";
    if (name != "handling" && name != "crash-handling") return false;
    state = std::make_unique<State>();
    State& s = *state; s.collision = name == "crash-handling";
    // Snapshot the production material definitions before replacing the city fixture.
    for (int i = 0; i < 3; i++) {
        CityObject fallback;
        fallback.kind = i == 0 ? CityObject::Lamp : CityObject::Prop;
        fallback.sprite = i == 1 ? (int)spritegen::Prop::Hydrant : (int)spritegen::Prop::Bollard;
        fallback.radius = 0.2f * M; fallback.w = fallback.l = 0.4f * M; fallback.h = 2 * M;
        fallback.strength = i == 0 ? 170.0f : i == 1 ? 90.0f : 600.0f;
        fallback.mass = i == 0 ? 0.15f : i == 1 ? 0.12f : 0.1f;
        s.materials[i] = fallback;
        for (const CityObject& object : g.map.objects) {
            bool match = i == 0 ? object.kind == CityObject::Lamp :
                         object.kind == CityObject::Prop && object.sprite == fallback.sprite;
            if (match) { s.materials[i] = object; break; }
        }
    }
    if (s.collision) s.MakeCollisionPlan();
    else {
        s.target = FindTarget(className && *className ? className : "Taxi");
        if (!s.target) { s.invalid = true; TraceLog(LOG_ERROR, "CJTEST unknown handling class '%s'", className ? className : ""); return true; }
        s.MakeHandlingPlan();
    }
    g.peds.clear(); g.pickups.clear(); g.heat = 0; GRng().s = 0xC0FFEEu;
    const char* revision = std::getenv("CJ_TEST_REVISION");
    TraceLog(LOG_INFO, "CJTEST run fixture=%s scenario=%s revision=%s config_fnv1a=%08x seed=00c0ffee class=%s planned_cases=%d render_dt=%.8f build=%s_%s",
             FIXTURE_ID, name.c_str(), revision ? revision : "UNRECORDED", ConfigHash(), s.target ? s.target->name : "all", (int)s.cases.size(), FIXED_DT, __DATE__, __TIME__);
    int scheduled = 0;
    for (size_t i = 0; i < s.cases.size(); i++) {
        const Case& c = s.cases[i]; scheduled += c.frames;
        TraceLog(LOG_INFO, "CJTEST scheduled index=%d phase=%s frames=%d simulation_s=%.3f physics_dt=%.8f", (int)i + 1, c.name.c_str(), c.frames, c.frames * FIXED_DT, c.step);
    }
    TraceLog(LOG_INFO, "CJTEST schedule render_frames=%d simulation_s=%.3f budget_frames=14400", scheduled, scheduled * FIXED_DT);
    if (scheduled > 14400) { s.invalid = true; TraceLog(LOG_ERROR, "CJTEST schedule exceeds approved frame budget"); return true; }
    s.index = 0; s.Start(g);
    return true;
}

void VehicleTests::Update(Game& g, float dt) {
    if (!state || state->finished || state->invalid) return;
    State& s = *state;
    if (fabsf(dt - FIXED_DT) > 1e-6f) { s.invalid = true; TraceLog(LOG_ERROR, "CJTEST requires the fixed 1/60 s render clock"); return; }
    s.frame++; s.elapsed += dt;
    const Case& c = s.Current();
    int divisor = c.step > 0.03f ? 3 : 1;
    if ((s.phaseFrame + 1) % divisor == 0) {
        std::vector<Vehicle> before = g.vehicles;
        s.Controls(g, c.step);
        PhysicsStepOptions options; options.disableForces = !c.forces; options.disableWorldEdges = true;
        g.physics.Step(g, c.step, options);
        s.Observe(g, before, c.step);
    }
    s.phaseFrame++;
    if (s.phaseFrame >= c.frames) {
        s.Finish(g); s.index++;
        if (s.index >= (int)s.cases.size()) { s.finished = true; s.index = (int)s.cases.size() - 1; s.capture = "complete"; }
        else s.Start(g);
    }
}

void VehicleTests::Draw(const Game& g) const {
    if (!state) return;
    const State& s = *state;
    ClearBackground({ 20, 25, 30, 255 });
    if (s.index < 0 || g.vehicles.empty()) {
        DrawText("CJ-002 fixture could not initialise. See the log.", 50, 80, 28, RED); return;
    }
    const Case& c = s.Current();
    bool circle = c.kind == Kind::Skid || c.kind == Kind::GrassSkid;
    Vector2 focus = circle ? ORIGIN : g.vehicles[0].pos;
    if (s.collision) { focus = ORIGIN * 0.35f + focus * 0.65f; }
    float zoom = circle ? 0.48f : 1.0f;
    Camera2D camera{}; camera.target = focus; camera.offset = V2(GetScreenWidth() * 0.5f, GetScreenHeight() * 0.58f); camera.zoom = zoom;
    BeginMode2D(camera);
    float halfW = GetScreenWidth() / zoom, halfH = GetScreenHeight() / zoom;
    Texture2D ground = c.kind == Kind::GrassBrake || c.kind == Kind::GrassSkid ? gAssets.grass : gAssets.asphalt;
    float tile = 16 * M;
    int x0 = (int)floorf((focus.x - halfW) / tile), x1 = (int)ceilf((focus.x + halfW) / tile);
    int y0 = (int)floorf((focus.y - halfH) / tile), y1 = (int)ceilf((focus.y + halfH) / tile);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
        DrawTexturePro(ground, { 0, 0, (float)ground.width, (float)ground.height }, { x * tile, y * tile, tile, tile }, {}, 0, { 165, 175, 185, 255 });
    float grid = 10 * M;
    for (float x = floorf((focus.x - halfW) / grid) * grid; x < focus.x + halfW; x += grid)
        DrawLineV(V2(x, focus.y - halfH), V2(x, focus.y + halfH), { 140, 160, 170, 45 });
    for (float y = floorf((focus.y - halfH) / grid) * grid; y < focus.y + halfH; y += grid)
        DrawLineV(V2(focus.x - halfW, y), V2(focus.x + halfW, y), { 140, 160, 170, 45 });
    if (circle) {
        DrawCircleLinesV(ORIGIN, RADIUS, { 230, 230, 210, 210 });
        DrawCircleLinesV(ORIGIN, RADIUS * 0.95f, { 70, 185, 135, 120 });
        DrawCircleLinesV(ORIGIN, RADIUS * 1.05f, { 70, 185, 135, 120 });
    }
    for (const Building& building : g.map.buildings) {
        DrawRectangleRec(building.r, { 80, 91, 104, 255 });
        DrawRectangleLinesEx(building.r, 2, { 180, 195, 205, 255 });
    }
    for (const CityObject& object : g.map.objects) {
        Color tint = object.alive ? Color{ 245, 190, 65, 255 } : Color{ 155, 75, 55, 180 };
        if (object.box) DrawRectanglePro({ object.pos.x, object.pos.y, object.w, object.l }, V2(object.w * 0.5f, object.l * 0.5f), object.rot * RAD2DEG, tint);
        else DrawCircleV(object.pos, std::max(3.0f, object.radius), tint);
    }
    for (size_t i = 1; i < s.trail.size(); i++) DrawLineEx(s.trail[i - 1], s.trail[i], 1.5f / zoom, { 76, 210, 194, 150 });
    for (const Vehicle& v : g.vehicles) {
        const VehicleSprite& sprite = gAssets.vehicles[v.skin];
        DrawTexturePro(sprite.tex, sprite.src, { v.pos.x + 4, v.pos.y + 5, v.width, v.length }, V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, { 0, 0, 0, 130 });
        DrawTexturePro(sprite.tex, sprite.src, { v.pos.x, v.pos.y, v.width, v.length }, V2(v.width * 0.5f, v.length * 0.5f), v.angle * RAD2DEG, WHITE);
        DrawLineEx(v.pos, v.pos + v.vel * 0.2f, 1.5f / zoom, { 85, 190, 255, 200 });
    }
    if (s.touched) { DrawCircleV(s.contactPoint, 4 / zoom, YELLOW); DrawLineEx(s.contactPoint, s.contactPoint + s.contactNormal * 35, 2 / zoom, YELLOW); }
    EndMode2D();
    DrawRectangle(0, 0, GetScreenWidth(), 160, { 12, 17, 23, 238 });
    DrawUIText("CONCRETE JUNGLE / CJ-002 MEASUREMENT", 28, 15, 26, { 215, 231, 241, 255 }, true);
    DrawUIText(TextFormat("%s   |   %s   |   case %d / %d", c.a.c_str(), c.name.c_str(), s.index + 1, (int)s.cases.size()), 28, 51, 24, WHITE);
    const Vehicle& v = g.vehicles[0];
    DrawUIText(TextFormat("%.1f km/h    yaw %.3f rad/s    phase %.2f s    physics %.0f Hz    penetration %.3f px",
                         Kmh(v.Speed()), v.angVel, s.phaseTime, 1.0f / c.step, s.maxPen), 28, 88, 21, { 151, 198, 211, 255 });
    DrawUIText(TextFormat("%s   |   checks %d   failures %d   |   10 m grid / production sprites / 16 px = 1 m",
                         s.finished ? "COMPLETE" : s.invalid ? "INVALID" : "MEASURING", s.checks, s.failures), 28, 122, 19,
               s.failures || s.invalid ? Color{ 255, 151, 115, 255 } : Color{ 116, 217, 165, 255 });
    DrawRectangle(0, GetScreenHeight() - 38, GetScreenWidth(), 38, { 12, 17, 23, 230 });
    DrawUIText(s.lastResult.empty() ? "The baseline is expected to expose failures. Results are recorded per class and per case." : s.lastResult.c_str(), 25, GetScreenHeight() - 29, 18, { 220, 225, 230, 255 });
}

void VehicleTests::Log() const {
    if (!state) return;
    const State& s = *state;
    bool pass = s.finished && !s.invalid && s.failures == 0;
    TraceLog(LOG_INFO, "CJTEST summary fixture=%s class=%s scheduled=%d completed=%d render_frames=%d physics_steps=%d simulated_s=%.3f checks=%d failures=%d incomplete=%d invalid=%d result=%s",
             FIXTURE_ID, s.target ? s.target->name : "all", (int)s.cases.size(), s.completed, s.frame, s.physicsSteps,
             s.frame * FIXED_DT, s.checks, s.failures, !s.finished, (int)s.invalid, pass ? "PASS" : "FAIL");
}
bool VehicleTests::Failed() const { return state && (state->invalid || state->failures > 0 || !state->finished); }
bool VehicleTests::Finished() const { return state && state->finished; }
const char* VehicleTests::CaptureLabel() const { return state && !state->capture.empty() ? state->capture.c_str() : nullptr; }
void VehicleTests::ClearCaptureRequest() { if (state) state->capture.clear(); }
