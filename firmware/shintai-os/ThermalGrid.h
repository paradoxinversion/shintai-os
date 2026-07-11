#ifndef THERMAL_GRID_H
#define THERMAL_GRID_H

// ThermalGrid — the pure, hardware-free frame->grid->chunk pack for Metsuke (目付)
// (specs/zokyo/metsuke.md). No sensor, no BLE: it turns the MLX90640's 32x24 (768 px)
// frame into the binary Thermal Grid the glasses/phone render as a false-colour panel.
// Keeping it Arduino-free makes the normalise + chunk logic host-testable
// (tools/thermal-grid-test.cpp); the sketch supplies the live frame and the BLE.
//
// Full native resolution (MD-5): the grid is the sensor's whole 32x24 — no 2x2 block
// average — so 768 cells + a 4-byte header = a 772-byte "canonical grid" that does NOT
// fit one notification (even at the max negotiable MTU). It is therefore sent as
// METSUKE_CHUNKS chunks the consumer reassembles by frame_seq (see CONTRACT.md
// "Thermal Grid"). This replaced the earlier single-packet 16x12 grid — 4x the pixels.
//
//   canonical grid (772 B, what a consumer reassembles to):
//     [0..1] int16 min_dC   (min cell temp * 10, little-endian, signed)
//     [2..3] int16 max_dC    (max cell temp * 10, little-endian, signed)
//     [4..]  768 x uint8      row-major 32x24, cell = round((t-min)/(max-min) * 255)
//   chunk on the wire (199 B, one notification):
//     [0] frame_seq  [1] chunk_index  [2] chunk_count(=4)
//     [3..4] min_dC  [5..6] max_dC     (frame-global, repeated in every chunk)
//     [7..] 192 x uint8  rows [chunk_index*6, +6) of the grid
//
// min/max are the grid's own range so the cells span the full palette; the consumer
// auto-ranges to them. A flat scene (max==min) packs all cells to 0.

#include <stdint.h>
#include <string.h>
#include <math.h>

static const int MLX_W = 32, MLX_H = 24;                          // source frame dims
static const int METSUKE_GRID_W = 32, METSUKE_GRID_H = 24;        // full native res (MD-5; no downsample)
static const int METSUKE_GRID_CELLS = METSUKE_GRID_W * METSUKE_GRID_H;   // 768
static const int METSUKE_GRID_BYTES = 4 + METSUKE_GRID_CELLS;           // 772 (canonical reassembled grid)

static const int METSUKE_CHUNKS       = 4;                              // chunks per frame
static const int METSUKE_CHUNK_ROWS   = METSUKE_GRID_H / METSUKE_CHUNKS; // 6 rows per chunk (24/4)
static const int METSUKE_CHUNK_CELLS  = METSUKE_GRID_W * METSUKE_CHUNK_ROWS; // 192
static const int METSUKE_CHUNK_HEADER = 7;                              // seq + idx + cnt + min + max
static const int METSUKE_CHUNK_BYTES  = METSUKE_CHUNK_HEADER + METSUKE_CHUNK_CELLS; // 199 (fits MTU 247)

// ── Spatial clean-up (MD-6): what the old 2x2 block-average did implicitly, done
// explicitly now that the grid is full 32x24. Two passes:
//   1. dead-pixel fill  — a NaN/dead cell becomes the mean of its valid 8-neighbours,
//      so a bad pixel no longer shows as a black HOLE (the average used to dilute it);
//   2. gentle 3x3 smooth — a [1 2 1; 2 4 2; 1 2 1]/16 kernel tames the per-pixel noise
//      full res no longer averages away, edges clamped (renormalised by weight used).
// Both operate on °C cells before the temporal denoise + range below.

// Fill NaN cells from valid 8-neighbours, in place. Two passes so a small cluster fills
// from its edges inward; a cell with NO valid neighbours stays NaN (packs to 0).
static inline void metsukeFillDead(float* cells) {
  for (int pass = 0; pass < 2; pass++) {
    bool filled = false;
    for (int r = 0; r < METSUKE_GRID_H; r++)
      for (int c = 0; c < METSUKE_GRID_W; c++) {
        int i = r * METSUKE_GRID_W + c;
        if (!isnan(cells[i])) continue;
        float sum = 0.0f; int n = 0;
        for (int dr = -1; dr <= 1; dr++)
          for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int rr = r + dr, cc = c + dc;
            if (rr < 0 || rr >= METSUKE_GRID_H || cc < 0 || cc >= METSUKE_GRID_W) continue;
            float v = cells[rr * METSUKE_GRID_W + cc];
            if (!isnan(v)) { sum += v; n++; }
          }
        if (n > 0) { cells[i] = sum / n; filled = true; }
      }
    if (!filled) break;
  }
}

