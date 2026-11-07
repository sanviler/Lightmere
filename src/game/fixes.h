#pragma once
// Lightmere fixes to the original code and data of Darkmere (applied once MAIN.BIN is loaded).
// Every fix checks the original bytes before writing: if they differ, nothing is changed.
// Each fix is described where it is applied.
#include "amiga/machine.h"

namespace dm::game {

void applyFixes(amiga::Machine& m);

} // namespace dm::game
