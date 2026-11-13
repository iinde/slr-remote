// One console line built in RAM and sent with a single Serial.write, so a log line printed
// by another task (the fuji BLE worker runs on core 0) can never land inside it. HWCDC locks
// per write call, not per line: a JSON reply made of many printf calls could be split.
#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

class LineBuf {
 public:
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (over_) return;
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(buf_ + n_, sizeof buf_ - n_, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= sizeof buf_ - n_) over_ = true;
    else n_ += (size_t)w;
  }
  void put(char c) {
    if (n_ + 1 < sizeof buf_) buf_[n_++] = c;
    else over_ = true;
  }
  // A JSON string value: quotes and backslashes escaped, control characters dropped.
  void jstr(const char* k, const char* v, bool comma = true) {
    printf("\"%s\":\"", k);
    for (const char* p = v ? v : ""; *p; p++) {
      if (*p == '"' || *p == '\\') put('\\');
      if ((uint8_t)*p >= 0x20) put(*p);
    }
    put('"');
    if (comma) put(',');
  }
  // Sends the line and its newline in one write. Too long: one short error line instead,
  // never half a JSON object.
  void send() {
    if (over_) {
      Serial.println(F("{\"error\":\"reply too long\"}"));
    } else {
      buf_[n_++] = '\n';  // room kept: put()/printf() stop one short of the end
      Serial.write((const uint8_t*)buf_, n_);
    }
    n_ = 0;
    over_ = false;
  }

 private:
  char buf_[1024];
  size_t n_ = 0;
  bool over_ = false;
};
