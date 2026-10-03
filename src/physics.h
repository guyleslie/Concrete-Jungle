// =====================================================================================
//  Vehicle rigid-body physics: contact generation + sub-stepped impulse solver.
//
//  Every frame:
//    1. COLLIDE once at the frame-start poses. Box-vs-box contacts (car/car, car/building)
//       use the separating axis test + reference/incident face clipping, which gives up
//       to two real contact points - a car resting flat against a wall is held by both
//       corners instead of rocking around one averaged point. Street furniture is
//       circle-vs-box. Contacts are "speculative": they are created a little before the
//       shapes touch (margin = how far they can move this frame), so fast cars never
//       sink into walls or tunnel through thin poles.
//    2. SOLVE in N sub-steps (Box2D v3 "soft step" scheme): tyre/engine forces ->
//       warm start -> sequential impulses (normal + Coulomb friction) with a soft,
//       speed-limited push-out for overlap -> integrate -> relax pass without push-out.
//       All constraints (car/car, car/wall, car/pole ...) are solved together, so a car
//       wedged between a wall and another car settles instead of being shoved back and
//       forth by one-at-a-time corrections.
//    3. RESTITUTION once at the end, only above 1 m/s and falling with impact speed
//       (bumpers spring back in a parking knock, crumple zones absorb a real crash).
//
//  Kinematic traffic ("rail" cars, see traffic.h) are infinitely heavy moving bodies
//  here. A real hit (or pushing on something for a moment) knocks them into full
//  physics, so they can never bulldoze the player into a wall.
//
//  Street furniture has a breakaway strength: the contact can only take so much
//  impulse, then the object gives way (lamp posts fall over, hydrants burst) and the
//  car loses the momentum it took to break it. Shrubs are soft (drag, get flattened).
//
//  Physics does not apply damage or play sounds: it reports ImpactEvents, the game
//  turns them into damage (by delta-V, the standard crash severity measure), sparks,
//  sounds and driver reactions.
// =====================================================================================
#pragma once
#include "raylib.h"
#include "math_utils.h"
#include <vector>

class Game;
struct Vehicle;

// Isolated fixtures can remove external tyre forces and island walls.
// Defaults preserve the production simulation.
struct PhysicsStepOptions {
    bool disableForces = false;
    bool disableWorldEdges = false;
};

enum class ContactKind : uint8_t { Vehicle, Building, Object, WorldEdge };

struct ImpactEvent {
    int         a = -1, b = -1;       // vehicle indices (b = -1: static world)
    ContactKind kind = ContactKind::Building;
    int         obj = -1;             // city object (kind == Object)
    Vector2     point{}, normal{};    // normal points from a to b
    float       approach = 0;         // closing speed at first touch (px/s)
    float       dvA = 0, dvB = 0;     // velocity change caused by the contact (px/s)
    float       scrape = 0;           // sliding speed along the contact while pressed (px/s)
    bool        broke = false;        // the object gave way
};

class VehiclePhysics {
public:
    void Step(Game& g, float dt, const PhysicsStepOptions& options = {});
    std::vector<ImpactEvent> events;  // filled by Step, consumed by the game

    // tuning (px, s, tonnes)
    static constexpr float SUBSTEP_HZ       = 240.0f;
    static constexpr float CONTACT_HZ       = 30.0f;   // stiffness of the soft push-out
    static constexpr float CONTACT_DAMPING  = 10.0f;
    static constexpr float MAX_PUSH         = 3.0f * 16.0f;   // overlap resolved at most 3 m/s
    static constexpr float RESTITUTION_MIN  = 16.0f;   // no bounce below 1 m/s
    static constexpr float KNOCK_SPEED      = 60.0f;   // closing speed that knocks a rail car
    static constexpr float KNOCK_SHOVE_TIME = 0.35f;   // ... or pushing on something this long

private:
    PhysicsStepOptions stepOptions;
    struct Body {
        Vehicle* v = nullptr;
        float im = 0, iI = 0;         // inverse mass / inertia (0: kinematic or static)
        bool  kin = false;            // kinematic rail car
        Vector2 p0{}; float a0 = 0;   // pose when the contacts were generated
        Vector2 to{}; float toAng = 0;  // kinematic: pose at the end of the frame
        Vector2 kv{}; float kw = 0;   // kinematic: velocity over the frame
    };
    struct Point {
        Vector2 ra{}, rb{};           // anchors from the body centres (rb: world point if static)
        float sep0 = 0;               // separation at collide time (< 0 overlap)
        float nMass = 0, tMass = 0;
        float relVel = 0;             // normal relative velocity at frame start (< 0 closing)
        float jn = 0, jt = 0;         // sub-step impulses (warm started)
        float jnTotal = 0, jnMax = 0, jtTotal = 0;
    };
    struct Contact {
        int a = -1, b = -1, obj = -1;
        ContactKind kind = ContactKind::Vehicle;
        Vector2 n{};
        int count = 0;
        Point pts[2];
        float friction = 0.4f, restitution = 0.1f;
        float cap = 0;                // > 0: breakaway strength (total impulse)
        bool  broken = false;
    };
    struct SoftHit { int v, obj; };

    std::vector<Body> bodies;
    std::vector<Contact> contacts;
    std::vector<SoftHit> soft;

    void Collide(Game& g, float dt);
    void AddContact(const Contact& c);
    void Prepare(Contact& c);
    void WarmStart(Contact& c);
    void Solve(Contact& c, bool useBias, float inv_h, float h);
    void ApplyRestitution(Contact& c);
    Vector2 VelAt(int b, Vector2 r) const;
    void    Impulse(int b, Vector2 r, Vector2 P);
};
