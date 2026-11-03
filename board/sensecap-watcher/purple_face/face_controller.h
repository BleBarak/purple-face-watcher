#pragma once
// Purple's "alive" behaviour (FACE_SPEC.md, robot_face_controller.dart) for
// XiaoZhi on the SenseCAP Watcher: blinks, saccades, breathing, glances,
// anticipation squash, moods that linger, the idle chain, and the XiaoZhi
// states (listening, speaking, connecting...).
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "face_engine.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

namespace purple {

enum class Mode { Booting, Setup, Idle, Connecting, Listening, Speaking, Updating, Error };

class FaceController {
 public:
  explicit FaceController(FaceRenderer &face) : face_(face) {}

  void begin() {
    neutral_ = FaceRenderer::findFace("neutral");
    happy_ = FaceRenderer::findFace("happy");
    surprised_ = FaceRenderer::findFace("surprised");
    listening_ = FaceRenderer::findFace("listening");
    talking_ = FaceRenderer::findFace("talking");
    thinking_ = FaceRenderer::findFace("thinking");
    connecting_ = FaceRenderer::findFace("connecting");
    splash_ = FaceRenderer::findFace("splash");
    bored_ = FaceRenderer::findFace("bored");
    yawn_ = FaceRenderer::findFace("yawn");
    halfAsleep_ = FaceRenderer::findFace("half_asleep");
    sleepy_ = FaceRenderer::findFace("sleepy");
    updating_ = FaceRenderer::findFace("updating");
    error_ = FaceRenderer::findFace("error");
    shy_ = FaceRenderer::findFace("shy");
    look_[0] = FaceRenderer::findFace("look_left");
    look_[1] = FaceRenderer::findFace("look_right");
    look_[2] = FaceRenderer::findFace("look_up");
    look_[3] = FaceRenderer::findFace("look_down");
    mood_ = neutral_;
    face_.snapTo(splash_);
  }

  // XiaoZhi emotion name (from the AI or the app) -> a Purple reaction.
  void emotion(const char *name) {
    int f = mapEmotion(name);
    if (f < 0) return;  // "neutral" etc: the state decides
    wake();
    react(f, 4.0f);
    mood_ = f;  // moods linger, then drift back to neutral
    moodUntil_ = t_ + frand(20, 35);
    kick(3);
  }

  void setMode(Mode m) {
    if (m == mode_) return;
    Mode old = mode_;
    mode_ = m;
    wake();
    if (m == Mode::Listening) {
      kick(-4);  // a little "oh!" when it starts listening
      gtx_ = gty_ = 0;
      nextSaccade_ = t_ + 1.2f;
    }
    if (m == Mode::Speaking) nextSyllable_ = t_;
    if (old == Mode::Speaking) talkTarget_ = 0;
    if (old == Mode::Listening && m == Mode::Idle) react(thinking_, 0.8f);
  }

  // A one-off reaction with a Purple face id (games with friends).
  void reactFace(const char *id, float secs) {
    int f = FaceRenderer::findFace(id);
    if (f < 0) return;
    wake();
    react(f, secs);
    kick(2);
  }

  // Knob turned: glance that way.
  void knob(int dir) {
    wake();
    gtx_ = dir > 0 ? 60 : -60;
    gty_ = 0;
    followUntil_ = t_ + 1.4f;
  }

