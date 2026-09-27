#ifndef ENIGMA_COMPLIANCE_H
#define ENIGMA_COMPLIANCE_H

namespace enigma {

// The game's GM_COMPATIBILITY_VERSION (50-81 for GM5-GM8.1, 65535 for standard).
// The macro exists only where GAME_SETTINGS.h is included; engine sources are
// compiled once for every game, so they read this value, set in SHELLmain.cpp.
extern const int gm_compatibility_version;

inline bool gm8_compliance() { return gm_compatibility_version <= 81; }

}  // namespace enigma

#endif  // ENIGMA_COMPLIANCE_H
