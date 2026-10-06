// =====================================================================================
//  CJ-016 driver incident fixture: a real rear-end contact, then the drivers' stop,
//  exit, approach, argument, fight and return to the same car, or an explicit reason.
// =====================================================================================
#pragma once
#include <memory>

class Game;

class TrafficIncidentTests {
public:
    TrafficIncidentTests();
    ~TrafficIncidentTests();
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
