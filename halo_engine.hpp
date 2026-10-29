#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace stereo_dsp {

// The DSP-relevant fields of a Halo "Place" (UI text such as name/line is dropped).
struct HaloPlace {
    float width;       // side gain at full immersion (1 = the recording's own stereo)
    float motion;      // 0..1, how far the edges drift
    float motionRate;  // drift speed in Hz
    float space;       // early-reflection send
    float spaceSize;   // scales reflection delays
    float tail;        // feedback on the longest tap
    float air;         // high-shelf dB on the side only
    float focus;       // presence dB on the centre
    float bloom;       // how much width follows loudness
    float punch;       // how much transients kick the room open
    float warmth;      // low-shelf dB on the centre
};

// ESTIMATED Bloom preset. The real numbers live in the web app's places file, which
// only its type definition was shared. Replace these eleven values with the real
// Bloom entry and nothing else needs to change.
inline constexpr HaloPlace kHaloBloomPlace = {
    /*width*/ 1.50f, /*motion*/ 0.12f, /*motionRate*/ 0.04f, /*space*/ 0.50f,
    /*spaceSize*/ 1.15f, /*tail*/ 0.30f, /*air*/ 2.5f, /*focus*/ 1.5f,
    /*bloom*/ 1.00f, /*punch*/ 0.60f, /*warmth*/ 0.6f};

// Halo "Field" listening engine, ported from the web app as a self-contained C++ stage
// (interleaved stereo float). Completely independent of ConsumerEnhancer: no shared
// state, no shared filters, no calls between them.
//
// There is exactly one control: the Bloom button. ON = immersion 100 + Bloom preset
// (headphone mode, neutral shape, no lean). OFF = bit-exact bypass.
//
// Signal path (per sample):
//   Mid/Side split
//   Mid  -> presence peak (2.8 kHz) -> warmth low shelf (220 Hz)
//   Side -> high-pass 120 Hz ("anchor": bass stays centred)
//        -> loudness-following width ("bloom")
//        -> air high shelf (7.8 kHz) -> 2.8 kHz dip (mirrors the centre lift)
//        -> direct + two opposing micro-delays with all-passes ("orbit", side only)
//   Room: width + mid -> high-pass 180 Hz -> pre-delay -> 3 filtered taps (one with
//        feedback tail), panned across the bus
//   Bass crossfeed: 200 Hz Linkwitz-Riley split, low band crossfed to the other ear
//   Compressor (-2.5 dB, 8:1, soft knee) -> slow loudness match -> equal-fade mix
class HaloEngine {
public:
    struct Config {
        float sampleRateHz = 48000.0f;
    };

    bool prepare(const Config& requested) noexcept;
    void reset() noexcept;
    void process(float* samples, std::size_t frames) noexcept;

    // THE button. Safe to call from any thread.
    void setBloomButton(bool on) noexcept;
    bool toggleBloomButton() noexcept;  // returns the new state
    bool isBloomOn() const noexcept;

private:
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
        void clear() noexcept { z1 = z2 = 0.0; }
        float process(float x) noexcept;
        void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept;
        // Web Audio conventions: lowpass/highpass Q is in dB, shelves use slope S = 1.
        void lowpass(double fs, double f, double qDb) noexcept;
        void highpass(double fs, double f, double qDb) noexcept;
        void peaking(double fs, double f, double q, double gainDb) noexcept;
        void lowshelf(double fs, double f, double gainDb) noexcept;
        void highshelf(double fs, double f, double gainDb) noexcept;
        void allpass(double fs, double f, double q) noexcept;
    };

    struct Delay {
        std::vector<float> buf;
        std::size_t pos = 0U;
        void allocate(std::size_t size);
        void clear() noexcept;
        float process(float x, float delaySamples) noexcept;
    };

    struct Compressor {
        float thrLin = 0.0f, kneeThr = 0.0f, kneeThrDb = 0.0f, yKneeDb = 0.0f;
        float slope = 0.125f, k = 1.0f, makeup = 1.0f;
        float attackCoef = 0.0f, releaseCoef = 0.0f, gain = 1.0f;
        std::vector<float> lookL, lookR;
        std::size_t pos = 0U;
        void configure(float fs, float thresholdDb, float kneeDb, float ratio,
                       float attackSec, float releaseSec, float lookaheadSec);
        void clear() noexcept;
        float saturate(float x) const noexcept;
        void process(float inL, float inR, float& outL, float& outR) noexcept;
    };

    void clearStates() noexcept;
    void tick() noexcept;

    std::atomic<bool> button_{false};

    bool prepared_ = false;
    bool active_ = false;
    float fs_ = 48000.0f;

    // Resolved Bloom @ immersion 100
    float width_ = 1.0f, motion_ = 0.0f, motionRate_ = 0.05f, space_ = 0.0f, bloom_ = 0.0f;
    float punch_ = 0.0f, tailGain_ = 0.2f;

    // Filters
    Biquad focusEq_, warmthEq_, airEq_, dipEq_, lowTap_, sideHp_, sendHp_;
    Biquad allpassA_, allpassB_, tapLp_[3];
    Biquad xoHp_[2][2], xoLp_[2][2];
    Delay delayA_, delayB_, predelay_, tapDelay_[3], crossDelay_[2];
    Compressor comp_;

    float tapGainL_[3] = {}, tapGainR_[3] = {};
    float tapSamples_[3] = {}, preSamples_ = 0.0f, crossSamples_ = 0.0f;
    float fbState_ = 0.0f;
    float ownGain_ = 1.0f, oppGain_ = 0.0f;

    // Smoothed control values (follow targets set by tick())
    float widthSm_ = 1.0f, widthTarget_ = 1.0f;
    float sendSm_ = 0.0f, sendTarget_ = 0.0f, midSendSm_ = 0.0f, midSendTarget_ = 0.0f;
    float delayASm_ = 0.0f, delayATarget_ = 0.0f, delayBSm_ = 0.0f, delayBTarget_ = 0.0f;
    float directSm_ = 1.0f, motionGainSm_ = 0.0f;
    float makeupSm_ = 1.0f, matchTarget_ = 1.0f, match_ = 1.0f;
    float dryGain_ = 1.0f, wetGain_ = 0.0f;
    float cWidth_ = 0.0f, cSend_ = 0.0f, cDelay_ = 0.0f, cMakeup_ = 0.0f, cFade_ = 0.0f;

    // Slow control analysis (runs every ~32 ms on a ~21 ms window, like the web analysers)
    std::vector<float> sqMid_, sqLow_, sqDry_, sqWet_;
    double sumMid_ = 0.0, sumLow_ = 0.0, sumDry_ = 0.0, sumWet_ = 0.0;
    std::size_t ringPos_ = 0U, tickSamples_ = 1536U, tickCounter_ = 0U;
    float env_ = 0.32f, prevEnergy_ = 0.0f, punchEnv_ = 0.0f, phase_ = 0.0f;
};

} // namespace stereo_dsp
