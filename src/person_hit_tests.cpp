// =====================================================================================
//  Person-hit scenes (CJ-035). A test file describes scenes on the empty test road:
//  vehicles (a class, or every class in turn) driving at a speed until they first touch
//  a person, then braking or keeping on; people standing, watching, lying, dead, the
//  player, in rows or as a random crowd. Each frame runs the game's own steps in its
//  order - the player on foot, physics, impacts, vehicle-person contacts, people - and
//  measures every person:
//    - contact: how long a vehicle stays in touch and how far it carries the person;
//    - overlap: frames a living person on the ground stays inside a vehicle's box;
//    - the impact: the speed of the vehicle, the person's launch speed, the momentum
//      each body gained or lost in the contact step;
//    - flight, rest (the throw distance from the point of impact), falling, death,
//      getting up, and the speed vehicles lose driving over people;
//    - how it looks: never lying in the air, tumbling rather than gliding along the road.
//  The named checks compare them with the published measurements of the CJ-035
//  specification (docs/backlog.md#cj-035-vehicles-hitting-people); EXPECT gives a
//  scene-wide share a range. INJURY records sweep the injury function on its own.
// =====================================================================================
#include "person_hit_tests.h"
#include "game.h"
#include "datafile.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr const char* FIXTURE_ID = "cj035-person-hits-v1";
constexpr uint32_t SEED = 0x000c0035u;
constexpr float DT = 1.0f / 60.0f;
constexpr float M = cfg::M;
constexpr float KMH = M / 3.6f;                  // px/s per km/h
constexpr float GRAVITY = 9.81f * M;             // px/s^2
constexpr float REST_SPEED = 0.05f * M;          // px/s: slower than this, a body is at rest
constexpr float SETTLE_S = 0.5f;                 // a scene ends this long after everything settled
constexpr float VIEW_M = 24.0f;

// Reference values the game must reproduce: published measurements, not the game's data.
constexpr float PERSON_KG = 75.0f;
constexpr float SEARLE_MU = 0.7f;                         // a body sliding on a dry road
constexpr float CAR_FRONT = 1.0f * M;                     // Happer's cars: a front edge below this
constexpr float TEFFT_V50 = 67.6f, TEFFT_SLOPE = 0.078f;  // Tefft 2011, deaths over impact speed (km/h)
constexpr float FALL_KMH = 25.0f;                         // a speed change below this is a fall, above a throw
constexpr float PLAY_SHARE = 0.125f;                      // the share of Happer's throw kept for play (ADR-0012)

enum class Kind { Standing, Aware, Lying, Dead, Player };
const char* KindName(Kind k) {
    static const char* names[] = { "standing", "aware", "lying", "dead", "player" };
    return names[(int)k];
}
bool ParseKind(const std::string& s, Kind& k) {
    for (int i = 0; i <= (int)Kind::Player; i++) if (s == KindName((Kind)i)) { k = (Kind)i; return true; }
    return false;
}
const char* const CHECKS[] = { "bump", "falls", "throw", "momentum", "runover", "body", "getup", "evasion-accel", "evasion-speed", "reaction-time", "freeze-time", "frozen-speed", "look-mobility", "reaction-ratio", "inherited-slowdown" };
const char* const METRICS[] = { "hit", "fell", "dead", "escaped", "threatened-escaped", "noticed", "dodged", "noticed-before-hit", "dodged-before-hit", "frozen" };
template <size_t N> bool Known(const char* const (&list)[N], const std::string& s) {
    for (const char* k : list) if (s == k) return true;
    return false;
}

struct VehicleLine { std::string cls; float kmh = 0; bool brake = true; Vector2 front{}; float heading = 0; };
struct PersonLine { Kind kind = Kind::Standing; Vector2 at{}; float facing = 0; int count = 1; Vector2 step{}; };
struct CrowdLine { int count = 0; Vector2 at{}; float radius = 0; Kind kind = Kind::Aware; };
struct Range { std::string name; float lo = 0, hi = 0; };
struct Capture { std::string name; float t = 0; };
struct Scene {
    std::string name; float maxT = 10; int line = 0;
    uint32_t seed = 0;                              // 0: retain the original per-run seed
    float evasionAccel = -1;                        // optional observed acceleration ceiling (m/s^2)
    float evasionSpeed = -1, frozenSpeed = -1, startSpeed = 0;
    float slowdownAt = -1, slowdownDrop = 0;
    int look = -1, attention = -1;                  // optional one look / controlled aware-person traits
    float scan = 0, horizon = 0, reaction = 0, mobility = -1, freeze = 0;
    Range reactionTime, freezeTime;
    float expectedMobility = -1;
    std::string reactionReference;
    Range reactionRatio;
    std::vector<VehicleLine> vehicles;
    std::vector<PersonLine> people;
    std::vector<CrowdLine> crowds;
    std::vector<std::string> checks;
    std::vector<Range> expects;
    std::vector<Capture> captures;
};
struct Sweep { std::string cls; float from = 0, to = 0, step = 0; int draws = 0; };
struct Run { int scene = 0; std::string cls, name; };       // a scene with 'all' resolved to one class

bool Num(const DataRecord& r, size_t i, float& out) {
    if (i >= r.size()) return false;
    const char* s = r[i].c_str(); char* end = nullptr;
    out = strtof(s, &end);
    return end != s && *end == '\0' && std::isfinite(out);
}
bool SafeName(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    return true;
}
int SkinOfClass(VClass c) {
    return c >= 0 && c < (int)gAssets.byClass.size() && !gAssets.byClass[c].empty() ? gAssets.byClass[c][0] : -1;
}
float Happer(float kmh, bool car) {                 // throw distance (m) Happer's regression gives a speed
    float a = car ? 12.7f : 11.4f, b = car ? -2.6f : -0.4f;
    float r = std::max(0.0f, (kmh - b) / a);
    return r * r;
}

