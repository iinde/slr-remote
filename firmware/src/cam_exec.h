// Runs ShotSequencer steps against the camera over BLE and reports only what the
// camera actually acknowledged. Shared by the camera board (relay) and the
// direct-mode handheld so both report outcomes the same way.
#pragma once
#include <Arduino.h>
#include <stdint.h>

#include "apperr.h"
#include "config.h"
#include "fuji.h"
#include "proto.h"
#include "shotseq.h"



struct CamOutcome {
  uint32_t tag;
  ShotStage stage;  // Accepted, Failed or CamEvent
  uint8_t err;
  // The outcome of a Release write, not of the press tag names: Failed (err = FujiErr) when
  // the camera did not acknowledge it (retried from then on), Failed + apperr::GAVE_UP when
  // the retries end, Accepted when a retry finally lands. Never changes a shot's stage.
  bool release = false;
};

class CamExec {
 public:
  // One press. tag identifies the command in the outcomes. false if one is running.
  bool startShot(uint32_t now, uint16_t focus_ms, uint32_t tag) {
    if (seq_.busy()) return false;
    pending(Kind::Shot, tag);
    seq_.startShot(now, focus_ms);
    return true;
  }
  void half(uint32_t now, uint32_t tag) {
    pending(Kind::Focus, tag);
    seq_.half(now);
  }
  void full(uint32_t now, uint32_t tag) {
    pending(Kind::Shot, tag);
    seq_.full(now);
  }
  // full() that lets go by itself hold_ms after the shutter write (the Release that follows
  // is then only a backup). Used for every one-button "shoot now" over relay, so a slow or
  // lost Release frame can never stretch the press into a CH burst.
  void fullTimed(uint32_t now, uint32_t tag, uint16_t hold_ms) {
    pending(Kind::Shot, tag);
    seq_.fullTimed(now, hold_ms);
  }
  // tag: what a Release outcome is reported under (0 = keep the last press's tag).
  void release(uint32_t now, uint32_t tag = 0) {
    if (kind_ == Kind::Shot && seq_.busy()) cancelPending();
    if (kind_ == Kind::Focus) cancelPending();  // its focus write never ran
    if (tag) rel_tag_ = tag;
    seq_.release(now);
  }
  // The Release written first on a new camera link. Nothing here knows whether the camera
  // still holds a press from before (a reset forgets it), so a failed write is retried too.
  void releaseOnConnect(uint32_t now) {
    connect_rel_ = true;
    release(now, 0);
  }

  // Execute every due step (each fuji call blocks until the camera answers, <= ~2 s).
  void poll(uint32_t now) {
    CamCmd c;
    for (uint8_t i = 0; i < 4 && seq_.poll(now, c); i++) {
      exec(c);
      now = millis();
      seq_.stepDone(c, now);  // AF wait and shutter hold count from the end of the write
    }
    if (seq_.buttonDown() && !down_) down_since_ = now;
    down_ = seq_.buttonDown();
    retryRelease(now);
    watchEvent(millis());
  }

  bool buttonDown() const { return seq_.buttonDown(); }
  // A ShutterPress ran and its Release has not yet (two-stage full, or one-press hold).
  bool shutterDown() const { return seq_.shutterDown(); }
  // A one-press sequence is in progress (its shutter step may still be due).
  bool oneShotRunning() const { return seq_.busy(); }
  // Shutter pressed and its Release already scheduled (one-press hold): no blocking LoRa TX
  // may start now, or the shutter stays down for the whole airtime.
  bool holdingForRelease() const { return seq_.shutterDown() && seq_.releaseQueued(); }
  uint32_t downSince() const { return down_since_; }
  // A Release the camera has not acknowledged yet is being retried.
  // S9: not the quiet retry of a failed release-on-connect: nothing is known to be held then.
  bool releasePending() const { return release_retry_ && !connect_retry_; }

  bool next(CamOutcome& out) {
    if (out_n_ == 0) return false;
    out = out_[0];
    for (uint8_t i = 1; i < out_n_; i++) out_[i - 1] = out_[i];
    out_n_--;
    return true;
  }
  bool busy() const { return seq_.busy() || kind_ != Kind::None; }

 private:
  enum class Kind : uint8_t { None, Shot, Focus };

  void pending(Kind k, uint32_t tag) {
    if (kind_ == Kind::Shot) cancelPending();  // its shutter step never ran
    kind_ = k;
    tag_ = tag;
    rel_tag_ = tag;  // a one-press's own Release step reports under the shot's tag
  }
  void cancelPending() {
    emit(tag_, ShotStage::Failed, apperr::CANCELLED);
    kind_ = Kind::None;
  }

