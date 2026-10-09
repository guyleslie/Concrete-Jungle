// =====================================================================================
//  Visible loading presentation fixture. It uses real startup reporters and private
//  CPU-only fixture records, without loading/simulating game content or gameplay RNG.
// =====================================================================================
#pragma once
class LoadingScreen;

// Window and loading cosmetics must already exist. Output directory must be relative.
// Owns its fixture Begin/EndDrawing calls; restores the original window dimensions.
int RunLoadingPresentationTests(LoadingScreen& screen, const char* outputDirectory);