// Reads the test file. On a problem: false, and 'error' names the file, the line and the problem.
bool Parse(const char* path, std::vector<Scene>& scenes, std::vector<Sweep>& sweeps, std::string& error) {
    if (!FileExists(path)) { error = std::string(path) + ": no such file"; return false; }
    auto fail = [&](const DataRecord& r, const std::string& what) {
        error = TextFormat("%s line %d: %s", path, r.line, what.c_str());
        return false;
    };
    for (const DataRecord& r : ReadDataFile(path)) {
        if (r.Is("SCENE")) {
            Scene s; s.line = r.line;
            if (r.size() != 3 || !SafeName(r[1]) || !Num(r, 2, s.maxT) || s.maxT <= 0) return fail(r, "SCENE <name> <max s> (letters, digits, - and _)");
            s.name = r[1];
            scenes.push_back(s);
            continue;
        }
        if (r.Is("INJURY")) {
            Sweep w; float draws = 0;
            if (r.size() != 6 || !Num(r, 2, w.from) || !Num(r, 3, w.to) || !Num(r, 4, w.step) || !Num(r, 5, draws) ||
                w.step <= 0 || w.to < w.from || draws < 1) return fail(r, "INJURY <class> <from km/h> <to km/h> <step> <draws>");
            if (FindVehicleClass(r[1]) < 0) return fail(r, "unknown vehicle class '" + r[1] + "'");
            w.cls = r[1]; w.draws = (int)draws;
            sweeps.push_back(w);
            continue;
        }
        if (scenes.empty()) return fail(r, r[0] + " before the first SCENE");
        Scene& s = scenes.back();
        if (r.Is("SEED")) {
            char* end = nullptr;
            unsigned long long seed = r.size() == 2 ? strtoull(r[1].c_str(), &end, 0) : 0;
            if (r.size() != 2 || !end || end == r[1].c_str() || *end != '\0' || seed == 0 || seed > 0xffffffffull || s.seed != 0)
                return fail(r, "SEED <positive 32-bit integer> (at most once per scene)");
            s.seed = (uint32_t)seed;
        } else if (r.Is("LOOK")) {
            float look = 0;
            if (r.size() != 2 || !Num(r, 1, look) || look != floorf(look) || look < 1 || look > (float)gAssets.peds.size())
                return fail(r, "LOOK <1-based civilian look index>");
            s.look = (int)look - 1;
        } else if (r.Is("MOTION")) {
            if (r.size() != 2 || !Num(r, 1, s.startSpeed) || s.startSpeed < 0)
                return fail(r, "MOTION <initial aware-person speed m/s along their facing>");
        } else if (r.Is("TRAITS")) {
            if (r.size() != 7) return fail(r, "TRAITS <attentive|distracted|headphones> <scan s> <horizon s> <reaction s|look> <mobility|look> <freeze s>");
            s.attention = r[1] == "attentive" ? 0 : r[1] == "distracted" ? 1 : r[1] == "headphones" ? 2 : -1;
            if (s.attention < 0 || !Num(r, 2, s.scan) || s.scan <= 0 || !Num(r, 3, s.horizon) || s.horizon <= 0 ||
                (r[4] != "look" && (!Num(r, 4, s.reaction) || s.reaction < 0)) || !Num(r, 6, s.freeze) || s.freeze < 0 ||
                (r[5] != "look" && (!Num(r, 5, s.mobility) || s.mobility <= 0)))
                return fail(r, "TRAITS: valid attention, positive scan/horizon/mobility (or look), nonnegative reaction/freeze");
            if (r[5] == "look") s.mobility = -1;
            if (r[4] == "look") s.reaction = -1;
        } else if (r.Is("VEHICLE")) {
            VehicleLine v;
            if (r.size() != 7 || !Num(r, 2, v.kmh) || !Num(r, 4, v.front.x) || !Num(r, 5, v.front.y) || !Num(r, 6, v.heading) ||
                v.kmh < 0 || (r[3] != "brake" && r[3] != "keep")) return fail(r, "VEHICLE <class|all> <km/h> <brake|keep> <x m> <y m> <heading deg>");
            if (r[1] != "all" && FindVehicleClass(r[1]) < 0) return fail(r, "unknown vehicle class '" + r[1] + "'");
            for (const VehicleLine& o : s.vehicles) if (r[1] == "all" && o.cls == "all") return fail(r, "one VEHICLE all per scene");
            v.cls = r[1]; v.brake = r[3] == "brake";
            s.vehicles.push_back(v);
        } else if (r.Is("PERSON")) {
            PersonLine p; float count = 1;
            bool row = r.size() == 8;
            if ((r.size() != 5 && !row) || !ParseKind(r[1], p.kind) || !Num(r, 2, p.at.x) || !Num(r, 3, p.at.y) || !Num(r, 4, p.facing) ||
                (row && (!Num(r, 5, count) || !Num(r, 6, p.step.x) || !Num(r, 7, p.step.y) || count < 1)))
                return fail(r, "PERSON <standing|aware|lying|dead|player> <x m> <y m> <facing deg> [<count> <dx m> <dy m>]");
            p.count = (int)count;
            if (p.kind == Kind::Player && p.count != 1) return fail(r, "there is one player");
            s.people.push_back(p);
        } else if (r.Is("CROWD")) {
            CrowdLine c; float count = 0;
            if ((r.size() != 5 && r.size() != 6) || !Num(r, 1, count) || !Num(r, 2, c.at.x) || !Num(r, 3, c.at.y) || !Num(r, 4, c.radius) ||
                count < 1 || c.radius <= 0 || (r.size() == 6 && (!ParseKind(r[5], c.kind) || c.kind == Kind::Player)))
                return fail(r, "CROWD <count> <x m> <y m> <radius m> [standing|aware|lying|dead]");
            c.count = (int)count;
            s.crowds.push_back(c);
        } else if (r.Is("CHECK")) {
            if (r.size() < 2 || !Known(CHECKS, r[1])) return fail(r, "CHECK bump|falls|throw|momentum|runover|body|getup, or CHECK evasion-accel <max m/s^2>");
            if (r[1] == "evasion-accel" || r[1] == "evasion-speed" || r[1] == "frozen-speed") {
                float& ceiling = r[1] == "evasion-accel" ? s.evasionAccel : r[1] == "evasion-speed" ? s.evasionSpeed : s.frozenSpeed;
                if (r.size() != 3 || !Num(r, 2, ceiling) || ceiling < 0 || (r[1] != "frozen-speed" && ceiling == 0))
                    return fail(r, "CHECK evasion-accel|evasion-speed|frozen-speed <max in m/s^2 or m/s>");
            } else if (r[1] == "reaction-time" || r[1] == "freeze-time") {
                Range& bounds = r[1] == "reaction-time" ? s.reactionTime : s.freezeTime;
                if (r.size() != 4 || !Num(r, 2, bounds.lo) || !Num(r, 3, bounds.hi) || bounds.lo < 0 || bounds.hi < bounds.lo)
                    return fail(r, "CHECK reaction-time|freeze-time <min s> <max s>");
            } else if (r[1] == "look-mobility") {
                if (r.size() != 3 || !Num(r, 2, s.expectedMobility) || s.expectedMobility <= 0)
                    return fail(r, "CHECK look-mobility <expected multiplier>");
            } else if (r[1] == "reaction-ratio") {
                if (r.size() != 5 || !SafeName(r[2]) || !Num(r, 3, s.reactionRatio.lo) || !Num(r, 4, s.reactionRatio.hi) ||
                    s.reactionRatio.lo <= 0 || s.reactionRatio.hi < s.reactionRatio.lo)
                    return fail(r, "CHECK reaction-ratio <earlier scene name> <min multiplier> <max multiplier>");
                s.reactionReference = r[2];
            } else if (r[1] == "inherited-slowdown") {
                if (r.size() != 4 || !Num(r, 2, s.slowdownAt) || !Num(r, 3, s.slowdownDrop) || s.slowdownAt <= 0 || s.slowdownDrop <= 0)
                    return fail(r, "CHECK inherited-slowdown <time after Dodge s> <min speed reduction m/s>");
            } else if (r.size() != 2) return fail(r, "CHECK <name>");
            s.checks.push_back(r[1]);
        } else if (r.Is("EXPECT")) {
            Range e;
            if (r.size() != 4 || !Known(METRICS, r[1]) || !Num(r, 2, e.lo) || !Num(r, 3, e.hi) || e.lo < 0 || e.hi > 1 || e.hi < e.lo)
                return fail(r, "EXPECT hit|fell|dead|escaped|threatened-escaped|noticed|dodged <min share> <max share> (0..1)");
            e.name = r[1];
            s.expects.push_back(e);
        } else if (r.Is("CAPTURE")) {
            Capture c;
            if (r.size() != 3 || !SafeName(r[1]) || !Num(r, 2, c.t)) return fail(r, "CAPTURE <name> <s>");
            c.name = r[1];
            s.captures.push_back(c);
        } else return fail(r, "unknown record " + r[0]);
    }
    for (const Scene& s : scenes) {
        if (s.vehicles.empty()) { error = TextFormat("%s line %d: scene %s has no VEHICLE", path, s.line, s.name.c_str()); return false; }
        if (s.people.empty() && s.crowds.empty()) { error = TextFormat("%s line %d: scene %s has no PERSON or CROWD", path, s.line, s.name.c_str()); return false; }
    }
    if (scenes.empty() && sweeps.empty()) { error = std::string(path) + ": no SCENE or INJURY record"; return false; }
    return true;
}

