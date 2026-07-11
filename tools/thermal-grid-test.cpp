// Host unit tests for Metsuke's pure thermal pack (ThermalGrid.h): the full-res 32x24
// canonical grid, the normalisation + little-endian wire format, the chunk slicing the
// consumer reassembles, and the temporal denoise / steadied-range clarity pass.
//
// Same posture as kanki-band-test.cpp / kehai-band-test.cpp: the firmware has no on-target
// harness, so the decidable-from-values logic is checked on the host with no board.
//
//   c++ -std=c++17 -Wall -o /tmp/thermal-test tools/thermal-grid-test.cpp && /tmp/thermal-test

#include "../firmware/shintai-os/ThermalGrid.h"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>

static const int W = METSUKE_GRID_W;   // 32
static const int H = METSUKE_GRID_H;   // 24

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
  assert(W == 32 && H == 24);
  assert(METSUKE_GRID_CELLS == 768);
  assert(METSUKE_GRID_BYTES == 772);              // canonical reassembled grid
  assert(METSUKE_CHUNKS == 4);
  assert(METSUKE_CHUNK_ROWS == 6);                // 24 / 4
  assert(METSUKE_CHUNK_CELLS == 192);             // 32 * 6
  assert(METSUKE_CHUNK_BYTES == 199);             // 7 header + 192 cells
  assert(METSUKE_CHUNK_BYTES <= 244);             // fits one notification at MTU 247
  assert(METSUKE_CHUNKS * METSUKE_CHUNK_CELLS == METSUKE_GRID_CELLS);   // full coverage
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
  // Temperature rising with column: each row is 10..41 C across the 32 columns. The 3x3
  // smooth leaves the interior of a linear ramp unchanged and only nudges the clamped
  // edges inward (10.0 -> ~10.3, 41.0 -> ~40.7), so the coldest cell is still 0, the
  // hottest 255, every row identical, monotonic left-to-right.
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int c) { return 10.0f + (float)c; });   // 10..41 C across width
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));
  assert(rd_i16(out) >= 100 && rd_i16(out) <= 106);          // ~10.3 C (edge nudged up)
  assert(rd_i16(out + 2) >= 404 && rd_i16(out + 2) <= 410);  // ~40.7 C (edge nudged down)
  const uint8_t* row0 = out + 4;
  assert(row0[0] == 0 && row0[W - 1] == 255);
  for (int c = 0; c < W - 1; c++) assert(row0[c] < row0[c + 1]);
  for (int gr = 1; gr < H; gr++)                              // every row identical
    assert(std::memcmp(out + 4, out + 4 + gr * W, W) == 0);
}

static void test_hot_spot_localises() {
  // One hot pixel in a cool field: after the 3x3 smooth it spreads into a small blob and
  // its peak is attenuated (40 -> 25 C), but the centre is still the max cell and far cells
  // floor. (A single-pixel hot object is softened — the price of the noise smoothing.)
  float frame[MLX_W * MLX_H];
  constexpr int hr = 12, hc = 16;
  fill(frame, [](int r, int c) { return (r == hr && c == hc) ? 40.0f : 20.0f; });
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));
  assert(out[4 + hr * W + hc] == 255);   // the blob centre is still the peak
  assert(out[4 + 0] == 0);               // a far cool corner stays at floor
  assert(rd_i16(out) == 200 && rd_i16(out + 2) == 250);   // 20.0 C .. attenuated 25.0 C
}

static void test_nan_handling() {
  // Scattered NaN pixels are now FILLED from valid neighbours (no black holes): a NaN/30
  // checkerboard fills to all 30 C. A fully-NaN frame still yields no data (skip).
  float frame[MLX_W * MLX_H];
  fill(frame, [](int r, int c) { return ((r + c) % 2) ? NAN : 30.0f; });
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));                      // half-NaN filled + usable
  assert(rd_i16(out) == 300 && rd_i16(out + 2) == 300);     // all cells resolve to 30.0C

  fill(frame, [](int, int) { return NAN; });
  assert(!metsukePackGrid(frame, out));                     // all-NaN -> no data
}

static void test_dead_pixel_filled() {
  // A single dead (NaN) pixel in a gradient must be FILLED from neighbours, not left as a
  // black hole: its cell lands at a sensible mid-level, nowhere near the 0 a hole would be.
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int c) { return 10.0f + (float)c; });   // 10..41 C
  frame[12 * MLX_W + 16] = NAN;                               // knock out one pixel (~26 C)
  uint8_t out[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, out));
  uint8_t cell = out[4 + 12 * W + 16];
  assert(cell > 60 && cell < 200);   // filled to ~mid-scale, NOT a 0 hole
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

// ── Chunking: each frame is sliced into METSUKE_CHUNKS wire chunks the consumer reassembles.

