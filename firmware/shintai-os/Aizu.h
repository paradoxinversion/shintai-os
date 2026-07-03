#ifndef AIZU_H
#define AIZU_H

// Aizu (合図) — the shared on-body output bus (specs/platform/aizu.md).
//
// Sources POST cues; Aizu arbitrates and RENDERS the winner on the one onboard
// NeoPixel (and, later, a haptic sink). No source ever touches the pixel or the
// BOOT button directly — Aizu is the sole writer of the pixel and sole reader of
// GPIO0 (AZ-4). This header is the whole public surface: the cue bus, the input
// gesture layer, and the output-sink registry. The pure math is in AizuCore.h.

#include <Arduino.h>
#include "AizuCore.h"

// Tunables. AIZU_MAX_BRIGHT generalises Kehai's KEHAI_MAX_BRIGHT (eye comfort +
// battery); render tick ~20 ms / ~50 fps (AZ-6); idle heartbeat ~30 s (KD-2).
static const uint8_t  AIZU_MAX_BRIGHT        = 40;     // 0..255 peak for active cues
static const uint8_t  AIZU_IDLE_BRIGHT       = 12;     // dimmer cap for the idle wallpaper
static const uint16_t AIZU_RENDER_MS         = 20;     // render tick period (~50 fps)
static const uint16_t AIZU_DEBOUNCE_MS       = 250;    // downward/near-equal arbitration dwell
static const uint16_t AIZU_IDLE_BREATHE_MS   = 3500;   // tethered idle breathe period
static const uint16_t AIZU_IDLE_HEARTBEAT_MS = 30000;  // field idle heartbeat interval

// BOOT button (GPIO0). CLICK toggles mute; a press held past AIZU_HOLD_MS is a
// HOLD routed to a subscriber. GPIO0 doubles as the bootloader strap, so it's
// only claimed as input in begin() (after boot).
static const uint16_t AIZU_BTN_DEBOUNCE_MS = 25;
static const uint16_t AIZU_HOLD_MS         = 400;

// Gesture events routed to the HOLD subscriber — the input twin of the cue bus
// (AZ-9). First subscriber is Nesshi (hold-to-measure).
enum AizuGesture { AIZU_HOLD_START, AIZU_HOLD_END };
typedef void (*AizuHoldHandler)(AizuGesture ev);

// The resolved state handed to every output sink each render tick. A sink maps it
// to its modality: the NeoPixel uses colour*level; a future DRV2605 haptic maps
// motion/priority to a vibration pattern — no source or arbiter change (AZ-5).
struct AizuRenderState {
  bool           visible;   // false => blank (muted, or field-idle between heartbeats)
  AizuColour     colour;    // hue of the winning cue (or idle green)
  uint8_t        level;     // 0..255 master brightness for THIS tick (gamma+cap applied)
  AizuMotionKind motion;    // winning motion kind
  uint16_t       periodMs;
  int            priority;  // winning priority (ALERT/etc. -> haptic class)
  AizuSource     source;
  bool           muted;
};

// An output modality. v1 registers one (the onboard NeoPixel) in begin(); a
// second (haptic) can be added with addSink() and no core change (AC-11).
class AizuSink {
 public:
  virtual void begin() {}
  virtual void render(const AizuRenderState& s) = 0;
  virtual ~AizuSink() {}
};

class AizuClass {
 public:
  void begin();   // NeoPixel + power pin + button; registers the NeoPixel sink
  void tick();    // call every loop(); self-rate-limits rendering to AIZU_RENDER_MS

  // The cue bus — a source posts/refreshes/withdraws its one slot (posting
  // replaces that source's slot). Posting is cheap; Aizu renders on its own clock.
  void postCue(AizuSource src, int priority, AizuColour colour, AizuMotion motion, uint32_t maxAgeMs);
  void clearCue(AizuSource src);

  // Input & mute.
  void onHold(AizuHoldHandler handler);          // register the HOLD subscriber
  bool muted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }

  // Output sinks (begin() auto-registers the NeoPixel).
  bool addSink(AizuSink* sink);

 private:
  void renderTick(uint32_t now);
  void serviceButton(uint32_t now);

  AizuCue cues_[AIZU_SOURCE_COUNT];

  static const int MAX_SINKS = 4;
  AizuSink* sinks_[MAX_SINKS];
  int sinkCount_ = 0;

  uint32_t lastRenderMs_ = 0;

  // Arbitration / anti-flicker state.
  int      shownSource_   = -1;              // rendered AizuSource index, or -1 (idle)
  int      shownPriority_ = AIZU_PRIO_IDLE;
  uint32_t motionStartMs_ = 0;               // origin of the current motion cycle
  uint32_t lastSwitchMs_  = 0;

  bool muted_ = false;

  // Button gesture state.
  AizuHoldHandler holdHandler_ = nullptr;
  bool     btnStable_     = false;           // debounced pressed state
  bool     btnRaw_        = false;
  uint32_t btnRawSinceMs_ = 0;
  uint32_t btnPressStart_ = 0;
  bool     holdFired_     = false;
};

extern AizuClass Aizu;

#endif  // AIZU_H