struct Person {
    Kind kind = Kind::Standing;
    int ped = -1;                                   // -1: the player
    Vector2 start{};
    float startHealth = 100;
    bool threatened = false;                       // starts in a vehicle's straight swept body
    float noticedAt = -1, dodgedAt = -1, maxEvasionAccel = 0;
    bool noticedBeforeHit = false, dodgedBeforeHit = false;
    float mobility = 1, frozenTime = 0;
    float sampledReaction = 0, initialReaction = 0;
    float maxEvasionSpeed = 0, evasionDistance = 0;
    int evasionFrames = 0;
    float maxFrozenSpeed = 0, entrySpeed = -1, slowdownSpeed = -1;
    // contact with vehicles
    bool touching = false, standingTouch = false;
    float touchT = 0, maxTouchT = 0, carried = 0, maxCarried = 0, firstTouch = -1, lastTouch = -1;
    Vector2 touchFrom{}, touchDir{};
    int overlapRun = 0, maxOverlapRun = 0;
    // the impact
    bool hit = false;
    int hitCar = -1, hitFrame = -1;
    Vector2 impactPos{};
    float impactKmh = 0, launch = 0, momentumRatio = NAN;
    bool runOver = false;                           // driven over after the impact: no free throw to measure
    float maxZ = 0, airT = 0, runoverLoss = 0;
    float airLying = 0, groundDist = 0, groundTurn = 0;  // s drawn lying in the air; px slid and rad turned on the road
    Vector2 prevPos{};
    float prevAngle = NAN;
    bool fell = false, died = false;
    float restT = -1, upT = -1;
    Vector2 restPos{};
};
struct Car {
    int idx = -1;
    float target = 0, length = 0;
    bool brake = true, touched = false;
    Vector2 dir{}, preVel{};
};
} // namespace

struct PersonHitTests::State {
    std::string file;
    std::vector<Scene> scenes;
    std::vector<Sweep> sweeps;
    std::vector<Run> runs;
    int index = -1, frame = 0, checks = 0, failures = 0, completed = 0;
    bool invalid = false, finished = false, nextRun = false;
    std::string error;
    struct ReactionResult { std::string name; std::vector<float> values; };
    std::vector<ReactionResult> reactionResults;      // same-seed adult / older-look comparisons
    // the running scene
    std::vector<Car> cars;
    std::vector<Person> people;
    std::vector<Vector2> prePos, preVel;
    std::vector<float> preVz;
    std::vector<char> captured;
    Vector2 centre{};
    float t = 0, settledAt = -1;
    std::string capture;

    const Run& R() const { return runs[index]; }
    const Scene& S() const { return scenes[R().scene]; }

    // ---- a person's state, whoever they are ----
    Vector2& Pos(Game& g, const Person& p) const { return p.ped < 0 ? g.player.pos : g.peds[p.ped].pos; }
    Vector2& Vel(Game& g, const Person& p) const { return p.ped < 0 ? g.player.vel : g.peds[p.ped].vel; }
    PersonBody& Body(Game& g, const Person& p) const { return p.ped < 0 ? g.player.body : g.peds[p.ped].body; }
    float Health(Game& g, const Person& p) const { return p.ped < 0 ? g.player.health : g.peds[p.ped].health; }
    bool Alive(Game& g, const Person& p) const {
        return p.ped < 0 ? g.player.health > 0 : g.peds[p.ped].state != PedState::Dead && g.peds[p.ped].health > 0;
    }
    bool OnGround(Game& g, const Person& p) const {
        if (p.ped < 0) return g.player.Down() || g.player.health <= 0;
        PedState s = g.peds[p.ped].state;
        return s == PedState::Down || s == PedState::Dead;
    }
    float Radius(const Person& p) const { return p.ped < 0 ? PLAYER_RADIUS : PED_RADIUS; }
    float Angle(Game& g, const Person& p) const { return p.ped < 0 ? g.player.aim : g.peds[p.ped].angle; }
    bool FallOver(Game& g, const Person& p) const {          // on the ground, the fall's frames played
        if (p.ped < 0) return g.player.fallT >= g.player.fallDur;
        return g.peds[p.ped].fallT >= g.peds[p.ped].fallDur;
    }