static void test_chunks_slice_and_reassemble() {
  float frame[MLX_W * MLX_H];
  fill(frame, [](int r, int c) { return 15.0f + r * 0.5f + c * 0.25f; });   // varies by row+col
  uint8_t grid[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(frame, grid));

  const uint8_t seq = 42;
  uint8_t recon[METSUKE_GRID_CELLS];
  for (int idx = 0; idx < METSUKE_CHUNKS; idx++) {
    uint8_t ch[METSUKE_CHUNK_BYTES];
    metsukeChunk(grid, seq, idx, ch);
    assert(ch[0] == seq);                         // frame_seq stamped
    assert(ch[1] == idx);                         // chunk_index
    assert(ch[2] == METSUKE_CHUNKS);              // chunk_count
    assert(std::memcmp(ch + 3, grid, 4) == 0);    // min/max repeated verbatim
    // this chunk's 192 cells == grid rows [idx*6, +6)
    assert(std::memcmp(ch + METSUKE_CHUNK_HEADER,
                       grid + 4 + idx * METSUKE_CHUNK_CELLS, METSUKE_CHUNK_CELLS) == 0);
    std::memcpy(recon + idx * METSUKE_CHUNK_CELLS, ch + METSUKE_CHUNK_HEADER, METSUKE_CHUNK_CELLS);
  }
  // Reassembled cells == the canonical grid's cells, exactly.
  assert(std::memcmp(recon, grid + 4, METSUKE_GRID_CELLS) == 0);
}

// ── Perceived-clarity pass: temporal denoise + steadied range (metsukePackGridFiltered).

static void test_filter_seeds_verbatim() {
  // The first frame after a reset must pass through UNCHANGED (EMA seeds to the sample),
  // so a fresh subscriber sees the true scene at once — byte-identical to the stateless pack.
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int c) { return 10.0f + (float)c; });
  uint8_t stateless[METSUKE_GRID_BYTES], filtered[METSUKE_GRID_BYTES];
  MetsukeFilter f; metsukeFilterReset(&f);
  assert(metsukePackGrid(frame, stateless));
  assert(metsukePackGridFiltered(frame, &f, filtered));
  assert(std::memcmp(stateless, filtered, METSUKE_GRID_BYTES) == 0);
}

static void test_filter_attenuates_transient_then_converges() {
  MetsukeFilter f; metsukeFilterReset(&f);
  uint8_t out[METSUKE_GRID_BYTES];

  float flat[MLX_W * MLX_H];
  fill(flat, [](int, int) { return 20.0f; });
  for (int k = 0; k < 20; k++) assert(metsukePackGridFiltered(flat, &f, out));
  assert(rd_i16(out) == 200 && rd_i16(out + 2) == 200);

  // A single-frame hot BLOCK (6x6 — big enough that its interior survives the 3x3 smooth,
  // unlike a lone pixel). Stateless reports the full 40 C max; the temporal denoise
  // attenuates the transient on the frame it appears.
  float spike[MLX_W * MLX_H];
  fill(spike, [](int r, int c) {
    return (r >= 9 && r <= 14 && c >= 13 && c <= 18) ? 40.0f : 20.0f;
  });
  uint8_t st[METSUKE_GRID_BYTES];
  assert(metsukePackGrid(spike, st));
  assert(rd_i16(st + 2) == 400);                              // stateless: block interior = full 40 C
  assert(metsukePackGridFiltered(spike, &f, out));
  assert(rd_i16(out + 2) > 200 && rd_i16(out + 2) < 400);     // filtered: attenuated

  // Sustained -> the estimate converges to the true 40 C.
  for (int k = 0; k < 60; k++) assert(metsukePackGridFiltered(spike, &f, out));
  assert(std::abs(rd_i16(out + 2) - 400) <= 2);
}

static void test_filter_nan_frame_is_safe() {
  float frame[MLX_W * MLX_H];
  fill(frame, [](int, int c) { return 15.0f + (float)c * 0.5f; });
  uint8_t out[METSUKE_GRID_BYTES];
  MetsukeFilter f; metsukeFilterReset(&f);
  assert(metsukePackGridFiltered(frame, &f, out));

  float nanframe[MLX_W * MLX_H];
  fill(nanframe, [](int, int) { return NAN; });
  assert(!metsukePackGridFiltered(nanframe, &f, out));   // no data -> false
  assert(metsukePackGridFiltered(frame, &f, out));       // filter still works
}

int main() {
  test_dims_and_format();
  test_uniform_frame();
  test_gradient_and_endpoints();
  test_hot_spot_localises();
  test_nan_handling();
  test_dead_pixel_filled();
  test_negative_temps();
  test_chunks_slice_and_reassemble();
  test_filter_seeds_verbatim();
  test_filter_attenuates_transient_then_converges();
  test_filter_nan_frame_is_safe();
  std::printf("thermal-grid-test: all assertions passed\n");
  return 0;
}