  void update(float dt) {
    if (dt > 0.05f) dt = 0.05f;
    t_ += dt;
    int want = shown();
    bool idle = mode_ == Mode::Idle && reaction_ < 0 && want == mood_;

    if (idle && mood_ == neutral_ && glance_ < 0 && t_ >= nextGlance_) {
      glance_ = look_[rand() % 4];
      glanceUntil_ = t_ + frand(0.6f, 1.5f);
      nextGlance_ = glanceUntil_ + frand(5, 10);
      want = glance_;
    }

    // Anticipation: squash for 90 ms, then the new face.
    if (want != face_.faceIndex() && want != pending_) {
      pending_ = want;
      pendingAt_ = t_ + 0.09f;
      atx_ = 1.08f, aty_ = 0.82f, atd_ = 6;
    }
    if (pending_ >= 0 && t_ >= pendingAt_) {
      face_.morphTo(pending_);
      pending_ = -1;
      atx_ = aty_ = 1, atd_ = 0;
    }

    // Blinks: every 2.2-6 s, 20 % doubles, one eye sometimes lags.
    if (t_ >= nextBlink_) {
      startBlink();
      nextBlink_ = frand(0, 1) < 0.2f ? t_ + 0.27f : t_ + frand(2.2f, 6);
    }
    float bL = 0, bR = 0;
    int j = 0;
    for (int i = 0; i < nBlinks_; i++) {
      float a = t_ - blinks_[i].start;
      if (a > 0.3f) continue;
      blinks_[j++] = blinks_[i];
      bL = fmaxf(bL, blinkCurve(a - blinks_[i].lagL));
      bR = fmaxf(bR, blinkCurve(a - blinks_[i].lagR));
    }
    nBlinks_ = j;
    bool ok = face_.canBlink() && pending_ < 0;
    face_.blinkL = ok ? bL : 0;
    face_.blinkR = ok ? bR : 0;

    // Saccades: dart, hold. Listening: look at the person (centre).
    if (t_ >= followUntil_ && t_ >= nextSaccade_) {
      float range = mode_ == Mode::Listening ? 0.25f : 1;
      if (frand(0, 1) < 0.35f) gtx_ = gty_ = 0;
      else gtx_ = frand(-55, 55) * range, gty_ = frand(-28, 28) * range;
      nextSaccade_ = t_ + frand(0.9f, 3.2f);
    }

    // Talking: no audio level from the codec, so syllables like the app's demo.
    if (mode_ == Mode::Speaking && t_ >= nextSyllable_) {
      talkTarget_ = frand(0, 1) < 0.18f ? 0 : frand(0.25f, 1);
      nextSyllable_ = t_ + frand(0.09f, 0.24f);
    }

    float h = dt / 2;
    for (int s = 0; s < 2; s++) {
      spring(gx_, gvx_, gtx_, GAZE_K, GAZE_C, h);
      spring(gy_, gvy_, gty_, GAZE_K, GAZE_C, h);
      spring(q_, qv_, 0, SQUASH_K, SQUASH_C, h);
      spring(ax_, avx_, atx_, SHAPE_K, SHAPE_C, h);
      spring(ay_, avy_, aty_, SHAPE_K, SHAPE_C, h);
      spring(ad_, avd_, atd_, SHAPE_K, SHAPE_C, h);
      spring(talk_, talkV_, talkTarget_, TALK_K, TALK_C, h);
    }
    face_.gazeX = gx_ + DRIFT * (sinf(t_ / 1.7f) + 0.6f * sinf(t_ / 0.73f)) / 1.6f;
    face_.gazeY = gy_ + DRIFT * (sinf(t_ / 2.1f + 1) + 0.6f * sinf(t_ / 0.91f)) / 1.6f;
    face_.squash = q_;
    face_.anticX = ax_, face_.anticY = ay_, face_.anticDrop = ad_;
    face_.breath = 1 + BREATH_AMOUNT * sinf(t_ / BREATH_PERIOD * 2 * (float)M_PI);
    face_.talk = talk_ > 0 ? talk_ : 0;
    face_.step(dt);
  }

 private:
  FaceRenderer &face_;
  float t_ = 0;
  Mode mode_ = Mode::Booting;
  int neutral_ = 0, happy_ = 0, surprised_ = 0, listening_ = 0, talking_ = 0, thinking_ = 0, connecting_ = 0,
      splash_ = 0, bored_ = 0, yawn_ = 0, halfAsleep_ = 0, sleepy_ = 0, updating_ = 0, error_ = 0, shy_ = 0;
  int look_[4] = {0, 0, 0, 0};

  int mood_ = 0;
  float moodUntil_ = 0;
  int reaction_ = -1, nextReaction_ = -1;
  float reactionUntil_ = 0, nextReactionFor_ = 0;
  int glance_ = -1;
  float glanceUntil_ = 0, nextGlance_ = 6;
  int pending_ = -1;
  float pendingAt_ = 0;
  float lastActivity_ = 0;