    void Check(const char* name, int person, float value, float lo, float hi) {
        bool pass = std::isfinite(value) && value >= lo - 1e-4f && value <= hi + 1e-4f;
        checks++;
        if (!pass) failures++;
        TraceLog(LOG_INFO, "PERSONHIT check fixture=%s run=%s person=%d check=%s value=%.4f min=%.4f max=%.4f result=%s",
                 FIXTURE_ID, R().name.c_str(), person, name, value, lo, hi, pass ? "PASS" : "FAIL");
    }

    // ---- setting a scene up ----
    void AddPerson(Game& g, Kind kind, Vector2 at, float facing) {
        Person p; p.kind = kind;
        Vector2 pos = centre + at * M;
        float angle = facing * DEG2RAD;
        if (kind == Kind::Player) {
            PlayerState& P = g.player;
            P.inVehicle = false; P.vehicle = -1;
            P.pos = pos; P.vel = V2(0, 0); P.aim = angle;
            P.health = 100; P.armor = 0; P.hurtFlash = 0;
            P.bodyState = BodyAnim::Idle; P.actionTimer = 0; P.reloadTimer = 0;
            P.body = PersonBody{}; P.body.balance = PlayerBalance();
            P.fallT = -1; P.lieT = P.getUpT = 0;
            g.autoMode = 6;                                    // stands still, no input
        } else {
            const Scene& scene = S();
            int look = scene.look >= 0 ? scene.look : scene.seed ? (int)people.size() : (int)(people.size() * 7 + 3);
            p.ped = g.SpawnPed(pos, look % std::max(1, (int)gAssets.peds.size()), false);
            Pedestrian& q = g.peds[p.ped];
            q.angle = angle; q.vel = V2(0, 0);
            if (kind == Kind::Aware) q.vel = Forward(angle) * (scene.startSpeed * M);
            q.state = PedState::Idle; q.resume = PedState::Idle; q.timer = 1e9f;
            q.courage = 0;
            p.sampledReaction = q.reaction;
            if (kind == Kind::Aware && scene.attention >= 0) {
                q.attention = (PedAttention)scene.attention;
                q.sensePeriod = scene.scan; q.noticeHorizon = scene.horizon; q.sense = 0;
                if (scene.reaction >= 0) q.reaction = scene.reaction;
                if (scene.mobility > 0) q.mobility = scene.mobility;
                q.freezeDelay = scene.freeze;
            }
            p.mobility = q.mobility;
            p.initialReaction = q.reaction;
            if (kind != Kind::Aware) { q.reaction = 1e9f; q.sense = 1e9f; }    // does not see it coming
            if (kind == Kind::Lying) q.state = PedState::Down;
            if (kind == Kind::Dead) { q.state = PedState::Dead; q.health = 0; q.pooled = true; }
        }
        p.start = pos;
        p.startHealth = p.ped < 0 ? g.player.health : g.peds[p.ped].health;
        people.push_back(p);
    }

    void Start(Game& g) {
        const Run& run = R();
        const Scene& s = S();
        g.vehicles.clear(); g.peds.clear(); g.pickups.clear(); g.deathSpots.clear();
        g.fx.Clear(); g.toasts.clear(); g.bigTimer = 0;
        g.pedGrid.Build(g.peds);
        g.physics = VehiclePhysics{};
        g.state = GameState::Playing; g.heat = 0;
        g.autoMode = 0;
        uint32_t seed = s.seed ? s.seed : SEED + (uint32_t)index * 7919u;
        GRng().s = seed;
        centre = V2(cfg::WORLD_W * 0.5f, cfg::WORLD_H * 0.5f);
        // the player stays well away unless the scene uses them
        g.player.inVehicle = false; g.player.vehicle = -1;
        g.player.pos = centre + V2(0, -150 * M); g.player.vel = V2(0, 0);
        g.player.health = 100; g.player.armor = 0;
        g.player.body = PersonBody{}; g.player.body.balance = PlayerBalance();
        g.player.fallT = -1; g.player.lieT = g.player.getUpT = 0;
        cars.clear(); people.clear();
        for (const VehicleLine& l : s.vehicles) {
            VClass c = FindVehicleClass(l.cls == "all" ? run.cls : l.cls);
            float angle = l.heading * DEG2RAD;
            Car car;
            car.idx = g.SpawnVehicle(SkinOfClass(c), V2(0, 0), angle, DriverType::Traffic);
            Vehicle& v = g.vehicles[car.idx];
            v.pos = centre + l.front * M - Forward(angle) * (v.length * 0.5f);
            car.target = l.kmh * KMH; car.brake = l.brake; car.dir = Forward(angle); car.length = v.length;
            v.vel = car.dir * car.target;
            cars.push_back(car);
        }
        for (const PersonLine& l : s.people)
            for (int k = 0; k < l.count; k++) AddPerson(g, l.kind, l.at + l.step * (float)k, l.facing);
        // New seeded scenes keep their placement independent of any extra random
        // traits drawn at spawn; old unseeded scenes retain their recorded sequence.
        Rng layoutRng; layoutRng.s = seed ^ 0x4c41594fu;
        Rng& layout = s.seed ? layoutRng : GRng();
        for (const CrowdLine& c : s.crowds) {
            int placed = 0;
            for (int tries = 0; placed < c.count && tries < c.count * 50; tries++) {
                float a = layout.Range(0, 2 * PI), d = c.radius * sqrtf(layout.Float());
                Vector2 at = c.at + V2(cosf(a), sinf(a)) * d;
                bool free = true;
                for (const Person& o : people) free = free && Dist(o.start, centre + at * M) > PED_RADIUS * 2.2f;
                if (!free) continue;
                AddPerson(g, c.kind, at, layout.Range(0, 360));
                placed++;
            }
        }
        int threatened = 0;
        for (Person& p : people) {
            if (p.kind == Kind::Lying || p.kind == Kind::Dead) continue;
            for (const Car& car : cars) {
                const Vehicle& v = g.vehicles[car.idx];
                Vector2 rel = p.start - v.pos;
                float along = Dot(rel, car.dir), across = fabsf(Cross(car.dir, rel));
                p.threatened = p.threatened || (along >= v.length * 0.5f - Radius(p) &&
                    along <= v.length * 0.5f + car.target * s.maxT + Radius(p) && across < v.width * 0.5f + Radius(p));
            }
            threatened += p.threatened ? 1 : 0;
        }
        captured.assign(s.captures.size(), 0);
        t = 0; settledAt = -1;
        TraceLog(LOG_INFO, "PERSONHIT start fixture=%s run=%s index=%d seed=%08x vehicles=%d people=%d threatened=%d", FIXTURE_ID, run.name.c_str(),
                 index + 1, seed, (int)cars.size(), (int)people.size(), threatened);
    }

