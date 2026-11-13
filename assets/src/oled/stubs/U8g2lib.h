// Thin C++ wrapper over the real U8g2 C library with a dummy bus, so the firmware's own
// drawing code renders into a memory buffer on a PC.
#pragma once
#include <stdint.h>
extern "C" {
#include "clib/u8g2.h"
}
#ifndef U8X8_PIN_NONE
#define U8X8_PIN_NONE 255
#endif
void oled_on_send(u8g2_t* u);   // called on every sendBuffer()

class U8G2_SSD1306_128X64_NONAME_F_HW_I2C {
 public:
  u8g2_t u;
  static uint8_t cb(u8x8_t*, uint8_t, uint8_t, void*) { return 1; }
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C(const u8g2_cb_t* rot, uint8_t, uint8_t, uint8_t) {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u, rot, cb, cb);
  }
  void setBusClock(uint32_t) {}
  u8x8_t* getU8x8() { return u8g2_GetU8x8(&u); }
  void begin() { u8g2_InitDisplay(&u); u8g2_ClearDisplay(&u); u8g2_SetPowerSave(&u, 0); }
  void setContrast(uint8_t) {}
  void setPowerSave(uint8_t) {}
  void setFontMode(uint8_t m) { u8g2_SetFontMode(&u, m); }
  void setFontPosBaseline() { u8g2_SetFontPosBaseline(&u); }
  void clearBuffer() { u8g2_ClearBuffer(&u); }
  void sendBuffer() { oled_on_send(&u); }
  void setFont(const uint8_t* f) { u8g2_SetFont(&u, f); }
  void setDrawColor(uint8_t c) { u8g2_SetDrawColor(&u, c); }
  int getStrWidth(const char* s) { return u8g2_GetStrWidth(&u, s); }
  void drawStr(int x, int y, const char* s) { u8g2_DrawStr(&u, x, y, s); }
  void drawFrame(int x, int y, int w, int h) { u8g2_DrawFrame(&u, x, y, w, h); }
  void drawBox(int x, int y, int w, int h) { u8g2_DrawBox(&u, x, y, w, h); }
  void drawRBox(int x, int y, int w, int h, int r) { u8g2_DrawRBox(&u, x, y, w, h, r); }
  void drawRFrame(int x, int y, int w, int h, int r) { u8g2_DrawRFrame(&u, x, y, w, h, r); }
  void drawHLine(int x, int y, int w) { u8g2_DrawHLine(&u, x, y, w); }
  void drawVLine(int x, int y, int h) { u8g2_DrawVLine(&u, x, y, h); }
  void drawPixel(int x, int y) { u8g2_DrawPixel(&u, x, y); }
  void drawLine(int a, int b, int c, int d) { u8g2_DrawLine(&u, a, b, c, d); }
  void drawCircle(int x, int y, int r) { u8g2_DrawCircle(&u, x, y, r, U8G2_DRAW_ALL); }
  void drawDisc(int x, int y, int r) { u8g2_DrawDisc(&u, x, y, r, U8G2_DRAW_ALL); }
  void drawTriangle(int a, int b, int c, int d, int e, int f) { u8g2_DrawTriangle(&u, a, b, c, d, e, f); }
};
