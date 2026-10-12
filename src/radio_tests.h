// =====================================================================================
//  Radio scenes (CJ-040), --scenario radio: the programme of every station simulated over
//  many hours and checked against its rules; radio cars driving past the player on an
//  empty road; the player's own radio changing clips. The scenes come from a test file
//  (tests/radio.cfg; the format is in its header and in docs/testing.md#radio-scenes).
//  The final mix and the station's own signal are recorded to build/radio/ for
//  tools/run_cj040.py, which measures them. Sound runs in real time, so these scenes do
//  too: the cars move by the wall clock, not by the fixed 1/60 s test step.
// =====================================================================================
#pragma once
#include <memory>

class Game;

class RadioTests {
public:
    RadioTests();
    ~RadioTests();
    bool Init(Game& game, const char* testFile);   // false: the file is missing or invalid
    void Update(Game& game, float dt);
    void Draw(Game& game) const;
    void Log() const;
    bool Failed() const;
    bool Finished() const;

private:
    struct State;
    std::unique_ptr<State> state;
};