    // ---- one frame ----
    void Drive(Game& g) {
        for (Car& car : cars) {
            Vehicle& v = g.vehicles[car.idx];
            float fwd = Dot(v.vel, car.dir);
            v.in = VehicleInput{};
            if (!car.touched) { v.vel = car.dir * car.target; v.angVel = 0; }       // at its speed until it meets someone
            else if (car.brake) { bool moving = fwd > 0.5f * M; v.in.brake = moving ? 1.0f : 0.0f; v.in.handbrake = !moving; }
            else {                                                                    // a driver holding the speed
                float err = car.target - fwd;
                v.in.throttle = Clampf(err / M, 0, 1);
                v.in.brake = Clampf(-err / M, 0, 1);
            }
        }
    }

    // The vehicle (index into cars) whose box a person is in, and how deep; -1 if none. With
    // 'wheels', someone lying counts only between the axles: a bumper may pass above them.
    int Touching(Game& g, const Person& p, float* deepest, bool wheels = false) const {
        int best = -1; float most = 0;
        for (size_t c = 0; c < cars.size(); c++) {
            const Vehicle& v = g.vehicles[cars[c].idx];
            Vector2 n; float depth;
            if (CircleOBB(Pos(g, p), Radius(p), wheels ? v.Wheels() : v.Box(), n, depth) && depth > most) { most = depth; best = (int)c; }
        }
        if (deepest) *deepest = most;
        return best;
    }

    void BeforeContacts(Game& g) {
        prePos.resize(people.size()); preVel.resize(people.size()); preVz.resize(people.size());
        for (Car& car : cars) car.preVel = g.vehicles[car.idx].vel;
        for (size_t k = 0; k < people.size(); k++) {
            Person& p = people[k];
            prePos[k] = Pos(g, p); preVel[k] = Vel(g, p); preVz[k] = Body(g, p).vz;
            bool living = Alive(g, p) && !Body(g, p).Airborne();
            int c = Touching(g, p, nullptr, OnGround(g, p));       // someone lying: only under the wheels
            if (c >= 0 && p.hit && OnGround(g, p) && !Body(g, p).Airborne()) p.runOver = true;
            if (c >= 0) cars[c].touched = true;
            if (c >= 0 && living) {
                if (!p.touching) {                     // a new contact
                    p.touching = true; p.touchT = 0; p.touchFrom = prePos[k];
                    p.touchDir = Norm(cars[c].preVel); p.standingTouch = !OnGround(g, p);
                    if (p.firstTouch < 0) p.firstTouch = t;
                }
                p.touchT += DT; p.lastTouch = t;
                p.carried = Dot(prePos[k] - p.touchFrom, p.touchDir);
                if (p.standingTouch) p.maxTouchT = std::max(p.maxTouchT, p.touchT);
                p.maxCarried = std::max(p.maxCarried, p.carried);
            } else p.touching = false;
            if (c >= 0 && !p.hit && Alive(g, p) && !OnGround(g, p) && Len(cars[c].preVel) > 0.1f * KMH) {
                p.hit = true; p.hitCar = c; p.hitFrame = frame;
                p.impactPos = prePos[k];
                p.impactKmh = Len(cars[c].preVel) / KMH;
            }
        }
    }

    void AfterContacts(Game& g) {
        for (size_t k = 0; k < people.size(); k++) {
            Person& p = people[k];
            // the speed vehicles lost in the contact step while over someone lying there
            if (p.kind == Kind::Lying || p.kind == Kind::Dead)
                for (const Car& car : cars) {
                    Vector2 n; float depth;
                    if (CircleOBB(prePos[k], Radius(p), g.vehicles[car.idx].Box(), n, depth))
                        p.runoverLoss += std::max(0.0f, (Len(car.preVel) - Len(g.vehicles[car.idx].vel)) / KMH);
                }
            // the impact itself: the person's launch, the momentum each body took
            if (p.hitFrame == frame) {
                Vector2 v = Vel(g, p);
                float vz = Body(g, p).vz;
                p.launch = sqrtf(Len2(v) + vz * vz);
                Vector2 dvP = v - preVel[k];
                const Car& car = cars[p.hitCar];
                Vector2 dvV = g.vehicles[car.idx].vel - car.preVel;
                float gained = PERSON_KG * Len(dvP);
                if (gained > 0) p.momentumRatio = g.vehicles[car.idx].S().mass * 1000.0f * Len(dvV) / gained;
            }
            // passing through: a living person inside a vehicle's box after the contacts
            float depth = 0;
            bool inside = Alive(g, p) && !Body(g, p).Airborne() && Touching(g, p, &depth, OnGround(g, p)) >= 0 && depth > 3.0f;
            p.overlapRun = inside ? p.overlapRun + 1 : 0;
            p.maxOverlapRun = std::max(p.maxOverlapRun, p.overlapRun);
        }
    }

