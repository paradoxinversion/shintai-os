// Host unit tests for Zanshin's pure derivation + pack (ZanshinGrid.h): the 8x8 field ->
// left/right nearest ranges (which feed distance_l/r_mm + alert) and the 128-byte depth-grid
// wire format. Same host-test posture as thermal-grid-test.cpp / koei — the decidable-from-
// values logic checked on the host with no board.
//
//   c++ -std=c++17 -Wall -o /tmp/zanshin-test tools/zanshin-grid-test.cpp && /tmp/zanshin-test

#include "../firmware/shintai-os/ZanshinGrid.h"

#include <cassert>
#include <cstdio>
#include <cstdint>

static const int W = ZANSHIN_W, H = ZANSHIN_H;   // 8, 8

// Fill a 64-zone field via distance + status generators.
template <typename FD, typename FS>
static void fill(int16_t* dist, uint8_t* status, FD fd, FS fs) {
  for (int r = 0; r < H; r++)
    for (int c = 0; c < W; c++) { int z = r * W + c; dist[z] = fd(r, c); status[z] = fs(r, c); }
}

static uint16_t rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void test_dims() {
  assert(W == 8 && H == 8);
  assert(ZANSHIN_ZONES == 64);
  assert(ZANSHIN_GRID_BYTES == 128);
  assert(ZANSHIN_GRID_BYTES <= 244);   // one BLE notification at the negotiated MTU 247
}

static void test_zone_valid() {
  assert(zanshinZoneValid(5) && zanshinZoneValid(9));            // ST's two "range valid" codes
  assert(!zanshinZoneValid(0) && !zanshinZoneValid(4) && !zanshinZoneValid(255));
}

static void test_pack_grid() {
  int16_t dist[ZANSHIN_ZONES];
  uint8_t status[ZANSHIN_ZONES];
  uint8_t out[ZANSHIN_GRID_BYTES];

  // Every zone 1000 mm, valid -> packs 1000 (LE uint16) everywhere.
  fill(dist, status, [](int, int) { return (int16_t)1000; }, [](int, int) { return (uint8_t)5; });
  zanshinPackGrid(dist, status, out);
  for (int z = 0; z < ZANSHIN_ZONES; z++) assert(rd_u16(out + z * 2) == 1000);

  // Invalid status -> 0; a status-9 zone keeps its mm.
  status[10] = 0;                       // knock out zone 10
  dist[20] = 1234; status[20] = 9;
  zanshinPackGrid(dist, status, out);
  assert(rd_u16(out + 10 * 2) == 0);    // invalid -> 0 sentinel
  assert(rd_u16(out + 20 * 2) == 1234); // status 9 is valid
}

static void test_derive_lr() {
  int16_t dist[ZANSHIN_ZONES];
  uint8_t status[ZANSHIN_ZONES];
  int16_t l, r;

  // All invalid -> both halves report no target.
  fill(dist, status, [](int, int) { return (int16_t)500; }, [](int, int) { return (uint8_t)0; });
  zanshinDeriveLR(dist, status, &l, &r);
  assert(l == -1 && r == -1);

  // Near target on the LEFT (col 1), farther on the RIGHT (col 6), plus a farther LEFT
  // target — each half must report its NEAREST valid zone.
  fill(dist, status, [](int, int) { return (int16_t)-1; }, [](int, int) { return (uint8_t)0; });
  dist[3 * W + 1] = 300;  status[3 * W + 1] = 5;   // left half, near
  dist[0 * W + 2] = 900;  status[0 * W + 2] = 5;   // left half, far
  dist[5 * W + 6] = 1500; status[5 * W + 6] = 9;   // right half
  zanshinDeriveLR(dist, status, &l, &r);
  assert(l == 300);    // nearest valid in cols 0..3
  assert(r == 1500);   // nearest valid in cols 4..7

  // A zone with a valid distance but the split at col 3|4 is respected: col 3 = left, col 4 = right.
  fill(dist, status, [](int, int) { return (int16_t)-1; }, [](int, int) { return (uint8_t)0; });
  dist[2 * W + 3] = 700; status[2 * W + 3] = 5;    // col 3 -> LEFT
  dist[2 * W + 4] = 400; status[2 * W + 4] = 5;    // col 4 -> RIGHT
  zanshinDeriveLR(dist, status, &l, &r);
  assert(l == 700 && r == 400);
}

int main() {
  test_dims();
  test_zone_valid();
  test_pack_grid();
  test_derive_lr();
  std::printf("zanshin-grid-test: all assertions passed\n");
  return 0;
}
