#ifndef ALLOC_GUARD_H
#define ALLOC_GUARD_H

#include <climits>
#include <cstddef>
#include <cstdint>

namespace SDL {

/* Check if w * h * bpp bytes can be safely allocated.
 * Performs the check entirely in unsigned space to avoid integer underflow
 * from casting SIZE_MAX to signed int. */
static inline bool alloc_fits(int w, int h, size_t bpp) {
  if (w <= 0 || h <= 0) return false;
  size_t uw = static_cast<size_t>(w);
  size_t uh = static_cast<size_t>(h);
  if (uw > SIZE_MAX / bpp) return false;
  if (uh > SIZE_MAX / (bpp * uw)) return false;
  return true;
}

}  // namespace SDL

#endif
