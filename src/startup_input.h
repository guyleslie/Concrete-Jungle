// =====================================================================================
//  Pure title-input handoff policy: startup-held keys rearm independently on release.
//  The game supplies key state; this helper has no window, clock or raylib dependency.
// =====================================================================================
#pragma once

namespace startup {

class TitleInputGuard {
public:
    void Suppress(bool enterHeld, bool spaceHeld) {
        suppressEnter = enterHeld;
        suppressSpace = spaceHeld;
    }

    bool AcceptStart(bool enterDown, bool spaceDown, bool enterPressed, bool spacePressed) {
        if (suppressEnter && !enterDown) suppressEnter = false;
        if (suppressSpace && !spaceDown) suppressSpace = false;
        return (!suppressEnter && enterPressed) || (!suppressSpace && spacePressed);
    }

private:
    bool suppressEnter = false, suppressSpace = false;
};

} // namespace startup
