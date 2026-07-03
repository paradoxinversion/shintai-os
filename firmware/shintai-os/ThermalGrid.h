#ifndef THERMAL_GRID_H
#define THERMAL_GRID_H

// ThermalGrid — the pure, hardware-free frame->grid downsample + binary pack for
// Metsuke (目付) (specs/zokyo/metsuke.md). No sensor, no BLE: it reduces the
// MLX90640's 32x24 (768 px) frame to a coarse 8x8 grid and packs it into the
// 68-byte notification the glasses render as a false-colour heat panel. Keeping
// it Arduino-free makes the block-average + normalisation host-testable
// (tools/thermal-grid-test.cpp); the sketch supplies the live frame and the BLE.
//
// Wire format (MD-2 — the contract's FIRST binary characteristic; every other
// characteristic is a UTF-8 string):
//   [0..1] int16 min_dC  (min cell temp * 10, little-endian, signed)
//   [2..3] int16 max_dC  (max cell temp * 10, little-endian, signed)
//   [4..67] 64 x uint8   row-major 8x8, cell = round((t-min)/(max-min) * 255)
// Total 68 bytes. min/max are the DOWNSAMPLED grid's own range (not the raw
// frame's) so the 0..255 cells span the full palette — best contrast when the
// glasses auto-range (AC-2). A flat scene (max==min) packs all cells to 0.

#include <stdint.h>
#include <math.h>

static const int MLX_W = 32, MLX_H = 24;                    // source frame dims
static const int METSUKE_GRID_W = 8, METSUKE_GRID_H = 8;    // v1 grid (MD-1)
static const int METSUKE_GRID_CELLS = METSUKE_GRID_W * METSUKE_GRID_H;  // 64
static const int METSUKE_GRID_BYTES = 4 + METSUKE_GRID_CELLS;           // 68

// Block-average the 32x24 frame to 8x8 (each cell = mean of its 4-wide x 3-tall
// source block, skipping NaN pixels). Writes cellsOut[64] row-major (NaN for an
// all-NaN block) and the min/max over valid cells. Returns the valid-cell count;
// 0 means no usable thermal data (caller should not emit).
static inline int metsukeDownsample(const float* frame, float* cellsOut,
                                    float* minOut, float* maxOut) {
  const int bw = MLX_W / METSUKE_GRID_W;   // 4 source cols per cell
  const int bh = MLX_H / METSUKE_GRID_H;   // 3 source rows per cell
  float mn = INFINITY, mx = -INFINITY;
  int validCells = 0;
  for (int gr = 0; gr < METSUKE_GRID_H; gr++) {
    for (int gc = 0; gc < METSUKE_GRID_W; gc++) {
      float sum = 0.0f; int n = 0;
      for (int r = 0; r < bh; r++) {
        for (int c = 0; c < bw; c++) {
          float v = frame[(gr * bh + r) * MLX_W + (gc * bw + c)];
          if (isnan(v)) continue;
          sum += v; n++;
        }
      }
      float cell = (n > 0) ? (sum / n) : NAN;
      if (n > 0) {
        validCells++;
        if (cell < mn) mn = cell;
        if (cell > mx) mx = cell;
      }
      cellsOut[gr * METSUKE_GRID_W + gc] = cell;
    }
  }
  if (validCells == 0) { *minOut = 0.0f; *maxOut = 0.0f; return 0; }
  *minOut = mn; *maxOut = mx;
  return validCells;
}

// Downsample + pack into out68 (must hold METSUKE_GRID_BYTES). Returns false when
// the frame has no valid data (nothing to send). NaN cells and a flat scene pack
// to 0.
static inline bool metsukePackGrid(const float* frame, uint8_t* out68) {
  float cells[METSUKE_GRID_CELLS];
  float mn, mx;
  if (metsukeDownsample(frame, cells, &mn, &mx) == 0) return false;

  int16_t minDc = (int16_t)lroundf(mn * 10.0f);
  int16_t maxDc = (int16_t)lroundf(mx * 10.0f);
  out68[0] = (uint8_t)(minDc & 0xFF);
  out68[1] = (uint8_t)((minDc >> 8) & 0xFF);
  out68[2] = (uint8_t)(maxDc & 0xFF);
  out68[3] = (uint8_t)((maxDc >> 8) & 0xFF);

  float span = mx - mn;
  for (int i = 0; i < METSUKE_GRID_CELLS; i++) {
    uint8_t b = 0;
    if (span > 0.0f && !isnan(cells[i])) {
      float norm = (cells[i] - mn) / span * 255.0f;
      if (norm < 0.0f) norm = 0.0f;
      if (norm > 255.0f) norm = 255.0f;
      b = (uint8_t)lroundf(norm);
    }
    out68[4 + i] = b;
  }
  return true;
}

#endif  // THERMAL_GRID_H
