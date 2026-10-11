// =====================================================================================
//  Person-hit scenes (CJ-035): vehicles meeting people on the empty test road, one or
//  many of each, as a test file describes them (tests/person-hits.cfg; the format is in
//  its header and in docs/testing.md#person-hit-scenes). Production physics, contacts
//  and pedestrian AI run in the game's order; each scene's named checks and expected
//  ranges are logged as PASS or FAIL.
// =====================================================================================
#pragma once
#include <memory>

class Game;

class PersonHitTests {
public:
    PersonHitTests();
    ~PersonHitTests();
    bool Init(Game& game, const char* testFile);   // false: the file is missing or invalid
    void Update(Game& game, float dt);
    void Draw(Game& game) const;
    void Log() const;
    bool Failed() const;
    bool Finished() const;
    const char* CaptureLabel() const;
    void ClearCaptureRequest();

private:
    struct State;
    std::unique_ptr<State> state;
};
