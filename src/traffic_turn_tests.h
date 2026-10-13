// =====================================================================================
//  CJ-020 turning fixture: one rail car of every traffic class turns right, turns left
//  or goes straight through an empty junction; the rear-axle slip, the rear-axle path
//  radius, the body over the kerb and in the wrong half of the road are measured.
//  CJ-038 adds a separate data-driven mode with a stationary crossing person: the
//  driver must yield before any contact and finish after the person clears.
// =====================================================================================
#pragma once
#include <memory>

class Game;

class TrafficTurnTests {
public:
    TrafficTurnTests();
    ~TrafficTurnTests();
    bool Init(Game& game, const char* pedestrianTestFile = nullptr);
    void Update(Game& game, float dt);
    void Draw(const Game& game) const;
    void Log() const;
    bool Failed() const;
    bool Finished() const;
    const char* CaptureLabel() const;
    void ClearCaptureRequest();

private:
    struct State;
    std::unique_ptr<State> state;
};
