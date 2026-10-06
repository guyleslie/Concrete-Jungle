// =====================================================================================
//  CJ-016 cooperative yielding fixture: two drivers stopped behind each other must get
//  stable roles and resolve the blockage physically (rail retreat, tuck-in, recovery).
// =====================================================================================
#pragma once
#include <memory>

class Game;

class TrafficConflictTests {
public:
    TrafficConflictTests();
    ~TrafficConflictTests();
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