// Gentle 3x3 Gaussian-ish smooth, src -> dst (dst must differ from src).
static inline void metsukeSmooth(const float* src, float* dst) {
  static const int K[3][3] = { {1, 2, 1}, {2, 4, 2}, {1, 2, 1} };
  for (int r = 0; r < METSUKE_GRID_H; r++)
    for (int c = 0; c < METSUKE_GRID_W; c++) {
      float sum = 0.0f; int wsum = 0;
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
          int rr = r + dr, cc = c + dc;
          if (rr < 0 || rr >= METSUKE_GRID_H || cc < 0 || cc >= METSUKE_GRID_W) continue;
          float v = src[rr * METSUKE_GRID_W + cc];
          if (isnan(v)) continue;
          int w = K[dr + 1][dc + 1];
          sum += w * v; wsum += w;
        }
      dst[r * METSUKE_GRID_W + c] = (wsum > 0) ? (sum / wsum) : NAN;
    }
}

static float metsukeScratch[METSUKE_GRID_CELLS];   // smooth work buffer (single-threaded use)

// Frame -> cleaned °C cells: copy, dead-pixel fill, then spatial smooth. Returns the
// valid-cell count (0 = no usable data — an all-NaN frame the caller must skip).
static inline int metsukeCleanCells(const float* frame, float* out) {
  int valid = 0;
  for (int i = 0; i < METSUKE_GRID_CELLS; i++) {
    out[i] = frame[i];
    if (!isnan(frame[i])) valid++;
  }
  if (valid == 0) return 0;
  metsukeFillDead(out);
  metsukeSmooth(out, metsukeScratch);
  for (int i = 0; i < METSUKE_GRID_CELLS; i++) out[i] = metsukeScratch[i];
  return valid;
}

// Min/max over the valid (non-NaN) cells. Returns the valid-cell count (0 = none).
static inline int metsukeRange(const float* cells, float* minOut, float* maxOut) {
  float mn = INFINITY, mx = -INFINITY;
  int valid = 0;
  for (int i = 0; i < METSUKE_GRID_CELLS; i++) {
    float v = cells[i];
    if (isnan(v)) continue;
    valid++; if (v < mn) mn = v; if (v > mx) mx = v;
  }
  if (valid == 0) { *minOut = 0.0f; *maxOut = 0.0f; return 0; }
  *minOut = mn; *maxOut = mx;
  return valid;
}

// Write the 772-byte canonical grid: int16 min/max header + 768 normalised cells. NaN
// cells and a flat scene (span 0) pack to 0.
static inline void metsukeWriteGrid(const float* cells, float mn, float mx, uint8_t* out) {
  int16_t minDc = (int16_t)lroundf(mn * 10.0f);
  int16_t maxDc = (int16_t)lroundf(mx * 10.0f);
  out[0] = (uint8_t)(minDc & 0xFF);
  out[1] = (uint8_t)((minDc >> 8) & 0xFF);
  out[2] = (uint8_t)(maxDc & 0xFF);
  out[3] = (uint8_t)((maxDc >> 8) & 0xFF);

  float span = mx - mn;
  for (int i = 0; i < METSUKE_GRID_CELLS; i++) {
    uint8_t b = 0;
    float v = cells[i];
    if (span > 0.0f && !isnan(v)) {
      float norm = (v - mn) / span * 255.0f;
      if (norm < 0.0f) norm = 0.0f;
      if (norm > 255.0f) norm = 255.0f;
      b = (uint8_t)lroundf(norm);
    }
    out[4 + i] = b;
  }
}

// Stateless full-res pack: frame -> cleaned (dead-pixel-filled + smoothed) -> the 772-byte
// canonical grid. Returns false when the frame has no valid data. (The sketch uses the
// filtered variant below; this is the no-temporal-denoise reference the host tests check
// the wire format + spatial clean-up + normalisation against.)
static inline bool metsukePackGrid(const float* frame, uint8_t* out) {
  float cells[METSUKE_GRID_CELLS];
  if (metsukeCleanCells(frame, cells) == 0) return false;
  float mn, mx;
  if (metsukeRange(cells, &mn, &mx) == 0) return false;
  metsukeWriteGrid(cells, mn, mx, out);
  return true;
}

