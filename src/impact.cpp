// =====================================================================================
//  Vehicles hitting people (CJ-035) - see impact.h.
// =====================================================================================
#include "impact.h"
#include "vehicle.h"

// The injury grows with the impact energy: about half die at 36 km/h, nearly all above 45 km/h.
float PersonImpactDamage(const Vehicle& v, float closing) {
    (void)v;
    return 100.0f * (closing / 160.0f) * (closing / 160.0f) * GRng().Range(0.8f, 1.25f);
}
