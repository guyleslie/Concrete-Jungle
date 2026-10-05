// =====================================================================================
//  CJ-016 frozen, rendered recovery fixtures. Isolated scenes exercise the production
//  driver and contact solver without city spawning, damage rules or player autopilots.
// =====================================================================================
#pragma once
#include <memory>

class Game;

class TrafficTests {
public:
    TrafficTests();
    ~TrafficTests();
    bool Init(Game& game, const char* className);
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
