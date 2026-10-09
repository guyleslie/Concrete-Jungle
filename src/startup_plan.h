// =====================================================================================
//  Startup discovery: estimates work from current metadata before freezing the plan.
//  Optional local timing history scales budgets; it never controls completion or waits.
// =====================================================================================
#pragma once
#include "startup_loading.h"

std::vector<startup::TaskSpec> DiscoverStartupPlan(startup::Reporter* loading = nullptr);
void SaveStartupProfile(const startup::Reporter& reporter);
