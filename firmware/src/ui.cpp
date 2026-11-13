// OLED UI: renders a UiModel on the 128x64 SSD1306-compatible panel with U8g2.
// Allocation-free: all text is formatted into stack buffers with integer math
// (newlib's float printf can touch the heap, so floats are formatted by hand).
#include "ui.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "apperr.h"
#include "fuji.h"

namespace {

// L4: no reset pin here. ui_begin pulses OLED_RST itself, far shorter than U8g2's 300 ms.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_d(U8G2_R0, U8X8_PIN_NONE, pins::OLED_SCL, pins::OLED_SDA);
bool g_ready = false;

constexpr int W = 128;
constexpr int H = 64;
constexpr int TOAST_Y = 54;                // toast band: rows 54..63
constexpr uint32_t BLINK_MS = 5000;        // next-stage dot blinks this long after a change

// Fonts. Tiny for the top bar, small/text for detail, bold for headers, big numerals.
const uint8_t* const F_TINY = u8g2_font_4x6_tf;
const uint8_t* const F_SMALL = u8g2_font_5x7_tf;
const uint8_t* const F_TEXT = u8g2_font_6x10_tf;
const uint8_t* const F_HEAD = u8g2_font_helvB10_tr;
const uint8_t* const F_BIG = u8g2_font_helvB12_tr;
const uint8_t* const F_LOGO = u8g2_font_helvB24_tr;
const uint8_t* const F_NUM = u8g2_font_logisoso28_tn;
const uint8_t* const F_NUM_S = u8g2_font_logisoso24_tn;

// ---------- text helpers ----------

int textW(const char* s) { return s ? (int)g_d.getStrWidth(s) : 0; }

void drawAt(int x, int y, const char* s) {
  if (s && *s) g_d.drawStr(x < 0 ? 0 : x, y, s);
}
void drawRight(int xr, int y, const char* s) { drawAt(xr - textW(s), y, s); }
void drawCenter(int x0, int w, int y, const char* s) { drawAt(x0 + (w - textW(s)) / 2, y, s); }

// Returns s, or a copy in buf shortened with a trailing '.' so it fits maxw (current font).
const char* clip(const char* s, char* buf, size_t cap, int maxw) {
  if (!s) return "";
  if (textW(s) <= maxw) return s;
  size_t n = strnlen(s, cap - 2);
  while (n > 0) {
    memcpy(buf, s, n);
    buf[n] = '.';
    buf[n + 1] = 0;
    if (textW(buf) <= maxw) return buf;
    --n;
  }
  buf[0] = 0;
  return buf;
}

// Sets the first font in which s fits maxw; falls back to the last one. Returns its index.
int fitFont(const char* s, int maxw, const uint8_t* const* fonts, int n) {
  for (int i = 0; i < n; ++i) {
    g_d.setFont(fonts[i]);
    if (textW(s) <= maxw) return i;
  }
  return n - 1;
}

// One decimal place without float printf. plus: prefix '+' on positives.
void fmt1(char* b, size_t cap, float v, bool plus) {
  if (isnan(v)) { snprintf(b, cap, "--"); return; }
  long t = lroundf(v * 10.0f);
  const char* sign = t < 0 ? "-" : (plus && t > 0 ? "+" : "");
  long a = t < 0 ? -t : t;
  snprintf(b, cap, "%s%ld.%ld", sign, a / 10, a % 10);
}

void fmtRssi(char* b, size_t cap, float rssi) {
  if (rssi == 0 || isnan(rssi)) snprintf(b, cap, "--");  // apps pass NAN for "never heard"
  else snprintf(b, cap, "%ld", lroundf(rssi));
}

const char* compass8(double deg) {
  static const char* const k[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  double d = fmod(deg, 360.0);
  if (d < 0) d += 360.0;
  return k[(int)((d + 22.5) / 45.0) & 7];
}

void fmtDist(char* b, size_t cap, double m) {
  if (m < 0) { snprintf(b, cap, "--"); return; }
  long r = lround(m);
  if (r < 1000) snprintf(b, cap, "%ld m", r);
  else if (r < 10000) snprintf(b, cap, "%ld.%02ld km", r / 1000, (r % 1000) / 10);
  else snprintf(b, cap, "%ld.%ld km", r / 1000, (r % 1000) / 100);
}

void fmtDistDir(char* b, size_t cap, const UiModel& m) {
  char d[16];
  fmtDist(d, sizeof d, m.dist_m);
  if (m.dist_m < 0) snprintf(b, cap, "%s", d);
  else snprintf(b, cap, "%s %s", d, compass8(m.bearing_deg));
}

// FujiErr codes (fuji.h, relayed verbatim in AckPayload.err) and apperr codes (apperr.h).
const char* errName(uint8_t e) {
  switch (e) {
    case 1: return "CAM NOT READY";
    case 2: return "BLE WRITE FAIL";
    case 3: return "CAM NOT FOUND";
    case 4: return "SECURE FAIL";
    case 5: return "HANDSHAKE FAIL";
    case 6: return "TIMEOUT";
    case 7: return "CAM MAY HAVE FIRED";
    case apperr::NO_ACK: return "NO REPLY: MAY HAVE FIRED";
    case apperr::NO_TX: return "NOT SENT";
    case apperr::QUEUED: return "NOT SENT: LINK BUSY";
    case apperr::STORE: return "CAM BOARD STORAGE";
    case apperr::CANCELLED: return "CANCELLED";
    case apperr::BUSY: return "CAM BUSY";
    case apperr::NO_RESULT: return "NO RESULT FROM CAM";
    case apperr::STALE: return "CAM BOARD RESTARTED";
    case apperr::NEED_FRESH: return "NOT RUN: PRESS AGAIN";
    case apperr::HOLD_LOST: return "HOLD RELEASED: NO LINK";
    case apperr::GAVE_UP: return "RELEASE GAVE UP";
    default: return nullptr;
  }
}

// The camera may or may not have fired: never shown as a success or a failure. NO_ACK only
// ever follows a real transmission (never-sent is NO_TX), so the camera board may have run it;
// FujiErr::Unsure (7) = the shutter write started but was never confirmed.
bool outcomeUnknown(const UiShot& s) {
  return s.err == apperr::NO_RESULT || s.err == apperr::STALE || s.err == apperr::NO_ACK ||
         s.err == (uint8_t)FujiErr::Unsure;
}

bool isRelayView(const UiModel& m) { return m.role == Role::Camera || m.mode == Mode::Relay; }

// Only the relay handheld learns where the camera is (camera-board fix in its Acks).
bool hasCamPos(const UiModel& m) { return m.role == Role::Handheld && m.mode == Mode::Relay; }

bool linkUp(const UiModel& m) {
  return isRelayView(m) ? m.lora_ok : m.cam == CamLink::Ready;
}

// ---------- icons ----------

void drawBatt(int x, int y, uint8_t pct, uint32_t now) {
  // 11x7 body + 1x3 nub
  if (pct != 255 && pct <= 10 && ((now / 500) & 1)) return;  // blink when critical
  g_d.drawFrame(x, y, 11, 7);
  g_d.drawBox(x + 11, y + 2, 1, 3);
  if (pct == 255) {  // unknown / USB: a 3x5 '?'
    const int qx = x + 4, qy = y + 1;
    g_d.drawHLine(qx, qy, 3);
    g_d.drawPixel(qx + 2, qy + 1);
    g_d.drawPixel(qx + 1, qy + 2);
    g_d.drawPixel(qx + 1, qy + 4);
    return;
  }
  int fill = (pct > 100 ? 100 : pct) * 9 / 100;
  if (pct > 0 && fill == 0) fill = 1;
  if (fill) g_d.drawBox(x + 1, y + 1, fill, 5);
}

void drawBars(int x, uint8_t bars, bool link) {
  for (int i = 0; i < 4; ++i) {
    const int h = 3 + 2 * i, bx = x + i * 4, by = 9 - h;
    if (link && i < bars) g_d.drawBox(bx, by, 3, h);
    else g_d.drawFrame(bx, by, 3, h);
  }
}

// Inverted rounded tag; returns its width.
int drawTag(int x, int y, const char* s) {
  const int w = textW(s) + 4;
  g_d.drawRBox(x, y, w, 9, 1);
  g_d.setDrawColor(0);
  g_d.drawStr(x + 2, y + 8, s);
  g_d.setDrawColor(1);
  return w;
}

void drawTopBar(const UiModel& m, uint32_t now) {
  g_d.setFont(F_SMALL);
  const char* tag = m.role == Role::Camera ? "CAM"
                    : m.role == Role::Unset ? "SETUP"
                    : (m.ob.active && m.ob.scr.title[0]) ? m.ob.scr.title
                    : m.mode == Mode::Relay ? "H-RLY" : "H-DIR";
  int x = drawTag(0, 0, tag) + 3;
  drawBars(x, m.bars, linkUp(m));
  x += 16 + 3;

  if (isRelayView(m) && m.role != Role::Unset) {
    // Users read speed, not spreading factor: SF7-9 = FAST, SF10-12 = SLOW (long range).
    char sf[8];
    snprintf(sf, sizeof sf, m.rescue ? "%s!" : "%s", m.sf <= 9 ? "FAST" : "SLOW");
    if (m.rescue) drawTag(x, 0, sf);
    else g_d.drawStr(x + 1, 8, sf);
  }

  // Batteries, right-aligned: H = handheld, C = camera board (absent in Direct mode).
  g_d.setFont(F_TINY);
  const bool showCam = m.role == Role::Camera || m.mode == Mode::Relay;
  int bx = W - 12;
  if (showCam) {
    drawBatt(bx, 1, m.cam_batt, now);
    g_d.drawStr(bx - 4, 7, "C");
    bx -= 19;
  }
  drawBatt(bx, 1, m.hh_batt, now);
  g_d.drawStr(bx - 4, 7, "H");

  g_d.drawHLine(0, 10, W);
}

// ---------- big status glyphs (20x20 box, top-left x,y) ----------

enum class Glyph : uint8_t { Disc, Ring, NoLink, Spinner, Arrow, Diamond, Check, DoubleCheck, Cross };

void thickLine(int x0, int y0, int x1, int y1) {
  for (int d = -1; d <= 1; ++d) {
    g_d.drawLine(x0, y0 + d, x1, y1 + d);
  }
}

void drawCheck(int x, int cy, int s) {  // s = width
  thickLine(x, cy, x + s * 3 / 8, cy + s * 3 / 8);
  thickLine(x + s * 3 / 8, cy + s * 3 / 8, x + s, cy - s / 2);
}

// Radar sweep: rotating beam with a fading trail and blips that light as the beam passes.
void drawRadar(int cx, int cy, int r, uint32_t now) {
  g_d.drawCircle(cx, cy, r);
  g_d.drawPixel(cx, cy);
  for (int k = 0; k < 4; k++) g_d.drawPixel(cx + (k % 2 ? 0 : (k ? -r / 2 : r / 2)), cy + (k % 2 ? (k == 1 ? r / 2 : -r / 2) : 0));
  const float a = (float)(now % 1400) / 1400.0f * 2.0f * (float)M_PI;
  for (int t = 0; t < 5; t++) {                         // beam + trail (thinning dots)
    const float b = a - t * 0.16f;
    const int len = r - 1;
    for (int d = 2; d <= len; d += (t == 0 ? 1 : t + 1))
      g_d.drawPixel(cx + (int)lroundf(d * sinf(b)), cy - (int)lroundf(d * cosf(b)));
  }
  static const float blip[3][2] = {{0.9f, 0.55f}, {2.6f, 0.8f}, {4.4f, 0.4f}};
  for (const auto& bl : blip) {
    float d = a - bl[0];
    while (d < 0) d += 2 * (float)M_PI;
    if (d < 1.2f) {                                     // just swept: lit, fading
      const int px = cx + (int)lroundf(bl[1] * r * sinf(bl[0])), py = cy - (int)lroundf(bl[1] * r * cosf(bl[0]));
      if (d < 0.5f) g_d.drawDisc(px, py, 1); else g_d.drawPixel(px, py);
    }
  }
}

// Six-blade iris: open = 0 closed .. 1 fully open (aperture radius = open * r).
void drawIris(int cx, int cy, int r, float open, float twist) {
  g_d.setDrawColor(1);
  g_d.drawDisc(cx, cy, r);
  const float ar = open * (r - 1);
  g_d.setDrawColor(0);
  if (ar >= 1) {
    for (int i = 0; i < 6; i++) {                       // hexagonal aperture
      const float a0 = twist + i * (float)M_PI / 3, a1 = a0 + (float)M_PI / 3;
      g_d.drawTriangle(cx, cy, cx + (int)lroundf(ar * cosf(a0)), cy + (int)lroundf(ar * sinf(a0)),
                       cx + (int)lroundf(ar * cosf(a1)), cy + (int)lroundf(ar * sinf(a1)));
    }
  }
  for (int i = 0; i < 6; i++) {                         // blade edges, tangent from the aperture
    const float a0 = twist + i * (float)M_PI / 3;
    const int x0 = cx + (int)lroundf(ar * cosf(a0)), y0 = cy + (int)lroundf(ar * sinf(a0));
    const float t = a0 + 1.2f;
    g_d.drawLine(x0, y0, cx + (int)lroundf(r * cosf(t)), cy + (int)lroundf(r * sinf(t)));
  }
  g_d.setDrawColor(1);
  g_d.drawCircle(cx, cy, r);
}

constexpr uint32_t FIRE_ANIM_MS = 380;
uint32_t g_fire_at = 0;          // last shot the UI has animated (stage change time)

// Shot fired: a small iris snaps shut and opens again in the headline's glyph spot, inline,
// while the result text is already showing (no overlay, nothing to wait for).
bool drawFire(uint32_t now) {
  if (!g_fire_at || now - g_fire_at > FIRE_ANIM_MS) return false;
  const float u = (float)(now - g_fire_at) / (float)FIRE_ANIM_MS;    // 0..1
  const float open = u < 0.4f ? 1.0f - u / 0.4f : (u - 0.4f) / 0.6f;
  g_d.setDrawColor(0);
  g_d.drawBox(1, 12, 23, 23);
  drawIris(12, 23, 10, open, u * 1.4f);
  return true;
}

void drawGlyph(Glyph g, int x, int y, uint32_t now) {
  const int cx = x + 10, cy = y + 10;
  switch (g) {
    case Glyph::Disc:
      g_d.drawDisc(cx, cy, 8);
      break;
    case Glyph::Ring:
      g_d.drawCircle(cx, cy, 8);
      g_d.drawCircle(cx, cy, 7);
      break;
    case Glyph::NoLink:
      g_d.drawCircle(cx, cy, 8);
      g_d.drawCircle(cx, cy, 7);
      thickLine(cx - 6, cy + 6, cx + 6, cy - 6);
      break;
    case Glyph::Spinner:
      drawRadar(cx, cy, 9, now);
      break;
    case Glyph::Arrow:
      g_d.drawBox(x + 1, cy - 2, 11, 5);
      g_d.drawTriangle(x + 10, cy - 9, x + 20, cy, x + 10, cy + 9);
      break;
    case Glyph::Diamond:
      g_d.drawTriangle(cx, cy - 10, cx + 10, cy, cx, cy + 10);
      g_d.drawTriangle(cx, cy - 10, cx - 10, cy, cx, cy + 10);
      break;
    case Glyph::Check:
      drawCheck(x + 1, cy, 18);
      break;
    case Glyph::DoubleCheck:
      drawCheck(x - 2, cy + 1, 12);
      drawCheck(x + 8, cy + 1, 12);
      break;
    case Glyph::Cross:
      thickLine(x + 3, y + 3, x + 17, y + 17);
      thickLine(x + 3, y + 17, x + 17, y + 3);
      break;
  }
}

// Glyph at left, bold text to its right, vertically centred on rows 13..33.
void drawHeadline(Glyph g, const char* text, uint32_t now) {
  drawGlyph(g, 2, 13, now);
  static const uint8_t* const fonts[] = {F_BIG, F_HEAD, F_TEXT};
  const int maxw = W - 27;
  const int fi = fitFont(text, maxw, fonts, 3);
  char buf[32];
  drawAt(27, fi == 0 ? 30 : 29, clip(text, buf, sizeof buf, maxw));
}

// ---------- pages ----------

struct StageView {
  Glyph glyph;
  const char* text;
};

StageView stageView(const UiModel& m) {
  if (m.shot.pending && m.shot.stage == ShotStage::Idle)
    return {Glyph::Spinner, m.mode == Mode::Relay ? "QUEUED" : "WAITING CAM"};
  if (m.shot.focus_only) {  // half-press: an AF outcome, never a shot
    // A focus write can never fire the shutter: an unanswered one is "AF ?", not UNKNOWN.
    if (m.shot.stage != ShotStage::Idle && outcomeUnknown(m.shot)) return {Glyph::Ring, "AF ?"};
    switch (m.shot.stage) {
      // Only the BLE focus write was acknowledged; nothing here observes AF lock.
      case ShotStage::Accepted:
      case ShotStage::CamEvent: return {Glyph::Check, "AF SENT"};
      case ShotStage::Failed: return {Glyph::Cross, "AF FAIL"};
      case ShotStage::Sent:
      case ShotStage::Delivered: return {Glyph::Arrow, "FOCUS"};
      case ShotStage::Idle: break;
    }
  }
  if (m.shot.stage != ShotStage::Idle && outcomeUnknown(m.shot)) return {Glyph::Ring, "UNKNOWN"};
  // A late Ack proves only that the camera board got it; a check needs the camera's word.
  if (m.shot.late && m.shot.stage == ShotStage::Accepted && millis() - m.shot.at_ms <= 4000) return {Glyph::Check, "PRESSED"};
  if (m.shot.late && m.shot.stage == ShotStage::Delivered) return {Glyph::Diamond, "LATE: AT CAM"};
  // A good outcome shows for 4 s, then the screen goes back to READY. Failures and unknowns stay.
  const bool good = m.shot.stage == ShotStage::Accepted || m.shot.stage == ShotStage::CamEvent;
  if (!(good && m.shot.at_ms && millis() - m.shot.at_ms > 4000)) switch (m.shot.stage) {
    case ShotStage::Sent: return {Glyph::Arrow, "SENT"};
    case ShotStage::Delivered: return {Glyph::Diamond, "AT CAM"};
    case ShotStage::Accepted: return {Glyph::Check, "PRESSED"};
    case ShotStage::CamEvent: return {Glyph::DoubleCheck, "CAM EVT"};
    case ShotStage::Failed: return {Glyph::Cross, "FAILED"};
    case ShotStage::Idle: break;
  }
  // Power-on antenna gate: nothing has been sent yet, so "no link" would be a guess.
  if (m.mode == Mode::Relay && !m.lora_ok && m.link_unchecked)
    return {Glyph::Ring, "NOT CHECKED"};
  if (m.mode == Mode::Relay && !m.lora_ok && m.link_checking)
    return {Glyph::Spinner, "CHECKING"};
  if (m.mode == Mode::Relay && !m.lora_ok) return {Glyph::NoLink, "NO LINK"};
  switch (m.cam) {
    case CamLink::Ready: return {Glyph::Disc, "READY"};
    case CamLink::Connecting: return {Glyph::Spinner, "CONNECTING"};
    case CamLink::Scanning: return {Glyph::Spinner, "SEARCHING"};
    case CamLink::NotPaired:
    case CamLink::None: break;
  }
  return {Glyph::Ring, "NO CAM"};
}

// Dots for each stage on the way: relay = sent/delivered/accepted/event, direct skips delivered.
void drawProgress(const UiModel& m, uint32_t now) {
  static const ShotStage relay[] = {ShotStage::Sent, ShotStage::Delivered, ShotStage::Accepted,
                                    ShotStage::CamEvent};
  static const ShotStage direct[] = {ShotStage::Sent, ShotStage::Accepted, ShotStage::CamEvent};
  const bool r = m.mode == Mode::Relay;
  const ShotStage* steps = r ? relay : direct;
  const int n = r ? 4 : 3;

  int reached = -1;
  for (int i = 0; i < n; ++i) {
    if ((uint8_t)m.shot.stage >= (uint8_t)steps[i]) reached = i;
  }
  const int cy = 39, x0 = 8, x1 = W - 9;
  const int step = (x1 - x0) / (n - 1);
  g_d.drawHLine(x0, cy, x1 - x0);
  const bool blinkOn = (now - m.shot.at_ms) < BLINK_MS && ((now / 250) & 1);
  for (int i = 0; i < n; ++i) {
    const int x = x0 + i * step;
    if (i <= reached) {
      g_d.drawDisc(x, cy, 4);
    } else {
      g_d.setDrawColor(0);
      g_d.drawDisc(x, cy, 4);
      g_d.setDrawColor(1);
      g_d.drawCircle(x, cy, 4);
      if (i == reached + 1 && blinkOn) g_d.drawDisc(x, cy, 2);
    }
  }
}

void drawMainHandheld(const UiModel& m, uint32_t now, bool compact = false) {
  const StageView v = stageView(m);
  drawHeadline(v.glyph, v.text, now);

  char buf[40], cl[40];
  g_d.setFont(F_TEXT);
  if (m.shot.focus_only && m.shot.stage != ShotStage::Idle && outcomeUnknown(m.shot)) {
    drawCenter(0, W, 43, "AF result unknown");  // never "may have fired": focus only
  } else if (m.shot.stage == ShotStage::Failed || outcomeUnknown(m.shot)) {
    const char* e = errName(m.shot.err);
    if (!e && m.shot.err) { snprintf(buf, sizeof buf, "ERR %u", m.shot.err); e = buf; }
    if (e) drawCenter(0, W, 43, e);
  } else if (m.shot.stage == ShotStage::Idle && m.shot.pending) {
    if (m.shot.pending_until && (int32_t)(m.shot.pending_until - now) > 0)
      snprintf(buf, sizeof buf, "fires on link, %lus left",
               (unsigned long)((m.shot.pending_until - now + 999) / 1000));
    else
      snprintf(buf, sizeof buf, "not on air yet");
    drawCenter(0, W, 43, clip(buf, cl, sizeof cl, W));
  } else if (m.shot.stage == ShotStage::Idle) {
    const char* s = (m.mode == Mode::Relay && !m.lora_ok)
                        ? (m.link_unchecked   ? "press any button to connect"
                           : m.link_checking ? "asking cam board"
                                             : "no reply from cam board")
                        : m.cam_text;
    drawCenter(0, W, 43, clip(s, cl, sizeof cl, W));
  } else if (m.shot.focus_only) {
    drawCenter(0, W, 43, "half-press, no shot");
  } else {
    drawProgress(m, now);
  }

  if (compact) {  // one-button: rows 53/63 belong to the label bar; one small status row
    g_d.setFont(F_SMALL);
    drawAt(0, 51, m.ob.info);
    const bool good_ = m.shot.stage == ShotStage::Accepted || m.shot.stage == ShotStage::CamEvent;
    if (m.shot.stage != ShotStage::Idle && !(good_ && now - m.shot.at_ms > 4000)) {
      const uint32_t age = (now - m.shot.at_ms) / 1000;
      if (age < 600) snprintf(buf, sizeof buf, "%lus ago", (unsigned long)age);
      else snprintf(buf, sizeof buf, ">10m");
      drawRight(W, 51, buf);
    }
    return;
  }
  // Row 1: latency + shot count.
  if (m.shot.latency_ms) snprintf(buf, sizeof buf, "%u ms", m.shot.latency_ms);
  else snprintf(buf, sizeof buf, "-- ms");
  drawAt(0, 53, buf);
  snprintf(buf, sizeof buf, "#%u", m.shot.count);
  drawRight(W, 53, buf);

  // Row 2: distance to camera + age of the current stage.
  if (m.dist_m >= 0) {
    fmtDistDir(buf, sizeof buf, m);
    drawAt(0, 63, buf);
  }
  if (m.shot.stage != ShotStage::Idle) {
    const uint32_t age = (now - m.shot.at_ms) / 1000;
    if (age < 600) snprintf(buf, sizeof buf, "%lus ago", (unsigned long)age);
    else snprintf(buf, sizeof buf, ">10m ago");
    drawRight(W, 63, buf);
  }
}

// Link table row: label | value right-aligned at 64 | right column right-aligned at 128.
void linkRow(int y, const char* label, const char* mid, const char* right) {
  drawAt(0, y, label);
  if (mid) drawRight(66, y, mid);
  if (right) drawRight(W, y, right);
}

// Direct mode: there is no LoRa link, only BLE to the camera.
void drawLinkBle(const UiModel& m) {
  g_d.setFont(F_SMALL);
  char c[24], cl[40];
  const int ys[] = {18, 27, 36, 45, 54};
  linkRow(ys[0], "MODE", nullptr, "DIRECT BLE");
  linkRow(ys[1], "CAM", nullptr, (m.cam_text && *m.cam_text) ? m.cam_text : "--");
  if (m.ble_rssi) snprintf(c, sizeof c, "%d dBm", m.ble_rssi);
  else snprintf(c, sizeof c, "--");
  linkRow(ys[2], "BLE RSSI", nullptr, c);
  drawAt(0, ys[3], "NAME");
  drawRight(W, ys[3], clip((m.cam_name && *m.cam_name) ? m.cam_name : "--", cl, sizeof cl, W - 30));
  if (m.shot.latency_ms) snprintf(c, sizeof c, "%u ms", m.shot.latency_ms);
  else snprintf(c, sizeof c, "--");
  linkRow(ys[4], "LAST PRESS", nullptr, c);
}

void drawLink(const UiModel& m) {
  if (m.role == Role::Handheld && m.mode == Mode::Direct) {
    drawLinkBle(m);
    return;
  }
  g_d.setFont(F_SMALL);
  char a[16], b[16], c[24], d[24];
  const int ys[] = {18, 27, 36, 45, 54, 63};

  fmtRssi(a, sizeof a, m.rssi_up);
  fmt1(b, sizeof b, m.snr_up, true);
  snprintf(c, sizeof c, "%s dBm", a);
  snprintf(d, sizeof d, "SNR %s", m.rssi_up == 0 || isnan(m.rssi_up) ? "--" : b);
  linkRow(ys[0], "UP", c, d);

  fmtRssi(a, sizeof a, m.rssi_dn);
  fmt1(b, sizeof b, m.snr_dn, true);
  snprintf(c, sizeof c, "%s dBm", a);
  snprintf(d, sizeof d, "SNR %s", m.rssi_dn == 0 || isnan(m.rssi_dn) ? "--" : b);
  linkRow(ys[1], "DN", c, d);

  fmt1(a, sizeof a, m.margin_db, true);
  snprintf(c, sizeof c, "%s dB", m.lora_ok ? a : "--");
  linkRow(ys[2], "MARGIN", c, m.adaptive ? "ADAPT" : "FIXED");

  snprintf(c, sizeof c, "%u/%u", m.sf, (unsigned)radiocfg::BW_KHZ);
  snprintf(d, sizeof d, "TX %d dBm", m.tx_out_dbm);
  linkRow(ys[3], m.rescue ? "SF RSC" : "SF/BW", c, d);

  fmt1(a, sizeof a, m.success_pct, false);  // NAN (camera board: unknowable) -> "--"
  snprintf(c, sizeof c, isnan(m.success_pct) ? "%s" : "%s%%", a);
  if (m.rtt_ms) snprintf(d, sizeof d, "RTT %u ms", m.rtt_ms);
  else snprintf(d, sizeof d, "RTT --");
  linkRow(ys[4], "OK", c, d);

  if (m.ble_rssi) snprintf(c, sizeof c, "%d dBm", m.ble_rssi);
  else snprintf(c, sizeof c, "--");
  fmt1(a, sizeof a, m.duty_permille / 10.0f, false);
  snprintf(d, sizeof d, "DUTY %s%%", a);
  linkRow(ys[5], "BLE", c, d);
}

int rssiToX(int rssi) {  // -130..-40 dBm -> 1..126
  if (rssi < -130) rssi = -130;
  if (rssi > -40) rssi = -40;
  return 1 + (rssi + 130) * 125 / 90;
}

void drawHunt(const UiModel& m) {
  char buf[24], b2[16];
  if (m.hunt_rssi == 0) snprintf(buf, sizeof buf, "---");
  else snprintf(buf, sizeof buf, "%d", m.hunt_rssi);
  g_d.setFont(F_NUM);
  const int nw = textW(buf);
  g_d.setFont(F_TEXT);
  const int uw = textW("dBm") + 3;
  const int x = (W - nw - uw) / 2;
  g_d.setFont(F_NUM);
  drawAt(x, 41, buf);
  g_d.setFont(F_TEXT);
  drawAt(x + nw + 3, 41, "dBm");

  // Bar -130..-40 with 10 dB ticks underneath.
  const int by = 45;
  g_d.drawFrame(0, by, W, 7);
  if (m.hunt_rssi != 0) g_d.drawBox(1, by + 1, rssiToX(m.hunt_rssi) - 1, 5);
  for (int r = -120; r <= -50; r += 10) g_d.drawPixel(rssiToX(r), by + 7);
  if (m.hunt_best > -200) {
    const int bx = rssiToX(m.hunt_best);
    g_d.setDrawColor(2);
    g_d.drawVLine(bx, by - 2, 11);
    g_d.setDrawColor(1);
    g_d.drawTriangle(bx - 3, by - 3, bx + 3, by - 3, bx, by);
  }

  g_d.setFont(F_SMALL);
  if (m.hunt_best > -200) snprintf(buf, sizeof buf, "BEST %d", m.hunt_best);
  else snprintf(buf, sizeof buf, "BEST --");
  drawAt(0, 63, buf);
  fmt1(b2, sizeof b2, m.margin_db, true);
  snprintf(buf, sizeof buf, "MRG %s dB", m.lora_ok ? b2 : "--");
  drawRight(W, 63, buf);
}

void drawCompass(const UiModel& m, int cx, int cy, int r) {
  g_d.drawCircle(cx, cy, r);
  g_d.setFont(F_TINY);
  drawCenter(cx - 5, 10, cy - r + 7, "N");
  if (m.dist_m < 0) {
    g_d.setFont(F_HEAD);
    drawCenter(cx - 10, 20, cy + 5, "?");
    return;
  }
  const float b = (float)(m.bearing_deg * M_PI / 180.0);
  const float dx = sinf(b), dy = -cosf(b);   // unit vector toward the camera
  const float px = -dy, py = dx;             // perpendicular
  const float tipL = r - 3, tailL = r - 8, headL = 8, headW = 5;
  const int tx = cx + (int)lroundf(dx * tipL), ty = cy + (int)lroundf(dy * tipL);
  const int ax = cx - (int)lroundf(dx * tailL), ay = cy - (int)lroundf(dy * tailL);
  const float hx = cx + dx * (tipL - headL), hy = cy + dy * (tipL - headL);
  g_d.drawLine(ax, ay, (int)lroundf(hx), (int)lroundf(hy));
  g_d.drawLine(ax + 1, ay, (int)lroundf(hx) + 1, (int)lroundf(hy));
  g_d.drawLine(ax, ay + 1, (int)lroundf(hx), (int)lroundf(hy) + 1);
  g_d.drawTriangle(tx, ty, (int)lroundf(hx + px * headW), (int)lroundf(hy + py * headW),
                   (int)lroundf(hx - px * headW), (int)lroundf(hy - py * headW));
}

void drawGps(const UiModel& m) {
  char buf[24], h[12];
  g_d.setFont(F_HEAD);
  drawAt(0, 24, !m.gps_on ? "GPS OFF" : m.hh_fix ? "FIX" : "NO FIX");

  g_d.setFont(F_TEXT);
  fmt1(h, sizeof h, m.hdop, false);
  snprintf(buf, sizeof buf, "SAT %u HD %s", m.sats, m.hh_fix ? h : "--");
  drawAt(0, 35, buf);
  if (!hasCamPos(m)) {
    // Camera board, or direct mode: no camera position exists here. Show our own fix.
    if (m.hh_fix) {
      snprintf(buf, sizeof buf, "%.5f", m.own_lat);
      drawAt(0, 49, buf);
      snprintf(buf, sizeof buf, "%.5f", m.own_lon);
      drawAt(0, 61, buf);
    } else {
      g_d.setFont(F_SMALL);
      drawAt(0, 49, m.role == Role::Camera ? "own position only" : "no camera position");
      drawAt(0, 58, m.role == Role::Camera ? "" : "in direct mode");
    }
    return;
  }
  drawAt(0, 46, m.cam_fix ? "CAM FIX" : "CAM NO FIX");

  fmtDistDir(buf, sizeof buf, m);
  static const uint8_t* const fonts[] = {F_HEAD, F_TEXT};
  fitFont(buf, 80, fonts, 2);
  drawAt(0, 62, buf);

  drawCompass(m, 106, 37, 20);
}

void drawRange(const UiModel& m) {
  char buf[24], a[12];
  g_d.setFont(F_HEAD);
  drawAt(0, 24, "RANGE LOG");
  const char* st = m.range_on ? "ON" : "OFF";
  const int w = textW(st) + 8;
  if (m.range_on) {
    g_d.drawRBox(W - w, 13, w, 14, 2);
    g_d.setDrawColor(0);
    drawAt(W - w + 4, 25, st);
    g_d.setDrawColor(1);
  } else {
    g_d.drawRFrame(W - w, 13, w, 14, 2);
    drawAt(W - w + 4, 25, st);
  }

  struct Row { const char* label; const char* val; };
  snprintf(buf, sizeof buf, "%u", m.range_n);
  fmt1(a, sizeof a, m.range_ok_pct, false);
  char pct[16];
  snprintf(pct, sizeof pct, "%s%%", m.range_n ? a : "--");
  char dist[24];
  fmtDistDir(dist, sizeof dist, m);
  const Row rows[] = {{"ROWS", buf}, {"SUCCESS", pct}, {"DIST", dist}};
  const int nrows = hasCamPos(m) ? 3 : 2;  // no distance without a camera position
  for (int i = 0; i < nrows; ++i) {
    const int y = 39 + i * 12;
    g_d.setFont(F_TEXT);
    drawAt(0, y, rows[i].label);
    g_d.setFont(F_HEAD);
    drawRight(W, y, rows[i].val);
  }
}

Glyph camGlyph(CamLink c) {
  switch (c) {
    case CamLink::Ready: return Glyph::Check;
    case CamLink::Scanning:
    case CamLink::Connecting: return Glyph::Spinner;
    case CamLink::NotPaired: return Glyph::Cross;
    case CamLink::None: break;
  }
  return Glyph::Ring;
}

void drawCameraPage(const UiModel& m, uint32_t now) {
  const char* t = (m.cam_text && *m.cam_text) ? m.cam_text : "NO CAMERA";
  drawHeadline(camGlyph(m.cam), t, now);

  char buf[32], cl[32], a[12], b[12];
  g_d.setFont(F_TEXT);
  drawAt(0, 43, clip((m.cam_name && *m.cam_name) ? m.cam_name : "no camera saved", cl, sizeof cl, W));

  if (m.ble_rssi) snprintf(buf, sizeof buf, "BLE %d", m.ble_rssi);
  else snprintf(buf, sizeof buf, "BLE --");
  drawAt(0, 53, buf);
  fmtRssi(a, sizeof a, m.rssi_up);
  fmt1(b, sizeof b, m.snr_up, true);
  if (m.rssi_up == 0 || isnan(m.rssi_up)) snprintf(buf, sizeof buf, "LR --");
  else snprintf(buf, sizeof buf, "LR %s/%s", a, b);
  drawRight(W, 53, buf);

  // This board's own fix (hh_fix carries it on both roles; cam_fix is the relay handheld's
  // view of the camera board and is always false here).
  drawAt(0, 63, !m.gps_on ? "GPS OFF" : m.hh_fix ? "GPS FIX" : "GPS NO FIX");
  if (m.cam_batt == 255) snprintf(buf, sizeof buf, "BAT --");
  else snprintf(buf, sizeof buf, "BAT %u%%", m.cam_batt);
  drawRight(W, 63, buf);
}

// ---------- overlays ----------

void drawMenu(const UiMenu& mn) {
  char buf[32], cl[32];
  g_d.drawBox(0, 0, W, 11);
  g_d.setDrawColor(0);
  g_d.setFont(F_TEXT);
  drawAt(3, 9, clip(mn.title, cl, sizeof cl, W - 30));
  g_d.setFont(F_SMALL);
  if (mn.n) {
    snprintf(buf, sizeof buf, "%u/%u", (unsigned)mn.sel + 1, (unsigned)mn.n);
    drawRight(W - 2, 8, buf);
  }
  g_d.setDrawColor(1);

  constexpr int ROWS = 5, RH = 10, Y0 = 13;
  const int n = mn.n > UI_MENU_MAX ? UI_MENU_MAX : mn.n;
  int first = (int)mn.sel - ROWS / 2;
  if (first > n - ROWS) first = n - ROWS;
  if (first < 0) first = 0;
  const bool bar = n > ROWS;
  const int right = bar ? W - 4 : W;

  g_d.setFont(F_TEXT);
  for (int r = 0; r < ROWS && first + r < n; ++r) {
    const int i = first + r, y = Y0 + r * RH;
    const bool sel = i == mn.sel;
    if (sel) {
      g_d.drawRBox(0, y, right, RH, 1);
      g_d.setDrawColor(0);
    }
    const char* val = mn.values[i];
    const int vw = val ? textW(val) : 0;
    drawAt(3, y + 8, clip(mn.items[i], cl, sizeof cl, right - vw - 9));
    if (val) drawRight(right - 2, y + 8, val);
    g_d.setDrawColor(1);
  }
  if (bar) {
    const int th = Y0 + ROWS * RH - Y0;
    for (int y = Y0; y < Y0 + th; y += 2) g_d.drawPixel(W - 2, y);
    const int tl = th * ROWS / n, ty = Y0 + (th - tl) * first / (n - ROWS);
    g_d.drawBox(W - 3, ty, 3, tl);
  }
}

bool numericOnly(const char* s) {
  for (; *s; ++s) {
    if (!((*s >= '0' && *s <= '9') || *s == ' ' || *s == '-' || *s == '.' || *s == ':')) return false;
  }
  return true;
}

// Splits s into two lines that fit maxw (current font); second line is clipped.
void wrap2(const char* s, int maxw, char* l1, char* l2, size_t cap) {
  l1[0] = l2[0] = 0;
  if (textW(s) <= maxw) {
    snprintf(l1, cap, "%s", s);
    return;
  }
  int cut = -1;
  for (int i = 0; s[i] && i < (int)cap - 1; ++i) {
    if (s[i] != ' ') continue;
    memcpy(l1, s, i);
    l1[i] = 0;
    if (textW(l1) > maxw) break;
    cut = i;
  }
  if (cut < 0) {
    char tmp[48];
    snprintf(l1, cap, "%s", clip(s, tmp, sizeof tmp, maxw));
    return;
  }
  memcpy(l1, s, cut);
  l1[cut] = 0;
  char tmp[48];
  snprintf(l2, cap, "%s", clip(s + cut + 1, tmp, sizeof tmp, maxw));
}

void drawBig(const UiModel& m) {
  g_d.drawRFrame(0, 0, W, H, 3);
  const char* b1 = m.big1 ? m.big1 : "";
  const bool has2 = m.big2 && *m.big2;
  char cl[40];

  int y1;
  if (*b1 && numericOnly(b1)) {
    static const uint8_t* const nf[] = {F_NUM, F_NUM_S, F_BIG};
    fitFont(b1, W - 8, nf, 3);
    y1 = has2 ? 38 : 46;
  } else {
    static const uint8_t* const tf[] = {F_BIG, F_HEAD, F_TEXT};
    fitFont(b1, W - 8, tf, 3);
    y1 = has2 ? 26 : 38;
  }
  drawCenter(0, W, y1, clip(b1, cl, sizeof cl, W - 8));

  if (!has2) return;
  g_d.setFont(F_TEXT);
  char l1[40], l2[40];
  wrap2(m.big2, W - 8, l1, l2, sizeof l1);
  if (l2[0]) {
    drawCenter(0, W, y1 + 12, l1);
    drawCenter(0, W, y1 + 22, l2);
  } else {
    drawCenter(0, W, y1 + 15, l1);
  }
}

void drawToast(const char* t) {
  char cl[40];
  g_d.setDrawColor(0);
  g_d.drawHLine(0, TOAST_Y - 1, W);
  g_d.setDrawColor(1);
  g_d.drawBox(0, TOAST_Y, W, H - TOAST_Y);
  g_d.setDrawColor(0);
  static const uint8_t* const fonts[] = {F_TEXT, F_SMALL};
  fitFont(t, W - 4, fonts, 2);
  drawCenter(0, W, H - 2, clip(t, cl, sizeof cl, W - 4));
  g_d.setDrawColor(1);
}

// ---------- one-button screens ----------

// The label bar: what a tap / hold / letting go does now. Always the bottom 10 rows.
void drawBar(const char* t) {
  char cl[40];
  g_d.setDrawColor(0);
  g_d.drawBox(0, TOAST_Y - 1, W, H - TOAST_Y + 1);
  g_d.setDrawColor(1);
  g_d.drawBox(0, TOAST_Y, W, H - TOAST_Y);
  g_d.setDrawColor(0);
  static const uint8_t* const fonts[] = {F_TEXT, F_SMALL};
  fitFont(t, W - 4, fonts, 2);
  drawCenter(0, W, H - 2, clip(t, cl, sizeof cl, W - 4));
  g_d.setDrawColor(1);
}

void drawProgressBar(int y, int16_t p) {
  g_d.drawFrame(0, y, W, 5);
  const int fill = p <= 0 ? 0 : (p >= 1000 ? W - 2 : (W - 2) * p / 1000);
  if (fill) g_d.drawBox(1, y + 1, fill, 3);
}

void drawObText(const ObScreen& o) {
  char cl[40];
  int rw = 0;
  if (o.right[0]) {
    g_d.setFont(F_SMALL);
    rw = textW(o.right) + 3;
    drawRight(W, 24, o.right);
  }
  static const uint8_t* const fonts[] = {F_BIG, F_HEAD, F_TEXT};
  fitFont(o.big, W - rw, fonts, 3);
  drawAt(0, 25, clip(o.big, cl, sizeof cl, W - rw));
  g_d.setFont(F_SMALL);
  if (o.progress >= 0) {
    drawProgressBar(29, o.progress);
    drawAt(0, 43, clip(o.l1, cl, sizeof cl, W));
    drawAt(0, 51, clip(o.l2, cl, sizeof cl, W));
  } else {
    drawAt(0, 35, clip(o.l1, cl, sizeof cl, W));
    drawAt(0, 43, clip(o.l2, cl, sizeof cl, W));
    drawAt(0, 51, clip(o.l3, cl, sizeof cl, W));
  }
}

void drawObCount(const ObScreen& o) {
  char cl[40];
  static const uint8_t* const nf[] = {F_NUM, F_NUM_S, F_BIG};
  if (numericOnly(o.big)) fitFont(o.big, W, nf, 3);
  else g_d.setFont(F_BIG);
  drawCenter(0, W, 41, o.big);
  if (o.progress >= 0) drawProgressBar(43, o.progress);
  g_d.setFont(F_SMALL);
  drawAt(0, 52, clip(o.l1, cl, sizeof cl, W));
}

void drawObList(const ObScreen& o) {
  char cl[40];
  g_d.drawBox(0, 0, W, 9);
  g_d.setDrawColor(0);
  g_d.setFont(F_SMALL);
  drawAt(2, 7, clip(o.head, cl, sizeof cl, W - 4));
  g_d.setDrawColor(1);
  constexpr int ROWS = 4, RH = 11, Y0 = 10;
  const int n = o.n > OB_LIST_MAX ? OB_LIST_MAX : o.n;
  int first = (int)o.sel - 1;
  if (first > n - ROWS) first = n - ROWS;
  if (first < 0) first = 0;
  g_d.setFont(F_TEXT);
  for (int r = 0; r < ROWS && first + r < n; ++r) {
    const int i = first + r, y = Y0 + r * RH;
    const bool sel = i == o.sel;
    if (sel) {
      g_d.drawRBox(0, y, W, RH, 1);
      g_d.setDrawColor(0);
    }
    const int vw = o.val[i][0] ? textW(o.val[i]) : 0;
    drawAt(3, y + 9, clip(o.item[i], cl, sizeof cl, W - vw - 9));
    if (o.val[i][0]) drawRight(W - 2, y + 9, o.val[i]);
    g_d.setDrawColor(1);
  }
}

void drawObAlert(const UiModel& m) {
  g_d.drawBox(0, 0, W, 11);
  g_d.setDrawColor(0);
  g_d.setFont(F_TEXT);
  drawCenter(0, W, 9, "! CHECK CAMERA !");
  g_d.setDrawColor(1);
  g_d.setFont(F_HEAD);
  drawCenter(0, W, 26, "RELEASE NOT");
  drawCenter(0, W, 40, "CONFIRMED");
  g_d.setFont(F_SMALL);
  drawCenter(0, W, 50, m.ob.alert_line);
}

void drawOb(const UiModel& m, uint32_t now) {
  const ObScreen& o = m.ob.scr;
  if (m.ob.alert) {
    drawObAlert(m);
    drawBar("ANY PRESS: OK");
    return;
  }
  switch (o.kind) {
    case ObKind::List: drawObList(o); break;
    case ObKind::Text:
      drawTopBar(m, now);
      drawObText(o);
      break;
    case ObKind::Count:
      drawTopBar(m, now);
      drawObCount(o);
      break;
    case ObKind::Home:
    default:
      drawTopBar(m, now);
      switch (m.page) {
        case Page::Link: drawLink(m); break;
        case Page::Hunt: drawHunt(m); break;
        case Page::Gps: drawGps(m); break;
        case Page::Range: drawRange(m); break;
        default: drawMainHandheld(m, now, true); break;
      }
      break;
  }
  // A view page owns its bottom rows except while a press is down or just after it opened.
  if (o.kind == ObKind::Home && m.page != Page::Main && !m.ob.view_bar) return;
  // A toast replaces the bar for its two seconds; the app never sends one during a press.
  if (!(m.toast && *m.toast)) drawBar(o.bar);
}

void drawPage(const UiModel& m, uint32_t now) {
  drawTopBar(m, now);
  switch (m.page) {
    case Page::Link: drawLink(m); break;
    case Page::Hunt: drawHunt(m); break;
    case Page::Gps: drawGps(m); break;
    case Page::Range: drawRange(m); break;
    case Page::Camera: drawCameraPage(m, now); break;
    case Page::Main:
    case Page::Count:
    default:
      if (m.role == Role::Camera) drawCameraPage(m, now);
      else drawMainHandheld(m, now);
      break;
  }
}

}  // namespace

void ui_begin() {
  g_d.setBusClock(400000);
  // L4: our own reset. Held LOW while the just-switched Vext rail settles, then released.
  // The SSD1306/SSD1315 needs 3 us of reset; 20 ms low + 10 ms after is a wide margin
  // and still saves about 270 ms on every boot and wake.
  digitalWrite(pins::OLED_RST, LOW);
  pinMode(pins::OLED_RST, OUTPUT);
  delay(20);
  digitalWrite(pins::OLED_RST, HIGH);
  delay(10);
  // U8g2's init helper waits 100+100+100 ms even with no reset pin: zero its timings.
  static u8x8_display_info_t info = *g_d.getU8x8()->display_info;
  info.reset_pulse_width_ms = 0;
  info.post_reset_wait_ms = 0;
  g_d.getU8x8()->display_info = &info;
  g_d.begin();
  g_d.setContrast(255);
  g_d.setFontMode(1);
  g_d.setFontPosBaseline();
  g_d.clearBuffer();
  g_d.sendBuffer();
  g_ready = true;
}

void ui_power(bool on) {
  if (g_ready) g_d.setPowerSave(on ? 0 : 1);
}

void ui_contrast(uint8_t c) {
  if (g_ready) g_d.setContrast(c);
}

void ui_render(const UiModel& m, uint32_t now) {
  if (!g_ready) return;
  g_d.clearBuffer();
  g_d.setDrawColor(1);
  if (m.menu.active) drawMenu(m.menu);
  else if (m.big1 || m.big2) drawBig(m);
  else if (m.ob.active && m.role == Role::Handheld) drawOb(m, now);
  else drawPage(m, now);
  if (m.toast && *m.toast) drawToast(m.toast);
  const bool fired = !m.menu.active && !m.big1 && !m.shot.focus_only && m.shot.at_ms &&
                     (m.shot.stage == ShotStage::Accepted || m.shot.stage == ShotStage::CamEvent);
  static uint32_t seen = 0;
  if (fired && m.shot.at_ms != seen && m.shot.stage == ShotStage::Accepted && now - m.shot.at_ms < 300) {
    seen = m.shot.at_ms;
    g_fire_at = now;
  }
  drawFire(now);
  g_d.sendBuffer();
}

bool ui_animating(uint32_t now) { return g_fire_at && now - g_fire_at <= FIRE_ANIM_MS; }

void ui_splash(bool camera, const char* ver) {
  if (!g_ready) return;
  const uint32_t t0 = millis();
  for (;;) {
    const uint32_t t = millis() - t0;
    if (t > 1700) break;
    g_d.clearBuffer();
    g_d.setDrawColor(1);
    if (t < 650) {                                        // iris opens
      drawIris(64, 32, 26, (float)t / 650.0f, (float)t / 650.0f * 2.0f);
    } else {
      const float u = (float)(t - 650) / 1050.0f;         // 0..1
      g_d.setFont(F_LOGO);
      const int w = textW("SLR");
      const int x = (W - w) / 2;
      const int sl = x + (int)((w + 8) * (u < 0.4f ? u / 0.4f : 1.0f));   // wipe in
      drawAt(x, 30, "SLR");
      g_d.setDrawColor(0);
      g_d.drawBox(sl, 0, W - sl, 34);
      g_d.setDrawColor(1);
      if (u >= 0.4f) {
        g_d.setFont(F_TEXT);
        drawCenter(0, W, 44, "SUPER LONG RANGE");
        drawCenter(0, W, 54, camera ? "REMOTE  CAMERA UNIT" : "REMOTE");
        const int bw = (int)(W * ((u - 0.4f) / 0.6f));
        g_d.drawHLine(0, 63, bw);                         // range line runs out
        if (u > 0.75f) drawRight(W, 63 - 2, ver);
      }
    }
    g_d.sendBuffer();
    delay(25);
  }
}

void ui_about(const char* const* lines, uint8_t n) {
  if (!g_ready) return;
  g_d.clearBuffer();
  g_d.setDrawColor(1);
  g_d.drawBox(0, 0, W, 13);
  g_d.setDrawColor(0);
  g_d.setFont(F_HEAD);
  drawCenter(0, W, 11, "SLR REMOTE");
  g_d.setDrawColor(1);
  g_d.setFont(F_TEXT);
  for (uint8_t i = 0; i < n && i < 5; i++) drawAt(0, 23 + i * 10, lines[i]);
  g_d.sendBuffer();
}
