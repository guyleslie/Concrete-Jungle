// =====================================================================================
//  CJ-002 measurement fixtures. These own isolated test scenes and drive the production
//  vehicle integrator with frozen inputs. They never run during an ordinary game.
// =====================================================================================
#pragma once
#include <memory>

class Game;

class VehicleTests {
public:
    VehicleTests();
    ~VehicleTests();
    bool Init(Game& game, const char* scenario, const char* className);
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
