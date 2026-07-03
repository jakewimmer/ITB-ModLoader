#include "../src/alloc_guard.h"
#include <cassert>
#include <cstdio>
#include <climits>
#include <cstring>

int main() {
  using SDL::alloc_fits;

  /* Test case 1: Small valid allocation (1x1) */
  assert(alloc_fits(1, 1, 4) == true);
  printf("PASS: 1x1 with 4 bpp\n");

  /* Test case 2: Medium valid allocation (100x100) */
  assert(alloc_fits(100, 100, 4) == true);
  printf("PASS: 100x100 with 4 bpp\n");

  /* Test case 3: Large valid allocation (4096x4096) */
  assert(alloc_fits(4096, 4096, 4) == true);
  printf("PASS: 4096x4096 with 4 bpp\n");

  /* Test case 4: Overflow rejection - w exceeds SIZE_MAX / bpp
   * On 64-bit: SIZE_MAX is ~1.84e19, SIZE_MAX/4 is ~4.6e18.
   * With w = (size_t)-1/4 (very large), h=1, should reject.
   * Simulate by using a huge bpp and testing boundary. */
  size_t huge_bpp = SIZE_MAX / 2;
  assert(alloc_fits(2, 2, huge_bpp) == false);
  printf("PASS: 2x2 with huge bpp rejected\n");

  /* Test case 5: Overflow rejection - h causes secondary overflow
   * INT_MAX with bpp=16: (2^31-1)^2 * 16 will overflow on most systems. */
  assert(alloc_fits(INT_MAX, INT_MAX, 16) == false);
  printf("PASS: INT_MAX x INT_MAX x 16 rejected\n");

  /* Test case 6: Invalid dimensions - w <= 0 */
  assert(alloc_fits(0, 100, 4) == false);
  assert(alloc_fits(-1, 100, 4) == false);
  printf("PASS: w <= 0 rejected\n");

  /* Test case 7: Invalid dimensions - h <= 0 */
  assert(alloc_fits(100, 0, 4) == false);
  assert(alloc_fits(100, -1, 4) == false);
  printf("PASS: h <= 0 rejected\n");

  /* Test case 8: Both dimensions invalid */
  assert(alloc_fits(-1, -1, 4) == false);
  printf("PASS: both dimensions invalid rejected\n");

  printf("\nAll tests passed!\n");
  return 0;
}
