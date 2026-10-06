// =====================================================================================
//  Vehicle physics & rendering - see vehicle.h
// =====================================================================================
#include "vehicle.h"
#include "assets.h"
#include "city_map.h"
#include "particles.h"
#include "render.h"

float VehicleYawInertia(const Vehicle& v) {
    return v.S().mass * (v.width * v.width + v.length * v.length) / 12.0f;
}

void InitVehicle(Vehicle& v, int skin, Vector2 pos, float angle) {
    const VehicleSprite& s = gAssets.vehicles[skin];
    static uint32_t nextSerial = 0;
    v = Vehicle{};
    v.active = true;
    v.serial = ++nextSerial;
    v.skin = skin;
    v.cls = s.cls;
    v.pos = pos;
    v.angle = angle;
    const VehicleSpec& sp = Spec(v.cls);
    v.length = sp.length;
    v.width = sp.length * (s.src.width / s.src.height);
    v.height = sp.height;
    v.health = sp.health;
    v.driverSkin = GRng().Int(0, std::max(0, (int)gAssets.peds.size() - 1));
}

// -------------------------------------------------------------------------------------
//  Physics (per sub-step; positions and collisions are handled by physics.cpp)
// -------------------------------------------------------------------------------------
static const float SLIDE_DECEL     = 520.0f;   // tyres sliding sideways (px/s^2 on asphalt)
static const float LAT_STATIC      = 90.0f;    // static friction: sideways creep stops
static const float HANDBRAKE_DECEL = 160.0f;   // locked rear wheels bring a slow car to rest
static const float LOCKED_DECEL    = 420.0f;   // parked (in gear + handbrake) / burnt-out wreck
static const float YAW_AUTHORITY   = 45.0f;    // max yaw acceleration the tyres can produce (rad/s^2)

void VehicleForces(Vehicle& v, const CityMap& map, float h) {
    const VehicleSpec& sp = v.S();
    VehicleInput in = v.in;
    bool parked = v.driver == DriverType::Parked;
    if (v.wrecked || v.burning || v.driver == DriverType::None || parked) {
        in = VehicleInput{};
        in.handbrake = parked || v.wrecked;
    }

    // The same values as Forward()/RightOf(), with one sine and cosine evaluation.
    float sinA, cosA;
    CachedSinCos(v.angle, sinA, cosA);
    Vector2 fwd = { sinA, -cosA }, right = { cosA, sinA };
    // Factors that depend only on the sub-step length are computed once per length.
    static float stepH = -1, handbrakeCar = 1, handbrakeBike = 1, steerDamp = 0, followSlip = 0, followGrip = 0;
    if (h != stepH) {
        stepH = h;
        handbrakeCar = expf(-0.9f * h); handbrakeBike = expf(-1.6f * h);
        steerDamp = Damp(9.0f, h); followSlip = Damp(3.5f, h); followGrip = Damp(9.0f, h);
    }
    float vF = Dot(v.vel, fwd), vR = Dot(v.vel, right);
    float surf = map.Grip(v.pos);
    bool offroad = surf < 0.8f;

    // ---- longitudinal ----
    v.braking = false; v.reversing = false;
    if (in.throttle > 0.01f) {
        if (vF < -25) { vF += sp.brake * in.throttle * h; v.braking = true; }
        else {
            float k = 1.0f - Saturate(vF / sp.maxSpeed) * Saturate(vF / sp.maxSpeed);
            vF += sp.accel * in.throttle * k * (offroad ? 0.7f : 1.0f) * h;
        }
    }
    if (in.brake > 0.01f) {
        if (vF > 25) { vF -= sp.brake * in.brake * h; v.braking = true; if (vF < 0) vF = 0; }
        else {
            vF -= sp.accel * 0.6f * in.brake * h;
            if (vF < -sp.reverseMax) vF = -sp.reverseMax;
            v.reversing = vF < -5;
        }
    }
    bool driven = v.driver == DriverType::Player || v.driver == DriverType::Traffic || v.driver == DriverType::Police;
    if (in.handbrake) {
        vF *= sp.twoWheeler() ? handbrakeBike : handbrakeCar;
        // locked wheels: a slow car really stops (a fast handbrake turn keeps its old feel)
        float lock = (parked || v.wrecked) ? LOCKED_DECEL : HANDBRAKE_DECEL * (1.0f - SmoothStep(60.0f, 200.0f, fabsf(vF)));
        vF -= Sign(vF) * std::min(fabsf(vF), lock * surf * h);
        v.braking = driven;
    }
    // aerodynamic + rolling resistance (more off-road)
    float drag = 0.12f + (offroad ? 0.9f : 0.0f);
    vF -= vF * drag * h;
    float roll = (offroad ? 120.0f : 45.0f) * h;
    if (fabsf(vF) <= roll && in.throttle < 0.01f && in.brake < 0.01f) vF = 0;
    else if (in.throttle < 0.01f) vF -= Sign(vF) * roll;

    // ---- lateral grip (drops with slip angle & handbrake -> drifting) ----
    // The handbrake only unsettles a moving car; at a standstill it holds it.
    float grip = sp.grip * surf;
    if (in.handbrake) grip *= Lerpf(1.0f, sp.twoWheeler() ? 0.35f : 0.16f, SmoothStep(40.0f, 160.0f, fabsf(vF)));
    grip *= 1.0f / (1.0f + fabsf(vR) / 380.0f);
    float vR0 = vR;
    vR *= expf(-grip * h);
    // Sliding sideways (spun or shoved in a crash): the tyres saturate, friction becomes a
    // constant deceleration - the car slides a few metres instead of stopping dead.
    float slideAngle = atan2f(fabsf(vR0), fabsf(vF) + 1.0f);
    float sat = SmoothStep(0.45f, 0.8f, slideAngle);
    float cap = (parked || v.wrecked ? LOCKED_DECEL : SLIDE_DECEL) * surf * h;
    if (sat > 0 && fabsf(vR0 - vR) > cap) vR = vR0 - Sign(vR0 - vR) * Lerpf(fabsf(vR0 - vR), cap, sat);
    vR -= Sign(vR) * std::min(fabsf(vR), LAT_STATIC * surf * h);   // no endless slow creep

    v.vel = fwd * vF + right * vR;

    // ---- steering: target yaw rate, reached with limited tyre torque ----
    v.steer = Lerpf(v.steer, in.steer, steerDamp);
    float speedFactor = Saturate(fabsf(vF) / 150.0f) * (1.0f - 0.42f * Saturate(fabsf(vF) / sp.maxSpeed));
    float dir = Clampf(vF / 30.0f, -1.0f, 1.0f);           // smooth through zero (no flip-flop)
    float yawTarget = v.steer * sp.steerRate * speedFactor * dir * (in.handbrake ? 1.35f : 1.0f);
    float follow = v.slip > 180 ? followSlip : followGrip;
    float auth = YAW_AUTHORITY * surf * h;
    v.angVel += Clampf((yawTarget - v.angVel) * follow, -auth, auth);
}

