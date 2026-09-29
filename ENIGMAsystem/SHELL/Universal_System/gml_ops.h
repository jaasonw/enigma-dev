// GML number helpers shared by the runtime and generated code.

#ifndef ENIGMA_GML_OPS_H
#define ENIGMA_GML_OPS_H

#include <cmath>

namespace enigma {

// Round half to even, independent of the floating-point rounding mode. GM8
// (Delphi's Round) uses this for round(), array indices, views and integer
// conversions.
inline double round_half_even(double x) {
  double r = std::floor(x + 0.5);
  if (r - x == 0.5 && std::fmod(r, 2.0) != 0) r -= 1;
  return r;
}

}  // namespace enigma

#endif  // ENIGMA_GML_OPS_H
