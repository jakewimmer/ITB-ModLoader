/* Regression test for the wasDrawn()/onModsLoaded byte-order contract.
 *
 * A Surface must store and hash its pixels in the SAME byte order the game
 * uploads its textures (GL_RGBA/GL_UNSIGNED_BYTE, i.e. RGBA). If setBitmap ever
 * re-introduces a channel swap (e.g. RGBA->BGRA), a Surface's hash stops
 * matching the identically-pixelled game texture, wasDrawn() never returns
 * true, and onModsLoaded never fires -- the exact bug this guards against.
 *
 * Links against libitbsdl.so for Surface::setBitmap and simple_hash. Those two
 * code paths touch no GL/SDL/Lua symbols, so the .so's undefined game-provided
 * symbols are never called here (lazy binding, unresolved-symbols=ignore-all). */
#include "../src/screen_gl.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

/* setBitmap is protected; expose it for the test without altering the API. */
struct TestSurface : SDL::Surface {
  using SDL::Surface::setBitmap;
};

int main() {
  /* Two pixels with distinct, asymmetric channels so a swap is detectable. */
  const unsigned char rgba[8] = {0x10, 0x20, 0x30, 0x40,
                                 0x50, 0x60, 0x70, 0x80};

  TestSurface s;
  s.setBitmap(rgba, 0, 0, 2, 1, 2 * 4);

  /* Explicit checks (not assert) so the guard stays live under -DNDEBUG. */
  if (s.pixelData == nullptr) {
    printf("FAIL: setBitmap produced no pixelData\n");
    return 1;
  }

  /* Stored bytes must be identical to the source -- no channel swap. */
  if (std::memcmp(s.pixelData, rgba, sizeof(rgba)) != 0) {
    printf("FAIL: setBitmap did not store pixels in source (RGBA) order\n");
    return 1;
  }
  printf("PASS: setBitmap stores pixels in source (RGBA) byte order\n");

  /* Hash must be computed over those exact RGBA bytes, matching the value the
   * GL interposer computes for a game texture uploaded with the same pixels. */
  if (s.hash != SDL::simple_hash(rgba, sizeof(rgba))) {
    printf("FAIL: hash does not cover the RGBA bytes\n");
    return 1;
  }
  printf("PASS: hash covers RGBA bytes (matches game-texture hashing path)\n");

  return 0;
}