void VehicleFrameEffects(Vehicle& v, const CityMap& map, Particles& fx, float dt) {
    const VehicleSpec& sp = v.S();
    VehicleInput in = v.in;
    if (v.wrecked || v.burning || v.driver == DriverType::None || v.driver == DriverType::Parked) {
        in = VehicleInput{};
        in.handbrake = v.driver == DriverType::Parked || v.wrecked;     // shoved on locked wheels: skid marks
    }
    Vector2 fwd = Forward(v.angle), right = RightOf(v.angle);
    float vF = Dot(v.vel, fwd), vR = Dot(v.vel, right);
    bool offroad = map.Grip(v.pos) < 0.8f;
    v.speedFwd = vF;
    v.slip = fabsf(vR);

    // engine "rpm" for audio: speed within a fake gearbox + throttle
    float sp01 = Saturate(fabsf(vF) / sp.maxSpeed);
    float gear = fmodf(sp01 * 4.0f, 1.0f);
    v.rpm = Lerpf(v.rpm, 0.25f + 0.55f * gear + 0.2f * in.throttle + (v.slip > 200 ? 0.2f : 0), Damp(6, dt));

    // ---- tyre effects: skid marks + smoke ----
    bool skid = !sp.twoWheeler() && ((v.slip > 130) || (in.handbrake && fabsf(vF) > 140) ||
                                   (in.throttle > 0.9f && vF > 20 && vF < 170 && sp.accel > 600));
    if (sp.twoWheeler()) skid = v.slip > 160 || (in.handbrake && fabsf(vF) > 200);
    for (int w = 0; w < 2; w++) {
        float side = w == 0 ? -1.0f : 1.0f;
        if (sp.twoWheeler() && w == 1) { v.skidOn[1] = false; break; }
        Vector2 wp = v.pos + right * (sp.twoWheeler() ? 0.0f : side * v.width * 0.36f) - fwd * (v.length * 0.33f);
        if (skid && !offroad) {
            if (v.skidOn[w] && Dist(v.lastSkid[w], wp) > 4.0f) {
                fx.AddSkid(v.lastSkid[w], wp, sp.twoWheeler() ? 3.0f : 5.0f, Saturate(0.25f + v.slip / 600.0f));
                v.lastSkid[w] = wp;
            } else if (!v.skidOn[w]) v.lastSkid[w] = wp;
            v.skidOn[w] = true;
            if (GRng().Chance(dt * 30.0f)) fx.TyreSmoke(wp, v.vel, Saturate(v.slip / 400.0f));
        } else {
            v.skidOn[w] = false;
        }
        if (offroad && fabsf(vF) > 120 && GRng().Chance(dt * 20.0f)) fx.Dust(wp, v.vel, { 110, 95, 70, 255 });
    }

    UpdateVehicleEffects(v, fx, dt);
}