// Slice the canonical grid `grid` (772 B) into wire chunk `idx` (0..METSUKE_CHUNKS-1),
// stamping it with `frameSeq`. `out` must hold METSUKE_CHUNK_BYTES (199). Chunk i carries
// grid rows [i*METSUKE_CHUNK_ROWS, +METSUKE_CHUNK_ROWS); min/max are copied into every chunk.
static inline void metsukeChunk(const uint8_t* grid, uint8_t frameSeq, int idx, uint8_t* out) {
  out[0] = frameSeq;
  out[1] = (uint8_t)idx;
  out[2] = (uint8_t)METSUKE_CHUNKS;
  out[3] = grid[0]; out[4] = grid[1];   // min_dC
  out[5] = grid[2]; out[6] = grid[3];   // max_dC
  memcpy(out + METSUKE_CHUNK_HEADER, grid + 4 + idx * METSUKE_CHUNK_CELLS, METSUKE_CHUNK_CELLS);
}

// ── Perceived-clarity pass (temporal denoise + steadied range) ───────────────────
// The MLX90640 at 8 Hz has visible per-frame speckle, and at full 32x24 there is no
// 2x2 block-average to smooth it spatially — so the temporal denoise below matters
// MORE than it did for the old 16x12 grid. Packing each frame's own min/max also makes
// the consumer's palette "pump" as hot/cold cells flicker. Two one-pole EMAs fix both,
// emitting the identical canonical grid / chunk format:
//   1. per-cell temporal denoise  — smooths each of the 768 cells over time;
//   2. steadied palette range     — EMAs the min/max bounds so the palette is stable.
// State lives in a caller-owned MetsukeFilter (explicit, host-testable); the sketch keeps
// one and resets it on each fresh (re)subscribe. Set an alpha to 1.0 to disable a stage.

static const float METSUKE_CELL_ALPHA  = 0.5f;   // per-cell denoise weight (1.0 = off)
static const float METSUKE_RANGE_ALPHA = 0.3f;   // palette-bound smoothing (1.0 = off)

struct MetsukeFilter {
  float cells[METSUKE_GRID_CELLS];   // EMA-smoothed cell temps (°C); NAN = unseeded
  float loC, hiC;                    // EMA-smoothed palette bounds (°C); NAN = unseeded
};

// Seed to "unseeded" so the first frame is adopted verbatim (no cold-start toward 0 °C).
static inline void metsukeFilterReset(MetsukeFilter* f) {
  for (int i = 0; i < METSUKE_GRID_CELLS; i++) f->cells[i] = NAN;
  f->loC = f->hiC = NAN;
}

// One-pole EMA that seeds on the first real sample and HOLDS through NaN inputs (a
// dropped/NaN cell contributes no new information, it doesn't decay the estimate).
static inline float metsukeEma(float prev, float sample, float alpha) {
  if (isnan(sample)) return prev;
  if (isnan(prev))   return sample;
  return alpha * sample + (1.0f - alpha) * prev;
}

// Frame -> spatial clean-up (dead-pixel fill + smooth) -> temporal denoise -> steadied
// palette range -> the 772-byte canonical grid (same format as metsukePackGrid). Returns
// false (filter untouched) when the frame has no valid data. `f` must persist across
// calls; reset it on a fresh subscribe.
static inline bool metsukePackGridFiltered(const float* frame, MetsukeFilter* f, uint8_t* out) {
  float cleaned[METSUKE_GRID_CELLS];
  if (metsukeCleanCells(frame, cleaned) == 0) return false;   // dead-pixel fill + 3x3 smooth

  // 1) Per-cell temporal denoise (on top of the spatial clean-up).
  for (int i = 0; i < METSUKE_GRID_CELLS; i++)
    f->cells[i] = metsukeEma(f->cells[i], cleaned[i], METSUKE_CELL_ALPHA);

  // 2) Range from the denoised cells, then EMA the bounds (order lo<=hi is preserved).
  float mn, mx;
  if (metsukeRange(f->cells, &mn, &mx) == 0) return false;
  f->loC = metsukeEma(f->loC, mn, METSUKE_RANGE_ALPHA);
  f->hiC = metsukeEma(f->hiC, mx, METSUKE_RANGE_ALPHA);

  // 3) Pack against the smoothed bounds.
  metsukeWriteGrid(f->cells, f->loC, f->hiC, out);
  return true;
}

#endif  // THERMAL_GRID_H
