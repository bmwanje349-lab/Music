#include "halo_engine.hpp"

#include <algorithm>
#include <cmath>

namespace stereo_dsp {
static_assert(std::atomic<bool>::is_always_lock_free,
              "HaloEngine requires a lock-free atomic bool for the button");

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kSafe = 1.0e6f;
constexpr double kTiny = 1.0e-30;

// Room taps (delay base in seconds, pan -1..1, gain, low-pass cutoff, has feedback)
constexpr float kTapBase[3] = {0.017f, 0.029f, 0.048f};
constexpr float kTapPan[3] = {-0.72f, 0.70f, -0.12f};
constexpr float kTapGain[3] = {0.40f, 0.28f, 0.22f};
constexpr float kTapCutoff[3] = {5400.0f, 3900.0f, 2500.0f};

constexpr float kTickSeconds = 0.032f;   // control rate of the web engine
constexpr float kWindowSeconds = 1024.0f / 48000.0f;

inline float clampf(float v, float lo, float hi) noexcept {
    return std::max(lo, std::min(hi, v));
}
inline float expCoef(float seconds, float fs) noexcept {
    return std::exp(-1.0f / (seconds * fs));
}
inline float dbToLin(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
inline float linToDb(float x) noexcept { return 20.0f * std::log10(std::max(x, 1.0e-12f)); }

// Keeps only the wet path from ever exceeding full scale; the dry path is never touched.
inline float softCeil(float x) noexcept {
    const float a = std::fabs(x);
    constexpr float knee = 0.95f;
    if (a <= knee) return x;
    const float y = knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}
} // namespace

// ---------------------------------------------------------------- biquad

void HaloEngine::Biquad::set(double nb0, double nb1, double nb2,
                             double na0, double na1, double na2) noexcept {
    const double inv = 1.0 / na0;
    b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv;
    a1 = na1 * inv; a2 = na2 * inv;
}

float HaloEngine::Biquad::process(float x) noexcept {
    const double xd = x;
    const double y = b0 * xd + z1;
    z1 = b1 * xd - a1 * y + z2;
    z2 = b2 * xd - a2 * y;
    if (std::fabs(z1) < kTiny) z1 = 0.0;
    if (std::fabs(z2) < kTiny) z2 = 0.0;
    if (!std::isfinite(y) || !std::isfinite(z1) || !std::isfinite(z2)) {
        clear();
        return 0.0f;
    }
    return static_cast<float>(y);
}

void HaloEngine::Biquad::lowpass(double fs, double f, double qDb) noexcept {
    const double w0 = 2.0 * kPi * std::min(f, fs * 0.45) / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * std::pow(10.0, qDb / 20.0));
    set((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

void HaloEngine::Biquad::highpass(double fs, double f, double qDb) noexcept {
    const double w0 = 2.0 * kPi * std::min(f, fs * 0.45) / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * std::pow(10.0, qDb / 20.0));
    set((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

void HaloEngine::Biquad::peaking(double fs, double f, double q, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * std::min(f, fs * 0.45) / fs;
    const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    set(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
}

void HaloEngine::Biquad::lowshelf(double fs, double f, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * std::min(f, fs * 0.45) / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s * 0.5 * std::sqrt(2.0);  // slope S = 1
    const double t = 2.0 * std::sqrt(A) * alpha;
    set(A * ((A + 1) - (A - 1) * c + t), 2.0 * A * ((A - 1) - (A + 1) * c),
        A * ((A + 1) - (A - 1) * c - t), (A + 1) + (A - 1) * c + t,
        -2.0 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
}

void HaloEngine::Biquad::highshelf(double fs, double f, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * std::min(f, fs * 0.45) / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s * 0.5 * std::sqrt(2.0);  // slope S = 1
    const double t = 2.0 * std::sqrt(A) * alpha;
    set(A * ((A + 1) + (A - 1) * c + t), -2.0 * A * ((A - 1) + (A + 1) * c),
        A * ((A + 1) + (A - 1) * c - t), (A + 1) - (A - 1) * c + t,
        2.0 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
}

void HaloEngine::Biquad::allpass(double fs, double f, double q) noexcept {
    const double w0 = 2.0 * kPi * std::min(f, fs * 0.45) / fs;
    const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    set(1.0 - alpha, -2.0 * c, 1.0 + alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

// ---------------------------------------------------------------- delay

void HaloEngine::Delay::allocate(std::size_t size) {
    buf.assign(std::max<std::size_t>(size, 4U), 0.0f);
    pos = 0U;
}

void HaloEngine::Delay::clear() noexcept {
    std::fill(buf.begin(), buf.end(), 0.0f);
    pos = 0U;
}

float HaloEngine::Delay::process(float x, float delaySamples) noexcept {
    const std::size_t size = buf.size();
    buf[pos] = x;
    const float d = clampf(delaySamples, 0.0f, static_cast<float>(size - 2U));
    const std::size_t di = static_cast<std::size_t>(d);
    const float fr = d - static_cast<float>(di);
    const std::size_t a = (pos + size - di) % size;
    const std::size_t b = (a + size - 1U) % size;
    const float y = buf[a] * (1.0f - fr) + buf[b] * fr;
    pos = (pos + 1U) % size;
    return y;
}

// ---------------------------------------------------------------- compressor
// Same static curve construction as a browser dynamics compressor (soft knee solved so the
// slope past the knee equals 1/ratio, makeup = (1/curve(1.0))^0.6) with a 6 ms look-ahead.
// Attack/release use plain one-pole smoothing instead of the browser's adaptive release.

void HaloEngine::Compressor::configure(float fs, float thresholdDb, float kneeDb, float ratio,
                                       float attackSec, float releaseSec, float lookaheadSec) {
    thrLin = dbToLin(thresholdDb);
    slope = 1.0f / ratio;
    const auto kneeAt = [&](float x, float kk) {
        return x < thrLin ? x : thrLin + (1.0f - std::exp(-kk * (x - thrLin))) / kk;
    };
    const auto slopeAt = [&](float x, float kk) {
        if (x < thrLin) return 1.0f;
        const float x2 = x * 1.001f;
        const float xDb = linToDb(x), x2Db = linToDb(x2);
        return (linToDb(kneeAt(x2, kk)) - linToDb(kneeAt(x, kk))) / (x2Db - xDb);
    };
    const float xKnee = dbToLin(thresholdDb + kneeDb);
    float minK = 0.1f, maxK = 10000.0f, kk = 5.0f;
    for (int i = 0; i < 15; ++i) {
        if (slopeAt(xKnee, kk) < slope) maxK = kk; else minK = kk;
        kk = std::sqrt(minK * maxK);
    }
    k = kk;
    kneeThrDb = thresholdDb + kneeDb;
    kneeThr = dbToLin(kneeThrDb);
    yKneeDb = linToDb(kneeAt(kneeThr, k));
    makeup = std::pow(1.0f / saturate(1.0f), 0.6f);
    attackCoef = expCoef(attackSec, fs);
    releaseCoef = expCoef(releaseSec, fs);
    const std::size_t look = std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::lround(lookaheadSec * fs)));
    lookL.assign(look + 1U, 0.0f);
    lookR.assign(look + 1U, 0.0f);
    pos = 0U;
    gain = 1.0f;
}

void HaloEngine::Compressor::clear() noexcept {
    std::fill(lookL.begin(), lookL.end(), 0.0f);
    std::fill(lookR.begin(), lookR.end(), 0.0f);
    pos = 0U;
    gain = 1.0f;
}

float HaloEngine::Compressor::saturate(float x) const noexcept {
    if (x < thrLin) return x;
    if (x < kneeThr) return thrLin + (1.0f - std::exp(-k * (x - thrLin))) / k;
    return dbToLin(yKneeDb + slope * (linToDb(x) - kneeThrDb));
}

void HaloEngine::Compressor::process(float inL, float inR, float& outL, float& outR) noexcept {
    const std::size_t size = lookL.size();
    lookL[pos] = inL;
    lookR[pos] = inR;
    const float det = std::max(std::fabs(inL), std::fabs(inR));
    float desired = 1.0f;
    if (det > thrLin) desired = std::min(1.0f, saturate(det) / det);
    const float coef = desired < gain ? attackCoef : releaseCoef;
    gain = desired + coef * (gain - desired);
    const std::size_t read = (pos + 1U) % size;   // oldest sample = look-ahead delayed
    outL = lookL[read] * gain * makeup;
    outR = lookR[read] * gain * makeup;
    pos = read;
}

// ---------------------------------------------------------------- setup

bool HaloEngine::prepare(const Config& requested) noexcept {
    prepared_ = false;
    if (!std::isfinite(requested.sampleRateHz) || requested.sampleRateHz < 8000.0f ||
        requested.sampleRateHz > 384000.0f) {
        return false;
    }
    fs_ = requested.sampleRateHz;
    const double fs = fs_;

    // Bloom at immersion 100, headphone mode, neutral shape (t = 1).
    const HaloPlace& p = kHaloBloomPlace;
    width_ = clampf(1.0f + std::max(0.0f, p.width - 1.0f), 1.0f, 2.05f);
    motion_ = clampf(std::max(0.0f, p.motion), 0.0f, 1.0f);
    motionRate_ = p.motionRate;
    space_ = clampf(std::max(0.0f, p.space), 0.0f, 1.15f);
    const float air = clampf(std::max(0.0f, p.air), 0.0f, 8.0f);
    const float focus = clampf(std::max(0.0f, p.focus), 0.0f, 6.0f);
    const float warmth = clampf(p.warmth, -1.0f, 4.0f);
    bloom_ = clampf(p.bloom, 0.0f, 1.0f);
    punch_ = clampf(p.punch, 0.0f, 1.0f);
    tailGain_ = clampf(p.tail, 0.0f, 0.58f);
    const float size = clampf(p.spaceSize, 0.6f, 1.9f);
    constexpr float cross = 0.66f;   // headphones
    ownGain_ = 1.0f - 0.22f * cross;
    oppGain_ = 0.42f * cross;

    constexpr double q707 = 0.70710678118654752;
    focusEq_.peaking(fs, 2800.0, 0.9, focus);
    warmthEq_.lowshelf(fs, 220.0, warmth);
    airEq_.highshelf(fs, 7800.0, air);
    dipEq_.peaking(fs, 2800.0, 0.75, -focus * 0.45);
    lowTap_.lowpass(fs, 140.0, q707);
    sideHp_.highpass(fs, 120.0, q707);
    sendHp_.highpass(fs, 180.0, q707);
    allpassA_.allpass(fs, 740.0, 0.5);
    allpassB_.allpass(fs, 1280.0, 0.5);
    for (int i = 0; i < 3; ++i) {
        tapLp_[i].lowpass(fs, kTapCutoff[i], 0.7);
        const float angle = (kTapPan[i] + 1.0f) * static_cast<float>(kPi) / 4.0f;
        tapGainL_[i] = std::cos(angle) * kTapGain[i];
        tapGainR_[i] = std::sin(angle) * kTapGain[i];
        tapSamples_[i] = kTapBase[i] * size * fs_;
    }
    preSamples_ = 0.012f * size * fs_;
    crossSamples_ = 0.00032f * fs_;
    for (int c = 0; c < 2; ++c) {
        for (int s = 0; s < 2; ++s) {
            xoHp_[c][s].highpass(fs, 200.0, q707);
            xoLp_[c][s].lowpass(fs, 200.0, q707);
        }
    }

    try {
        delayA_.allocate(static_cast<std::size_t>(0.02 * fs) + 4U);
        delayB_.allocate(static_cast<std::size_t>(0.02 * fs) + 4U);
        predelay_.allocate(static_cast<std::size_t>(0.03 * fs) + 4U);
        for (auto& d : tapDelay_) d.allocate(static_cast<std::size_t>(0.12 * fs) + 4U);
        for (auto& d : crossDelay_) d.allocate(static_cast<std::size_t>(0.002 * fs) + 4U);
        comp_.configure(fs_, -2.5f, 6.0f, 8.0f, 0.003f, 0.12f, 0.006f);
        const std::size_t window = std::max<std::size_t>(64U,
            static_cast<std::size_t>(std::lround(kWindowSeconds * fs_)));
        sqMid_.assign(window, 0.0f);
        sqLow_.assign(window, 0.0f);
        sqDry_.assign(window, 0.0f);
        sqWet_.assign(window, 0.0f);
    } catch (...) {
        return false;
    }
    tickSamples_ = std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::lround(kTickSeconds * fs_)));

    cWidth_ = expCoef(0.035f, fs_);
    cSend_ = expCoef(0.05f, fs_);
    cDelay_ = expCoef(0.06f, fs_);
    cMakeup_ = expCoef(0.25f, fs_);
    cFade_ = expCoef(0.03f, fs_);

    clearStates();
    active_ = false;
    prepared_ = true;
    return true;
}

void HaloEngine::clearStates() noexcept {
    for (Biquad* b : {&focusEq_, &warmthEq_, &airEq_, &dipEq_, &lowTap_, &sideHp_, &sendHp_,
                      &allpassA_, &allpassB_}) {
        b->clear();
    }
    for (auto& b : tapLp_) b.clear();
    for (auto& c : xoHp_) for (auto& b : c) b.clear();
    for (auto& c : xoLp_) for (auto& b : c) b.clear();
    delayA_.clear(); delayB_.clear(); predelay_.clear();
    for (auto& d : tapDelay_) d.clear();
    for (auto& d : crossDelay_) d.clear();
    comp_.clear();
    fbState_ = 0.0f;

    std::fill(sqMid_.begin(), sqMid_.end(), 0.0f);
    std::fill(sqLow_.begin(), sqLow_.end(), 0.0f);
    std::fill(sqDry_.begin(), sqDry_.end(), 0.0f);
    std::fill(sqWet_.begin(), sqWet_.end(), 0.0f);
    sumMid_ = sumLow_ = sumDry_ = sumWet_ = 0.0;
    ringPos_ = 0U;
    tickCounter_ = 0U;

    const float depth0 = 0.0035f * motion_;
    widthSm_ = widthTarget_ = width_;
    sendSm_ = sendTarget_ = space_ * 0.62f;
    midSendSm_ = midSendTarget_ = space_ * 0.14f;
    delayASm_ = delayATarget_ = depth0 * fs_;
    delayBSm_ = delayBTarget_ = depth0 * fs_;
    const float m = clampf(motion_, 0.0f, 1.0f);
    directSm_ = std::cos(m * static_cast<float>(kPi) / 2.0f);
    motionGainSm_ = std::sin(m * static_cast<float>(kPi) / 2.0f);
    makeupSm_ = 1.0f; matchTarget_ = 1.0f; match_ = 1.0f;
    dryGain_ = 1.0f; wetGain_ = 0.0f;
    env_ = 0.32f; prevEnergy_ = 0.0f; punchEnv_ = 0.0f; phase_ = 0.0f;
}

void HaloEngine::reset() noexcept {
    clearStates();
    active_ = false;
}

void HaloEngine::setBloomButton(bool on) noexcept {
    button_.store(on, std::memory_order_relaxed);
}
bool HaloEngine::toggleBloomButton() noexcept {
    const bool next = !button_.load(std::memory_order_relaxed);
    button_.store(next, std::memory_order_relaxed);
    return next;
}
bool HaloEngine::isBloomOn() const noexcept {
    return button_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------- control tick

void HaloEngine::tick() noexcept {
    const auto rms = [&](double sum, std::size_t n) {
        return std::sqrt(static_cast<float>(std::max(0.0, sum) / static_cast<double>(n)));
    };
    const std::size_t n = sqMid_.size();
    const float midRms = rms(sumMid_, n), lowRms = rms(sumLow_, n);
    const float dryRms = rms(sumDry_, n), wetRms = rms(sumWet_, n);

    const float mid = clampf(midRms * 3.4f, 0.0f, 1.0f);
    const float low = clampf(lowRms * 3.6f, 0.0f, 1.0f);
    const float energy = std::max(mid, low);
    env_ += (energy - env_) * (energy > env_ ? 0.05f : 0.02f);
    const float diff = energy - prevEnergy_;
    prevEnergy_ = energy;
    if (diff > 0.035f) punchEnv_ = clampf(punchEnv_ + diff * 4.2f, 0.0f, 1.0f);
    else punchEnv_ *= 0.9f;

    // Bloom: width follows loudness, transients kick the room open.
    const float dyn = 1.0f + bloom_ * (env_ - 0.32f) * 0.85f + punchEnv_ * punch_ * 0.3f;
    widthTarget_ = clampf(width_ * dyn, 0.5f, 2.2f);
    const float space = clampf(space_ * (1.0f + punchEnv_ * punch_ * 0.4f), 0.0f, 1.25f);
    sendTarget_ = space * 0.62f;
    midSendTarget_ = space * 0.14f;

    // Orbit: slow opposing micro-delays on the side signal only.
    phase_ = std::fmod(phase_ + kTickSeconds * motionRate_, 1.0f);
    const float depth = 0.0075f * motion_;
    const float base = 0.0035f * motion_;
    const float wobble = std::sin(phase_ * 2.0f * static_cast<float>(kPi));
    delayATarget_ = (base + depth * (0.5f + 0.5f * wobble)) * fs_;
    delayBTarget_ = (base + depth * (0.5f - 0.5f * wobble)) * fs_;

    // Slow loudness match so Halo never "wins" an A/B by being louder.
    if (dryRms > 0.006f && wetRms > 0.006f) {
        const float target = clampf(dryRms / wetRms, 0.72f, 1.2f);
        match_ += (target - match_) * 0.04f;
    }
    matchTarget_ = match_;
}

// ---------------------------------------------------------------- process

void HaloEngine::process(float* samples, std::size_t frames) noexcept {
    if (!prepared_ || samples == nullptr || frames == 0U) return;

    const bool want = button_.load(std::memory_order_relaxed);
    if (!want && !active_) return;   // exact bypass
    if (want && !active_) {
        clearStates();
        active_ = true;
    }
    const float dryTarget = want ? 0.0f : 1.0f;
    const float wetTarget = want ? 1.0f : 0.0f;
    const std::size_t window = sqMid_.size();

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t idx = frame * 2U;
        float inL = samples[idx], inR = samples[idx + 1U];
        if (!std::isfinite(inL)) inL = 0.0f;
        if (!std::isfinite(inR)) inR = 0.0f;
        inL = clampf(inL, -kSafe, kSafe);
        inR = clampf(inR, -kSafe, kSafe);

        dryGain_ = dryTarget + cFade_ * (dryGain_ - dryTarget);
        wetGain_ = wetTarget + cFade_ * (wetGain_ - wetTarget);
        widthSm_ = widthTarget_ + cWidth_ * (widthSm_ - widthTarget_);
        sendSm_ = sendTarget_ + cSend_ * (sendSm_ - sendTarget_);
        midSendSm_ = midSendTarget_ + cSend_ * (midSendSm_ - midSendTarget_);
        delayASm_ = delayATarget_ + cDelay_ * (delayASm_ - delayATarget_);
        delayBSm_ = delayBTarget_ + cDelay_ * (delayBSm_ - delayBTarget_);
        makeupSm_ = matchTarget_ + cMakeup_ * (makeupSm_ - matchTarget_);

        // Mid / Side
        const float mid = 0.5f * (inL + inR);
        const float side = 0.5f * (inL - inR);
        const float lowSample = lowTap_.process(mid);

        // Mid: presence + warmth
        const float midOut = warmthEq_.process(focusEq_.process(mid));

        // Side: anchor (bass stays centred), bloom width, air, presence dip
        const float sideW = sideHp_.process(side) * widthSm_;
        const float dip = dipEq_.process(airEq_.process(sideW));

        // Orbit: direct + two opposing micro-delays (side only; centre never modulated)
        const float directPart = dip * directSm_;
        const float orbitA = allpassA_.process(delayA_.process(dip, delayASm_)) * motionGainSm_;
        const float orbitB = allpassB_.process(delayB_.process(dip, delayBSm_)) * motionGainSm_;
        float busL = midOut + directPart + orbitA;
        float busR = midOut - directPart - orbitB;

        // Room: early reflections from the side-heavy send, with a feedback tail on tap 3
        const float send = sendHp_.process(sideW * sendSm_ + midOut * midSendSm_);
        const float pre = predelay_.process(send, preSamples_);
        float nextFb = fbState_;
        for (int i = 0; i < 3; ++i) {
            const float tapIn = (i == 2) ? pre + fbState_ : pre;
            const float tap = tapLp_[i].process(tapDelay_[i].process(tapIn, tapSamples_[i]));
            busL += tap * tapGainL_[i];
            busR += tap * tapGainR_[i];
            if (i == 2) nextFb = tap * tailGain_;
        }
        fbState_ = nextFb;

        // Bass crossfeed: 200 Hz Linkwitz-Riley split; lows are crossfed, highs stay wide
        const float lowL = xoLp_[0][1].process(xoLp_[0][0].process(busL));
        const float lowR = xoLp_[1][1].process(xoLp_[1][0].process(busR));
        const float highL = xoHp_[0][1].process(xoHp_[0][0].process(busL));
        const float highR = xoHp_[1][1].process(xoHp_[1][0].process(busR));
        const float crossToR = crossDelay_[0].process(lowL, crossSamples_);
        const float crossToL = crossDelay_[1].process(lowR, crossSamples_);
        const float outL = highL + ownGain_ * lowL + oppGain_ * crossToL;
        const float outR = highR + ownGain_ * lowR + oppGain_ * crossToR;

        float wetL = 0.0f, wetR = 0.0f;
        comp_.process(outL, outR, wetL, wetR);

        // Control analysis windows (mono sums, like the web analysers)
        const float sMid = midOut * midOut;
        const float sLow = lowSample * lowSample;
        const float sDry = mid * mid;
        const float wMono = 0.5f * (wetL + wetR);
        const float sWet = wMono * wMono;
        sumMid_ += static_cast<double>(sMid) - sqMid_[ringPos_]; sqMid_[ringPos_] = sMid;
        sumLow_ += static_cast<double>(sLow) - sqLow_[ringPos_]; sqLow_[ringPos_] = sLow;
        sumDry_ += static_cast<double>(sDry) - sqDry_[ringPos_]; sqDry_[ringPos_] = sDry;
        sumWet_ += static_cast<double>(sWet) - sqWet_[ringPos_]; sqWet_[ringPos_] = sWet;
        ringPos_ = (ringPos_ + 1U) % window;
        if (++tickCounter_ >= tickSamples_) {
            tickCounter_ = 0U;
            tick();
        }

        float yL = dryGain_ * inL + wetGain_ * softCeil(wetL * makeupSm_);
        float yR = dryGain_ * inR + wetGain_ * softCeil(wetR * makeupSm_);
        if (!std::isfinite(yL)) yL = 0.0f;
        if (!std::isfinite(yR)) yR = 0.0f;
        samples[idx] = yL;
        samples[idx + 1U] = yR;
    }

    if (!want && wetGain_ < 1.0e-4f && dryGain_ > 0.9999f) {
        active_ = false;   // fully faded out; next call is a bit-exact bypass
    }
}

} // namespace stereo_dsp