  void exec(CamCmd c) {
    switch (c) {
      case CamCmd::FocusPress: {
        FujiErr e = fuji.focusPress();
        if (e != FujiErr::NotReady) maybe_pressed_ = true;  // a failed write may still have landed
        if (e == FujiErr::NotReady && kind_ == Kind::Shot && seq_.busy()) {
          // One-press with no camera link: nothing was written, so fail it now and drop the
          // shutter step (no AF wait, no tally for a press that never happened). The queued
          // Release only clears the sequencer's button state.
          emit(tag_, ShotStage::Failed, (uint8_t)e);
          kind_ = Kind::None;
          seq_.release(millis());
          break;
        }
        if (kind_ == Kind::Focus) {
          // Half-press: report the focus write now, not at release, so a failure is seen
          // while the button is still held (the camera board sends it after the Release).
          const bool ok = e == FujiErr::None;
          emit(tag_, ok ? ShotStage::Accepted : ShotStage::Failed, (uint8_t)e);
          kind_ = Kind::None;
        }
        // One-press: a failed focus write is not reported on its own. The shutter write that
        // follows decides the outcome (it fails too if the link is gone; if it succeeds the
        // camera took the press, possibly without AF lock, which no write can tell us).
        break;
      }
      case CamCmd::ShutterPress: {
        const uint32_t from = millis();  // notifications during the write count
        FujiErr e = fuji.shutterPress();
        if (e != FujiErr::NotReady) maybe_pressed_ = true;
        if (kind_ == Kind::Shot) {
          // FujiErr::Unsure (action write sent, never confirmed) travels as Failed + err 7,
          // which every UI renders as an unknown outcome ("CAM MAY HAVE FIRED"), never FAILED.
          const bool ok = e == FujiErr::None;
          emit(tag_, ok ? ShotStage::Accepted : ShotStage::Failed, (uint8_t)e);
          // Unsure: the write may have landed, so a notification can still show the camera
          // fired (the handheld overturns UNKNOWN with it, never into ACCEPTED).
          if (ok || e == FujiErr::Unsure) {
            watch_ = true;
            watch_tag_ = tag_;
            watch_from_ = from;
          }
          kind_ = Kind::None;
          // L6: no link, so no shutter write went out. A one-press or timed press runs its
          // own queued Release now instead of after the hold: no tally and no radio pause for
          // a press that never happened. The Release is still written (AF may have landed).
          if (e == FujiErr::NotReady && seq_.releaseQueued()) seq_.release(millis());
        }
        break;
      }
      case CamCmd::Release: {
        // S9: only a retry that was reported as failed gets a closing outcome.
        const bool was_retrying = release_retry_ && !connect_retry_;
        const bool on_connect = connect_rel_;
        connect_rel_ = false;
        FujiErr e = fuji.release();
        // Release can never fire the camera, so it is safe to repeat until acknowledged.
        // Only needed if a press may have reached the camera, or on a new link (unknown).
        if (e == FujiErr::None) maybe_pressed_ = false;
        if (e != FujiErr::None && (maybe_pressed_ || on_connect)) {
          release_retry_ = true;
          // S9: nothing known pressed (first write on a new link): retried quietly, with no
          // outcome under an old press's tag and no "may still hold" alert.
          connect_retry_ = !maybe_pressed_;
          retry_at_ = millis() + RELEASE_RETRY_MS;
          retry_until_ = millis() + RELEASE_GIVE_UP_MS;
          // The camera may still hold AF or the shutter (a CH burst): say so, never silently.
          if (maybe_pressed_) emit(rel_tag_, ShotStage::Failed, (uint8_t)e, true);
        } else {
          // This write beat the retry to it: the retry was reported as failed, so close it.
          if (was_retrying && e == FujiErr::None) emit(rel_tag_, ShotStage::Accepted, 0, true);
          release_retry_ = false;
        }
        break;
      }
    }
  }

  void retryRelease(uint32_t now) {
    if (!release_retry_) return;
    if (seq_.buttonDown()) {  // superseded by a new press (it ends with its own Release)
      release_retry_ = false;
      return;
    }
    if ((int32_t)(now - retry_until_) >= 0) {
      release_retry_ = false;
      if (!connect_retry_) emit(rel_tag_, ShotStage::Failed, apperr::GAVE_UP, true);  // S9
      return;
    }
    if ((int32_t)(now - retry_at_) < 0) return;
    retry_at_ = now + RELEASE_RETRY_MS;
    if (fuji.ready() && fuji.release() == FujiErr::None) {
      release_retry_ = maybe_pressed_ = false;
      if (!connect_retry_) emit(rel_tag_, ShotStage::Accepted, 0, true);  // S9
    }
  }

  void watchEvent(uint32_t now) {
    if (!watch_) return;
    const uint32_t t = fuji.lastNotifyMs();
    if (t != 0 && (int32_t)(t - watch_from_) >= 0 &&
        t - watch_from_ <= timing::CAM_EVENT_WINDOW_MS) {
      emit(watch_tag_, ShotStage::CamEvent, 0);
      watch_ = false;
    } else if (now - watch_from_ > timing::CAM_EVENT_WINDOW_MS) {
      watch_ = false;
    }
  }

  void emit(uint32_t tag, ShotStage st, uint8_t err, bool release = false) {
    if (out_n_ == sizeof(out_) / sizeof(out_[0])) next(out_[0]);  // drop oldest
    out_[out_n_++] = CamOutcome{tag, st, err, release};
  }

  static constexpr uint32_t RELEASE_RETRY_MS = 250;
  static constexpr uint32_t RELEASE_GIVE_UP_MS = 60000;
  ShotSequencer seq_;
  bool down_ = false;
  uint32_t down_since_ = 0;
  bool release_retry_ = false;
  bool maybe_pressed_ = false;
  bool connect_rel_ = false;  // the queued Release is the first write on a new link
  bool connect_retry_ = false;  // S9: the running retry is a quiet release-on-connect one
  uint32_t retry_at_ = 0, retry_until_ = 0;
  Kind kind_ = Kind::None;
  uint32_t tag_ = 0;
  uint32_t rel_tag_ = 0;
  bool watch_ = false;
  uint32_t watch_tag_ = 0, watch_from_ = 0;
  CamOutcome out_[6];
  uint8_t out_n_ = 0;
};
