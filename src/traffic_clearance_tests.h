// =====================================================================================
//  CJ-016 nearby-box clearance regression. Separate from the frozen recovery fixture,
//  this scene checks the production rejoin API and a real physical-to-rail handoff.
// =====================================================================================
#pragma once
#include <memory>

class Game;

class TrafficClearanceTests {
public:
    TrafficClearanceTests();
    ~TrafficClearanceTests();
    bool Init(Game& game);
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