// Damage smoke, fire and the hit flash (shared by physics and rail-driven vehicles).
void UpdateVehicleEffects(Vehicle& v, Particles& fx, float dt) {
    const VehicleSpec& sp = v.S();
    Vector2 fwd = Forward(v.angle);
    Vector2 engine = v.pos + fwd * (v.length * 0.3f);
    if (!v.wrecked && v.health < sp.health * 0.5f && GRng().Chance(dt * (v.health < sp.health * 0.25f ? 25.0f : 10.0f))) {
        unsigned char g = v.health < sp.health * 0.25f ? 40 : 150;
        fx.Smoke(engine, v.height, v.vel * 0.3f, 14, { g, g, g, 150 }, 1.6f);
    }
    if (v.burning || (v.wrecked && v.wreckTimer < 8.0f)) {
        if (GRng().Chance(dt * 40)) fx.FirePuff(engine + V2(GRng().Range(-8, 8), GRng().Range(-8, 8)), v.height, 26);
        if (GRng().Chance(dt * 12)) fx.Smoke(engine, v.height + 10, { 0, 0 }, 26, { 30, 30, 30, 200 }, 3.0f);
    }
    v.damageFlash = std::max(0.0f, v.damageFlash - dt * 4);
}

// -------------------------------------------------------------------------------------
//  Rendering
// -------------------------------------------------------------------------------------
static const VehicleSprite& SpriteOf(const Vehicle& v) {
    const VehicleSprite& s = gAssets.vehicles[v.skin];
    // motorbikes without a rider use the empty variant
    if (s.emptySkin >= 0 && (v.driver == DriverType::None || v.driver == DriverType::Parked || v.wrecked))
        return gAssets.vehicles[s.emptySkin];
    return s;
}

void DrawVehicleShadow(const Vehicle& v, Vector2 sv) {
    const VehicleSprite& s = SpriteOf(v);
    // silhouette of the sprite, offset by the sun, slightly spread for softness
    Vector2 o = sv * (v.height * 0.6f);
    DrawFlatSprite(s.tex, s.src, v.pos + o, 0, v.width * 1.08f, v.length * 1.04f, v.angle, ColorA(BLACK, 0.55f));
    DrawFlatSprite(s.tex, s.src, v.pos + o * 0.5f, 0, v.width * 1.02f, v.length * 1.0f, v.angle, ColorA(BLACK, 0.8f));
}

void DrawVehicle(const Vehicle& v, float time) {
    const VehicleSprite& s = SpriteOf(v);
    Color tint = WHITE;
    if (v.wrecked) tint = { 70, 62, 58, 255 };
    else {
        float dmg = 1.0f - Saturate(v.health / v.S().health);
        tint = ColorMul(WHITE, 1.0f - 0.35f * dmg);
        if (v.damageFlash > 0) tint = LerpColor(tint, { 255, 200, 190, 255 }, v.damageFlash * 0.5f);
    }
    DrawFlatSprite(s.tex, s.src, v.pos, v.height, v.width, v.length, v.angle, tint);
    if (v.missionTarget) {
        float p = 0.5f + 0.5f * sinf(time * 5);
        DrawFlatRing(v.pos, v.length * 0.62f, v.length * 0.62f + 2.5f, v.height + 1, ColorA({ 255, 220, 60, 255 }, 0.5f + 0.5f * p), 40);
    }
}

static bool IsEmergency(const Vehicle& v) { return v.S().emergency(); }