    void Track(Game& g) {
        bool settled = true;
        for (size_t k = 0; k < people.size(); k++) {
            Person& p = people[k];
            if (p.ped >= 0) {
                const Pedestrian& q = g.peds[p.ped];
                bool dodging = q.state == PedState::Dodge;
                if (p.noticedAt < 0 && (q.alarmVehicle >= 0 || dodging)) p.noticedAt = t;
                if (p.dodgedAt < 0 && dodging) { p.dodgedAt = t; p.entrySpeed = Len(preVel[k]) / M; }
                if (!p.hit) {
                    p.noticedBeforeHit = p.noticedBeforeHit || q.alarmVehicle >= 0 || dodging;
                    p.dodgedBeforeHit = p.dodgedBeforeHit || dodging;
                    if (dodging && q.freezeT > 0) {
                        p.frozenTime += DT;
                        p.maxFrozenSpeed = std::max(p.maxFrozenSpeed, Len(q.vel) / M);
                    }
                    if (S().slowdownAt > 0 && p.dodgedAt >= 0 && p.slowdownSpeed < 0 && t - p.dodgedAt >= S().slowdownAt)
                        p.slowdownSpeed = Len(q.vel) / M;
                }
                // Observe actual velocity, including the body-heading projection, without
                // treating the impact impulse or a later get-up as an evasive acceleration.
                if (!p.hit && dodging) {
                    p.maxEvasionAccel = std::max(p.maxEvasionAccel, Len(q.vel - preVel[k]) / (DT * M));
                    p.maxEvasionSpeed = std::max(p.maxEvasionSpeed, Len(q.vel) / M);
                    p.evasionDistance = std::max(p.evasionDistance, Dist(q.pos, p.start) / M);
                    if (Len(q.vel) > 0.05f * M) p.evasionFrames++;
                }
            }
            const PersonBody& b = Body(g, p);
            if (b.Airborne()) { p.airT += DT; p.maxZ = std::max(p.maxZ, b.z); }
            if (b.Airborne() && OnGround(g, p) && FallOver(g, p)) p.airLying += DT;
            float angle = Angle(g, p);
            if (p.fell && !b.Airborne() && Len(Vel(g, p)) > 0.5f * M && std::isfinite(p.prevAngle)) {
                p.groundDist += Dist(Pos(g, p), p.prevPos);
                p.groundTurn += fabsf(WrapAngle(angle - p.prevAngle));
            }
            p.prevPos = Pos(g, p); p.prevAngle = angle;
            if (OnGround(g, p) && p.kind != Kind::Lying && p.kind != Kind::Dead) p.fell = true;
            if (!Alive(g, p) && p.kind != Kind::Dead) p.died = true;
            bool still = !b.Airborne() && Len(Vel(g, p)) < REST_SPEED;
            if (p.hit && p.restT < 0 && still) { p.restT = t; p.restPos = Pos(g, p); }
            if (p.fell && !p.died && p.upT < 0 && !OnGround(g, p)) p.upT = t;
            bool done = !p.touching && !b.Airborne() && (!Alive(g, p) ? still : !p.fell || p.upT >= 0);
            settled = settled && done;
        }
        for (const Car& car : cars) {
            const Vehicle& v = g.vehicles[car.idx];
            bool passed = true;
            for (const Person& p : people) passed = passed && Dot(Pos(g, p) - v.pos, car.dir) < -(car.length * 0.5f + 1.0f * M);
            bool stopped = car.touched && car.brake && Len(v.vel) < 0.5f * M;
            settled = settled && (passed || stopped);
        }
        if (!settled) settledAt = -1;
        else if (settledAt < 0) settledAt = t;
    }

