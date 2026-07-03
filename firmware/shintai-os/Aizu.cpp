#include "Aizu.h"
#include <Adafruit_NeoPixel.h>

// ── The onboard NeoPixel sink — the one v1 output. Aizu is the SOLE writer of the
// pixel and the sole owner of its power pin (AZ-4). Uses the board's PIN_NEOPIXEL
// / NEOPIXEL_POWER macros — never a hardcoded GPIO (the S3 must drive
// NEOPIXEL_POWER HIGH to power the pixel at all).
namespace {

class NeoPixelSink : public AizuSink {
 public:
  void begin() override {
#ifdef NEOPIXEL_POWER
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
    px_.begin();
    px_.setBrightness(255);      // Aizu does its own gamma+cap; keep the strip linear
    px_.clear();
    px_.show();
  }

  void render(const AizuRenderState& s) override {
    if (!s.visible || s.muted || s.level == 0) {
      px_.setPixelColor(0, 0, 0, 0);
    } else {
      AizuColour c = aizuScaleColour(s.colour, s.level);
      px_.setPixelColor(0, px_.Color(c.r, c.g, c.b));
    }
    px_.show();
  }

 private:
  Adafruit_NeoPixel px_{1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800};
};

NeoPixelSink neoSink;
const AizuColour kIdleGreen = {0, 255, 0};

}  // namespace

AizuClass Aizu;

void AizuClass::begin() {
  for (int i = 0; i < AIZU_SOURCE_COUNT; i++) cues_[i].active = false;

  // GPIO0 (BOOT) doubles as the bootloader strap, so claim it as input only now,
  // after boot (AZ-9). External strap pull-up present; pressed reads LOW.
  pinMode(0, INPUT_PULLUP);

  addSink(&neoSink);
  for (int i = 0; i < sinkCount_; i++) sinks_[i]->begin();

  lastRenderMs_  = millis();
  motionStartMs_ = lastRenderMs_;
}

bool AizuClass::addSink(AizuSink* sink) {
  if (sink == nullptr || sinkCount_ >= MAX_SINKS) return false;
  sinks_[sinkCount_++] = sink;
  return true;
}

void AizuClass::onHold(AizuHoldHandler handler) { holdHandler_ = handler; }

void AizuClass::postCue(AizuSource src, int priority, AizuColour colour,
                        AizuMotion motion, uint32_t maxAgeMs) {
  if (src < 0 || src >= AIZU_SOURCE_COUNT) return;
  AizuCue& c = cues_[src];
  c.active     = true;
  c.source     = src;
  c.priority   = priority;
  c.colour     = colour;
  c.motion     = motion;
  c.maxAgeMs   = maxAgeMs;
  c.postedAtMs = millis();
}

void AizuClass::clearCue(AizuSource src) {
  if (src < 0 || src >= AIZU_SOURCE_COUNT) return;
  cues_[src].active = false;
}

void AizuClass::tick() {
  uint32_t now = millis();
  serviceButton(now);                                     // sample every call — stays responsive
  if ((uint32_t)(now - lastRenderMs_) < AIZU_RENDER_MS) return;
  lastRenderMs_ = now;
  renderTick(now);
}

void AizuClass::renderTick(uint32_t now) {
  // 1) Pick the winner (highest-priority live cue), then apply anti-flicker.
  int winner    = aizuPickWinner(cues_, AIZU_SOURCE_COUNT, now);
  int candPrio  = (winner < 0) ? AIZU_PRIO_IDLE : cues_[winner].priority;
  int candSrc   = (winner < 0) ? -1 : (int)cues_[winner].source;

  // Effective priority of what's shown: if its source went stale/inactive, treat
  // it as Idle so a release back to a lower cue reads as upward (instant, AC-3).
  int shownEffPrio = AIZU_PRIO_IDLE;
  if (shownSource_ >= 0 && aizuCueLive(cues_[shownSource_], now))
    shownEffPrio = cues_[shownSource_].priority;

  if (candSrc != shownSource_ || candPrio != shownPriority_) {
    bool sameSource = (candSrc == shownSource_);
    if (aizuShouldSwitch(shownEffPrio, candPrio, sameSource,
                         (uint32_t)(now - lastSwitchMs_), AIZU_DEBOUNCE_MS)) {
      if (candSrc != shownSource_) motionStartMs_ = now;   // restart the motion cycle
      shownSource_   = candSrc;
      shownPriority_ = candPrio;
      lastSwitchMs_  = now;
    }
  }

  // 2) Build the render state for whatever is shown.
  AizuRenderState st = {};
  st.muted    = muted_;
  st.priority = shownPriority_;
  st.source   = (shownSource_ < 0) ? AIZU_SYSTEM : (AizuSource)shownSource_;
  uint32_t phase = now - motionStartMs_;

  if (muted_) {
    st.visible = false;                                    // mute is absolute (AZ-3/AZ-8)
  } else if (shownSource_ < 0) {
    // ── Idle wallpaper (AZ-2). Tethered: dim green breathe. Field (!Serial):
    // dark + a ~30 s heartbeat so the wearer knows it's alive.
    st.colour = kIdleGreen;
    if ((bool)Serial) {
      st.motion   = AIZU_BREATHE;
      st.periodMs = AIZU_IDLE_BREATHE_MS;
    } else {
      st.motion   = AIZU_HEARTBEAT;
      st.periodMs = AIZU_IDLE_HEARTBEAT_MS;
    }
    st.level   = aizuLevel(aizuEnvelope(st.motion, st.periodMs, phase), AIZU_IDLE_BRIGHT);
    st.visible = (st.level > 0);
  } else {
    const AizuCue& c = cues_[shownSource_];
    st.colour   = c.colour;
    st.motion   = c.motion.kind;
    st.periodMs = c.motion.periodMs;
    st.level    = aizuLevel(aizuEnvelope(c.motion.kind, c.motion.periodMs, phase), AIZU_MAX_BRIGHT);
    st.visible  = true;
  }

  // 3) Fan the same resolved state out to every registered sink.
  for (int i = 0; i < sinkCount_; i++) sinks_[i]->render(st);
}

// Debounced BOOT-button gesture layer: CLICK (short press-release) toggles mute;
// a press held past AIZU_HOLD_MS fires HOLD_START then HOLD_END to the subscriber.
// Duration separates the two, so mute and hold-to-measure share the one button.
void AizuClass::serviceButton(uint32_t now) {
  bool raw = (digitalRead(0) == LOW);                      // pressed = LOW
  if (raw != btnRaw_) { btnRaw_ = raw; btnRawSinceMs_ = now; }

  if ((uint32_t)(now - btnRawSinceMs_) >= AIZU_BTN_DEBOUNCE_MS && raw != btnStable_) {
    btnStable_ = raw;
    if (btnStable_) {                                       // press edge
      btnPressStart_ = now;
      holdFired_     = false;
    } else {                                               // release edge
      if (holdFired_) {
        if (holdHandler_) holdHandler_(AIZU_HOLD_END);
      } else {
        muted_ = !muted_;                                  // CLICK -> toggle mute
      }
    }
  }

  // Cross into HOLD once the (debounced) press outlasts the threshold.
  if (btnStable_ && !holdFired_ && (uint32_t)(now - btnPressStart_) >= AIZU_HOLD_MS) {
    holdFired_ = true;
    if (holdHandler_) holdHandler_(AIZU_HOLD_START);
  }
}
