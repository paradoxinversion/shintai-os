// Host unit tests for Metsuke's pure thermal downsample + binary pack (ThermalGrid.h).
//
// Same posture as kanki-band-test.cpp / kehai-band-test.cpp: the firmware has no
// on-target harness, so the decidable-from-values logic — the 32x24 -> 8x8 block
// average, NaN handling, and the 68-byte little-endian wire format — is checked on
// the host with no board.
//
//   c++ -std=c++17 -Wall -o /tmp/thermal-test tools/thermal-grid-test.cpp && /tmp/thermal-test

#include "../firmware/shintai-os/ThermalGrid.h"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>

// Fill a 32x24 frame via a f(row,col)->temp generator.
template <typename F>
static void fill(float* frame, F gen) {
  for (int r = 0; r < MLX_H; r++)
    for (int c = 0; c < MLX_W; c++)
      frame[r * MLX_W + c] = gen(r, c);
}

static int16_t rd_i16(const uint8_t* p) {   // little-endian signed read-back
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void test_dims_and_format() {
  assert(METSUKE_GRID_CELLS == 64);
  assert(METSUKE_GRID_BYTES == 68);
}

static void test_uniform_frame() {
  // A flat 25.0C scene: min==max, span 0 -> all cells pack to 0, header carries 250.
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int) { return 25.0f; });
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));
  assert(rd_i16(out) == 250 && rd_i16(out + 2) == 250);
  for (int i = 0; i < METSUKE_GRID_CELLS; i++) assert(out[4 + i] == 0);
}

static void test_gradient_and_endpoints() {
  // Temperature rising with column: left cells cool, right cells hot. After block
  // averaging the grid spans its own min..max, so the coldest cell -> 0 and the
  // hottest -> 255, and the row is monotonic left-to-right.
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int c) { return 10.0f + (float)c; });   // 10..41 C across width
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));

  // Header min/max are the grid-cell extremes (block means), not the raw 10/41.
  int16_t mn = rd_i16(out), mx = rd_i16(out + 2);
  assert(mn < mx);

  // First row: cell 0 is the coldest -> 0, cell 7 is the hottest -> 255, monotonic.
  const uint8_t* row0 = out + 4;
  assert(row0[0] == 0 && row0[7] == 255);
  for (int c = 0; c < 7; c++) assert(row0[c] < row0[c + 1]);

  // Column-only gradient: every row is identical.
  for (int gr = 1; gr < METSUKE_GRID_H; gr++)
    assert(std::memcmp(out + 4, out + 4 + gr * METSUKE_GRID_W, METSUKE_GRID_W) == 0);
}

static void test_hot_spot_localises() {
  // One hot block in the middle should light exactly its grid cell brightest.
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int) { return 20.0f; });
  // grid cell (gr=4, gc=4) covers rows 12..14, cols 16..19.
  for (int r = 12; r < 15; r++)
    for (int c = 16; c < 20; c++)
      frame[r * MLX_W + c] = 60.0f;
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));
  int hot = out[4 + 4 * METSUKE_GRID_W + 4];
  assert(hot == 255);                       // the hot cell saturates
  assert(out[4 + 0] == 0);                  // a cool corner stays at floor
}

static void test_nan_handling() {
  // Scattered NaN pixels are skipped in the block mean; a fully-NaN frame yields
  // no valid cells and metsukePackGrid returns false (nothing to send).
  float frame[MLX_W * MLX_H];
  fill(frame, [](int r, int c) { return ((r + c) % 2) ? NAN : 30.0f; });
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));      // half-NaN is still usable
  assert(rd_i16(out) == 300 && rd_i16(out + 2) == 300);   // valid pixels all 30.0C

  fill(frame, [](int, int) { return NAN; });
  assert(!metsukePackGrid(frame, out));     // all-NaN -> no data
}

static void test_negative_temps() {
  // Cold scene with sub-zero temps: int16 min_dC carries the sign correctly.
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int c) { return -10.0f + (float)c * 0.5f; });   // -10 .. +5.5
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));
  assert(rd_i16(out) < 0);                  // min is negative
  assert(rd_i16(out + 2) > rd_i16(out));    // max above min
}

int main() {
  test_dims_and_format();
  test_uniform_frame();
  test_gradient_and_endpoints();
  test_hot_spot_localises();
  test_nan_handling();
  test_negative_temps();
  std::printf("thermal-grid-test: all assertions passed\n");
  return 0;
}