    // ---- results ----
    void Finish(Game& g) {
        const Scene& s = S();
        const Run& run = R();
        for (size_t k = 0; k < people.size(); k++) {
            Person& p = people[k];
            int id = (int)k + 1;
            Vector2 end = Pos(g, p);
            float health = Health(g, p);
            float throwM = p.restT >= 0 ? Dist(p.restPos, p.impactPos) / M : NAN;
            // two lines: raylib cuts a log message at 256 characters
            TraceLog(LOG_INFO, "PERSONHIT measure run=%s person=%d kind=%s hit=%d impact_kmh=%.2f launch_kmh=%.2f throw_m=%.3f "
                     "peak_m=%.3f air_s=%.3f rest_s=%.3f fell=%d died=%d up_s=%.3f health=%.1f",
                     run.name.c_str(), id, KindName(p.kind), (int)p.hit, p.impactKmh, p.launch / KMH, throwM,
                     p.maxZ / M, p.airT, p.restT, (int)p.fell, (int)p.died, p.upT, health);
            TraceLog(LOG_INFO, "PERSONHIT contact run=%s person=%d moved_m=%.3f contact_s=%.3f carried_m=%.3f overlap_frames=%d "
                     "momentum_ratio=%.3f runover_loss_kmh=%.3f run_over=%d",
                     run.name.c_str(), id, Dist(end, p.start) / M, p.maxTouchT, p.maxCarried / M, p.maxOverlapRun,
                     p.momentumRatio, p.runoverLoss, (int)p.runOver);
            if (p.kind == Kind::Aware)
                TraceLog(LOG_INFO, "PERSONHIT evasion run=%s person=%d threatened=%d noticed_s=%.3f dodge_s=%.3f reaction_s=%.3f accel_mps2=%.3f hit=%d",
                         run.name.c_str(), id, (int)p.threatened, p.noticedAt, p.dodgedAt,
                         p.noticedAt >= 0 && p.dodgedAt >= 0 ? p.dodgedAt - p.noticedAt : -1, p.maxEvasionAccel, (int)p.hit);
            if (p.kind == Kind::Aware) {
                const Pedestrian& q = g.peds[p.ped];
                TraceLog(LOG_INFO, "PERSONHIT traits run=%s person=%d look=%d attention=%d scan_s=%.3f horizon_s=%.3f reaction_s=%.3f mobility=%.3f freeze_s=%.3f frozen_s=%.3f",
                         run.name.c_str(), id, q.skin + 1, (int)q.attention, q.sensePeriod, q.noticeHorizon,
                         q.reaction, p.mobility, q.freezeDelay, p.frozenTime);
                TraceLog(LOG_INFO, "PERSONHIT movement run=%s person=%d peak_mps=%.3f evasion_m=%.3f moving_dodge_frames=%d",
                         run.name.c_str(), id, p.maxEvasionSpeed, p.evasionDistance, p.evasionFrames);
                TraceLog(LOG_INFO, "PERSONHIT sampled run=%s person=%d sampled_reaction_s=%.5f initial_reaction_s=%.5f",
                         run.name.c_str(), id, p.sampledReaction, p.initialReaction);
                if (s.frozenSpeed >= 0 || s.slowdownAt > 0)
                    TraceLog(LOG_INFO, "PERSONHIT transition run=%s person=%d frozen_peak_mps=%.4f entry_mps=%.4f later_mps=%.4f after_s=%.3f",
                             run.name.c_str(), id, p.maxFrozenSpeed, p.entrySpeed, p.slowdownSpeed, s.slowdownAt);
            }
            // always: no pushing (CJ-035 criterion 6), no passing through (criterion 7)
            if (p.kind != Kind::Dead) {
                Check("contact_s", id, p.maxTouchT, 0, 0.1f);
                Check("carried_m", id, p.maxCarried / M, 0, 0.3f);
                Check("overlap_frames", id, (float)p.maxOverlapRun, 0, 2);
            }
            // how it looks: never lying in the air; tumbling, not gliding, along the road
            if (p.fell) Check("lying_in_air_s", id, p.airLying, 0, 0);
            if (p.fell && p.groundDist > 1.0f * M)
                Check("tumble_deg_per_m", id, p.groundTurn * RAD2DEG / (p.groundDist / M), 20, 1000);
            const VehicleSpec* spec = p.hitCar >= 0 ? &g.vehicles[cars[p.hitCar].idx].S() : nullptr;
            for (const std::string& c : s.checks) {
                if (c == "bump") {
                    Check("bumped", id, (float)p.hit, 1, 1);
                    Check("on_feet", id, (float)!p.fell, 1, 1);
                    Check("health_lost", id, p.startHealth - health, 0, 0);
                    Check("recovered_s", id, p.firstTouch >= 0 ? p.lastTouch - p.firstTouch : NAN, 0, 1.0f);
                } else if (c == "falls") {
                    Check("fell", id, (float)(p.fell || p.died), 1, 1);
                } else if (c == "throw" && !p.runOver) {
                    float s0 = p.launch * p.launch / (2 * SEARLE_MU * GRAVITY) / M;
                    Check("throw_searle_m", id, throwM, s0, s0 * (1 + SEARLE_MU * SEARLE_MU));
                    if (spec && !spec->twoWheeler()) {
                        bool car = spec->front < CAR_FRONT;
                        float tol = car ? 9.0f : 10.5f;
                        Check(car ? "throw_happer_car_m" : "throw_happer_truck_m", id, throwM,
                              PLAY_SHARE * Happer(p.impactKmh - tol, car), PLAY_SHARE * Happer(p.impactKmh + tol, car));
                    }
                } else if (c == "momentum") {
                    Check("momentum_ratio", id, p.momentumRatio, 0.9f, 1.1f);
                } else if (c == "runover" || c == "body") {
                    bool passed = false;
                    for (const Car& car : cars)
                        passed = passed || Dot(end - g.vehicles[car.idx].pos, car.dir) < -(car.length * 0.5f + Radius(p));
                    if (c == "runover") Check("died", id, (float)p.died, 1, 1);
                    Check("moved_m", id, Dist(end, p.start) / M, 0, 0.3f);
                    Check("vehicle_loss_kmh", id, p.runoverLoss, 0, c == "runover" ? 2.0f : 1.0f);
                    Check("passed_over", id, (float)passed, 1, 1);
                } else if (c == "getup") {
                    if (p.died) Check("died_instead", id, 1, 1, 1);
                    else {
                        bool thrown = p.launch / KMH / sqrtf(PLAY_SHARE) >= FALL_KMH;   // the real collision's launch
                        Check(thrown ? "getup_after_throw_s" : "getup_after_fall_s", id,
                              p.upT >= 0 && p.restT >= 0 ? p.upT - p.restT : NAN, thrown ? 4.0f : 2.5f, thrown ? 5.0f : 3.5f);
                    }
                } else if (c == "evasion-accel" && p.kind == Kind::Aware) {
                    Check("evasion_accel_mps2", id, p.maxEvasionAccel, 0, s.evasionAccel * p.mobility);
                } else if (c == "evasion-speed" && p.kind == Kind::Aware) {
                    Check("evasion_speed_mps", id, p.maxEvasionSpeed, 0, s.evasionSpeed * p.mobility);
                } else if (c == "reaction-time" && p.kind == Kind::Aware) {
                    Check("reaction_time_s", id, p.noticedAt >= 0 && p.dodgedAt >= 0 ? p.dodgedAt - p.noticedAt : NAN,
                          s.reactionTime.lo, s.reactionTime.hi);
                } else if (c == "freeze-time" && p.kind == Kind::Aware) {
                    Check("freeze_time_s", id, p.frozenTime, s.freezeTime.lo, s.freezeTime.hi);
                } else if (c == "frozen-speed" && p.kind == Kind::Aware) {
                    Check("frozen_speed_mps", id, p.maxFrozenSpeed, 0, s.frozenSpeed);
                } else if (c == "look-mobility" && p.kind == Kind::Aware) {
                    Check("look_mobility", id, p.mobility, s.expectedMobility, s.expectedMobility);
                } else if (c == "reaction-ratio" && p.kind == Kind::Aware) {
                    float reference = NAN;
                    for (const ReactionResult& prior : reactionResults)
                        if (prior.name == s.reactionReference && k < prior.values.size()) reference = prior.values[k];
                    Check("look_reaction_ratio", id, reference > 0 ? p.initialReaction / reference : NAN,
                          s.reactionRatio.lo, s.reactionRatio.hi);
                } else if (c == "inherited-slowdown" && p.kind == Kind::Aware) {
                    Check("inherited_speed_reduction_mps", id,
                          p.entrySpeed >= 0 && p.slowdownSpeed >= 0 ? p.entrySpeed - p.slowdownSpeed : NAN, s.slowdownDrop, s.startSpeed);
                }
            }
        }
        ReactionResult reactionResult; reactionResult.name = run.name;
        for (const Person& p : people) reactionResult.values.push_back(p.initialReaction);
        reactionResults.push_back(std::move(reactionResult));
        if (s.evasionAccel > 0 || s.evasionSpeed > 0) {
            int moved = 0;
            for (const Person& p : people) moved += p.evasionFrames > 0 ? 1 : 0;
            Check("moving_evasion_people", 0, (float)moved, 1, (float)people.size());
        }
        for (const Range& e : s.expects) {
            float n = 0, total = 0;
            for (const Person& p : people) {
                if (e.name == "threatened-escaped" && !p.threatened) continue;
                bool v = e.name == "hit" ? p.hit : e.name == "fell" ? p.fell || p.died : e.name == "dead" ? p.died :
                         e.name == "noticed" ? p.noticedAt >= 0 : e.name == "dodged" ? p.dodgedAt >= 0 :
                         e.name == "noticed-before-hit" ? p.noticedBeforeHit : e.name == "dodged-before-hit" ? p.dodgedBeforeHit :
                         e.name == "frozen" ? p.frozenTime > 0 : !p.hit;
                n += v ? 1.0f : 0.0f;
                total += 1.0f;
            }
            Check(e.name.c_str(), 0, total > 0 ? n / total : NAN, e.lo, e.hi);
        }
        completed++;
    }

    void RunSweeps() {
        for (const Sweep& w : sweeps) {
            Vehicle v; InitVehicle(v, SkinOfClass(FindVehicleClass(w.cls)), V2(0, 0), 0);
            for (float kmh = w.from; kmh <= w.to + 1e-3f; kmh += w.step) {
                GRng().s = SEED ^ (uint32_t)(kmh * 100);
                int deaths = 0;
                for (int i = 0; i < w.draws; i++) deaths += PersonImpactDamage(v, kmh * KMH) >= 100.0f ? 1 : 0;
                float share = deaths / (float)w.draws, want = 1.0f / (1.0f + expf(-TEFFT_SLOPE * (kmh - TEFFT_V50)));
                bool pass = fabsf(share - want) <= 0.03f;
                checks++;
                if (!pass) failures++;
                TraceLog(LOG_INFO, "PERSONHIT injury fixture=%s class=%s kmh=%.1f draws=%d deaths=%.4f tefft=%.4f min=%.4f max=%.4f result=%s",
                         FIXTURE_ID, w.cls.c_str(), kmh, w.draws, share, want, want - 0.03f, want + 0.03f, pass ? "PASS" : "FAIL");
            }
        }
    }
};