void DrawVehicleLights(const Vehicle& v, float night, float time) {
    if (v.wrecked) {
        if (v.wreckTimer < 8.0f) Renderer::Radial(v.pos, 3, 10 * 16, { 255, 140, 60, 255 }, 0.9f * (0.8f + 0.2f * sinf(time * 23)));
        return;
    }
    if (v.burning) Renderer::Radial(v.pos, 3, 9 * 16, { 255, 140, 60, 255 }, 1.0f * (0.8f + 0.2f * sinf(time * 29)));
    Vector2 f = v.Fwd(), r = RightOf(v.angle);
    bool on = v.headlights && v.driver != DriverType::None && v.driver != DriverType::Parked;
    if (on) {
        float spread = v.S().twoWheeler() ? 0.0f : v.width * 0.32f;
        float reach = v.S().twoWheeler() ? 17 * 16.0f : 22 * 16.0f;
        for (int k = v.S().twoWheeler() ? 1 : 0; k < 2; k++) {
            float side = v.S().twoWheeler() ? 0.0f : (k == 0 ? -1.0f : 1.0f);
            Vector2 apex = v.pos + f * (v.length * 0.48f) + r * (side * spread);
            Renderer::Cone(apex, 3, v.angle + side * 0.04f, reach, reach * 0.8f, { 255, 240, 215, 255 }, 0.85f * night + 0.1f);
        }
        Renderer::Radial(v.pos + f * (v.length * 0.5f + 20), 3, 60, { 255, 240, 220, 255 }, 0.35f * night);
    }
    if (on || v.braking) {
        float k = v.braking ? 0.7f : 0.25f * night;
        Renderer::Radial(v.pos - f * (v.length * 0.5f + 8), 3, v.braking ? 70 : 45, { 255, 30, 20, 255 }, k);
    }
    if (v.reversing) Renderer::Radial(v.pos - f * (v.length * 0.5f + 20), 3, 70, { 255, 255, 255, 255 }, 0.5f);
    if (v.siren && IsEmergency(v)) {
        bool phase = fmodf(time * 3.0f, 1.0f) < 0.5f;
        Color a = phase ? Color{ 255, 30, 30, 255 } : Color{ 40, 80, 255, 255 };
        Renderer::Radial(v.pos, 3, 14 * 16, a, 0.9f);
    }
}

void DrawVehicleEmissive(const Vehicle& v, float night, float time) {
    if (v.wrecked) return;
    Vector2 f = v.Fwd(), r = RightOf(v.angle);
    bool on = v.headlights && v.driver != DriverType::None && v.driver != DriverType::Parked;
    float h = v.height + 0.5f;
    bool bike = v.S().twoWheeler();
    for (int k = 0; k < (bike ? 1 : 2); k++) {
        float side = bike ? 0.0f : (k == 0 ? -1.0f : 1.0f);
        Vector2 hp = v.pos + f * (v.length * 0.46f) + r * (side * v.width * 0.32f);
        Vector2 tp = v.pos - f * (v.length * 0.47f) + r * (side * v.width * 0.34f);
        if (on) Renderer::Flare(hp, h, bike ? 18 : 26, { 255, 245, 225, 255 }, 0.35f + 0.65f * night);
        float tail = v.braking ? 1.0f : (on ? 0.45f : 0.0f);
        if (tail > 0) Renderer::Flare(tp, h, v.braking ? 22 : 14, { 255, 30, 20, 255 }, tail);
        if (v.reversing) Renderer::Flare(tp + r * (side * -4.0f), h, 14, { 255, 255, 255, 255 }, 0.8f);
    }
    if (v.siren && IsEmergency(v)) {
        // light bar: alternating red / blue strobes across the roof
        float ph = fmodf(time * 3.0f, 1.0f);
        Vector2 bar = v.pos - f * (v.S().police() ? v.length * 0.05f : -v.length * 0.3f);
        Renderer::Flare(bar - r * (v.width * 0.25f), h + 1, 34, { 255, 40, 40, 255 }, ph < 0.5f ? 1.0f : 0.15f);
        Renderer::Flare(bar + r * (v.width * 0.25f), h + 1, 34, { 60, 110, 255, 255 }, ph >= 0.5f ? 1.0f : 0.15f);
    }
    if (v.burning) Renderer::Radial(v.pos + f * (v.length * 0.3f), h, 30, { 255, 150, 60, 255 }, 0.5f + 0.3f * sinf(time * 31));
}