  float gx_ = 0, gy_ = 0, gvx_ = 0, gvy_ = 0, gtx_ = 0, gty_ = 0, nextSaccade_ = 1, followUntil_ = 0;
  float q_ = 0, qv_ = 0;
  float ax_ = 1, ay_ = 1, ad_ = 0, avx_ = 0, avy_ = 0, avd_ = 0, atx_ = 1, aty_ = 1, atd_ = 0;
  float talk_ = 0, talkV_ = 0, talkTarget_ = 0, nextSyllable_ = 0;

  struct Blink { float start, lagL, lagR; };
  Blink blinks_[4];
  int nBlinks_ = 0;
  float nextBlink_ = 2;

  static float frand(float a, float b) { return a + (b - a) * ((rand() & 0xFFFF) / 65535.0f); }

  static float blinkCurve(float t) {
    if (t < 0) return 0;
    if (t < 0.075f) return (t / 0.075f) * (t / 0.075f);
    if (t < 0.105f) return 1;
    if (t < 0.23f) {
      float u = 1 - (t - 0.105f) / 0.125f;
      return u * u;
    }
    return 0;
  }

  void kick(float v) { qv_ += v; }

  void startBlink() {
    if (nBlinks_ >= 4) return;
    float lag = (rand() & 1) ? frand(0, 0.025f) : 0;
    bool left = rand() & 1;
    blinks_[nBlinks_++] = {t_, left ? lag : 0, left ? 0 : lag};
  }

  void react(int f, float secs) {
    reaction_ = f;
    reactionUntil_ = t_ + secs;
    nextReaction_ = -1;
  }

  bool asleep() const { return mode_ == Mode::Idle && reaction_ < 0 && t_ - lastActivity_ >= 180; }

  void wake() {
    bool was = asleep();
    lastActivity_ = t_;
    if (was) {
      react(surprised_, 1.2f);
      nextReaction_ = happy_;
      nextReactionFor_ = 3;
      kick(-6);
    }
  }

  int shown() {
    switch (mode_) {
      case Mode::Booting: return splash_;
      case Mode::Setup:
      case Mode::Connecting: return connecting_;
      case Mode::Updating: return updating_;
      case Mode::Error: return error_;
      default: break;
    }
    if (reaction_ >= 0) {
      if (t_ < reactionUntil_) return reaction_;
      if (nextReaction_ >= 0) {
        int n = nextReaction_;
        react(n, nextReactionFor_);
        return n;
      }
      reaction_ = -1;
    }
    if (mood_ != neutral_ && t_ > moodUntil_) mood_ = neutral_;
    if (mode_ == Mode::Listening) return listening_;
    if (mode_ == Mode::Speaking) return mood_ != neutral_ ? mood_ : talking_;
    float idle = t_ - lastActivity_;
    if (idle >= 240) return sleepy_;
    if (idle >= 180) return halfAsleep_;
    if (idle >= 150 && idle < 152.5f) return yawn_;
    if (idle >= 120) return bored_;
    if (glance_ >= 0) {
      if (t_ < glanceUntil_) return glance_;
      glance_ = -1;
    }
    return mood_;
  }

  // XiaoZhi's emotion names -> Purple faces. -1: no reaction.
  static int mapEmotion(const char *e) {
    static const char *const MAP[][2] = {
        {"happy", "happy"},        {"laughing", "laugh"},     {"funny", "playful"},
        {"sad", "sad"},            {"angry", "angry"},        {"crying", "crying"},
        {"loving", "love"},        {"embarrassed", "embarrassed"}, {"surprised", "surprised"},
        {"shocked", "shocked"},    {"thinking", "thinking"},  {"winking", "wink"},
        {"cool", "cool"},          {"relaxed", "content"},    {"delicious", "cheeky"},
        {"kissy", "kiss"},         {"confident", "proud"},    {"sleepy", "sleepy"},
        {"silly", "silly"},        {"confused", "confused"},
    };
    if (!e) return -1;
    for (auto &m : MAP)
      if (!strcmp(e, m[0])) return FaceRenderer::findFace(m[1]);
    int direct = FaceRenderer::findFace(e);  // a Purple face id works too
    if (direct >= 0 && strcmp(e, "neutral")) return direct;
    return -1;
  }
};

}  // namespace purple

#pragma GCC pop_options