PersonHitTests::PersonHitTests() = default;
PersonHitTests::~PersonHitTests() = default;

bool PersonHitTests::Init(Game& g, const char* testFile) {
    state = std::make_unique<State>();
    State& s = *state;
    s.file = testFile ? testFile : "tests/person-hits.cfg";
    if (!Parse(s.file.c_str(), s.scenes, s.sweeps, s.error)) {
        s.invalid = true;
        TraceLog(LOG_ERROR, "PERSONHIT invalid test file: %s", s.error.c_str());
        return false;
    }
    const char* only = std::getenv("CJ_TEST_CASE");
    for (size_t i = 0; i < s.scenes.size(); i++) {
        const Scene& sc = s.scenes[i];
        float need = 0; bool all = false;
        for (const VehicleLine& v : sc.vehicles) if (v.cls == "all") { all = true; need = v.kmh; }
        std::vector<Run> runs;
        if (!all) runs.push_back({ (int)i, "", sc.name });
        else for (const VehicleSpec& spec : VehicleClasses()) {
            if (SkinOfClass(FindVehicleClass(spec.name)) < 0) continue;
            if (need * KMH > spec.maxSpeed) {
                TraceLog(LOG_INFO, "PERSONHIT skipped run=%s-%s top_speed_kmh=%.0f", sc.name.c_str(), spec.name.c_str(), spec.maxSpeed / KMH);
                continue;
            }
            runs.push_back({ (int)i, spec.name, sc.name + "-" + spec.name });
        }
        for (const Run& r : runs) if (!only || !*only || r.name.find(only) != std::string::npos) s.runs.push_back(r);
    }
    g.map.ResetTestGround(Tile::Road);
    g.dn.hour = 13.0f; g.dn.timeScale = 0; g.dn.Update(0);
    g.showHelp = false;
    g.debugContacts = true;
    const char* revision = std::getenv("CJ_TEST_REVISION");
    TraceLog(LOG_INFO, "PERSONHIT run fixture=%s file=%s revision=%s seed=%08x scenes=%d runs=%d sweeps=%d render_dt=%.8f build=%s_%s",
             FIXTURE_ID, s.file.c_str(), revision ? revision : "UNRECORDED", SEED, (int)s.scenes.size(), (int)s.runs.size(),
             (int)s.sweeps.size(), DT, __DATE__, __TIME__);
    s.RunSweeps();
    if (s.runs.empty()) { s.finished = true; return true; }
    s.index = 0;
    s.Start(g);
    return true;
}

void PersonHitTests::Update(Game& g, float dt) {
    if (!state || state->invalid || state->finished) return;
    State& s = *state;
    if (!std::isfinite(dt) || fabsf(dt - DT) > 1e-6f) {
        s.invalid = true; TraceLog(LOG_ERROR, "PERSONHIT requires a fixed 1/60 s clock"); return;
    }
    if (s.nextRun) { s.index++; s.Start(g); s.nextRun = false; }
    s.t += DT;
    s.frame++;
    g.time += DT;
    // The game's own order: the player on foot, physics and impacts, vehicles meeting
    // people, then the people.
    s.Drive(g);
    bool player = false;
    for (const Person& p : s.people) player = player || p.ped < 0;
    if (player) g.UpdatePlayerOnFoot(DT);
    PhysicsStepOptions options; options.disableWorldEdges = true;
    g.physics.Step(g, DT, options);
    g.HandleImpacts(DT);
    s.BeforeContacts(g);
    g.VehiclePedCollisions();
    s.AfterContacts(g);
    g.pedGrid.Build(g.peds);
    for (Pedestrian& p : g.peds) if (p.active) UpdatePed(p, g, DT);
    g.fx.Update(DT);
    g.player.hurtFlash = std::max(0.0f, g.player.hurtFlash - DT * 2);   // as UpdatePlaying
    s.Track(g);
    for (size_t i = 0; i < s.S().captures.size(); i++)
        if (!s.captured[i] && s.t >= s.S().captures[i].t) { s.captured[i] = 1; s.capture = s.R().name + "-" + s.S().captures[i].name; break; }
    Vector2 focus = s.people.empty() ? s.centre : s.Pos(g, s.people[0]);
    g.cam.Snap(focus, VIEW_M * M);
    bool done = s.t >= s.S().maxT - 1e-4f || (s.settledAt >= 0 && s.t >= s.settledAt + SETTLE_S);
    if (done && s.capture.empty()) {
        s.Finish(g);
        if (s.index == (int)s.runs.size() - 1) s.finished = true;
        else s.nextRun = true;
    }
}

void PersonHitTests::Draw(Game& g) const {
    if (!state) return;
    const State& s = *state;
    if (s.invalid) {
        ClearBackground({ 20, 25, 30, 255 });
        DrawUITextShadow("CJ-035 person-hit scenes: invalid test file", 40, 60, 30, RED);
        DrawUITextShadow(s.error.c_str(), 40, 110, 22, WHITE);
        return;
    }
    g.Draw();
    if (s.index >= 0 && s.index < (int)s.runs.size())
        DrawUITextShadow(TextFormat("CJ-035  %s   %.2f s   run %d / %d", s.R().name.c_str(), s.t, s.index + 1, (int)s.runs.size()),
                         30, (float)GetScreenHeight() - 50, 24, WHITE);
}

void PersonHitTests::Log() const {
    if (!state) return;
    const State& s = *state;
    TraceLog(LOG_INFO, "PERSONHIT summary fixture=%s runs=%d completed=%d checks=%d failed=%d invalid=%d finished=%d",
             FIXTURE_ID, (int)s.runs.size(), s.completed, s.checks, s.failures, (int)s.invalid, (int)s.finished);
}

bool PersonHitTests::Failed() const { return !state || state->invalid || state->failures > 0; }
bool PersonHitTests::Finished() const { return state && state->finished; }
const char* PersonHitTests::CaptureLabel() const { return state && !state->capture.empty() ? state->capture.c_str() : nullptr; }
void PersonHitTests::ClearCaptureRequest() { if (state) state->capture.clear(); }
