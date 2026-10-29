#include "consumer_enhancer.hpp"

#include <algorithm>
#include <cmath>

namespace stereo_dsp {
static_assert(std::atomic<float>::is_always_lock_free,
              "ConsumerEnhancer requires lock-free float atomics");

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kSafeMagnitude = 1.0e6f;
constexpr double kTiny = 1.0e-30;

// Stage strengths at 100% macro / 100% trim.
constexpr float kBassHarmonicGain = 0.9f;
constexpr float kClarityGain = 1.2f;
constexpr float kWarmthMix = 0.45f;
constexpr float kPunchMaxBoost = 0.9f;     // +5.6 dB on a full-strength attack
constexpr float kGlueMix = 0.35f;
constexpr float kGlueMakeup = 1.778f;      // +5 dB on the parallel path
constexpr float kGlueThresholdDb = -26.0f;
constexpr float kGlueRatio = 4.0f;
constexpr float kGlueKneeDb = 8.0f;
constexpr float kSpaceAmbience = 0.75f;
constexpr float kSpaceWiden = 0.5f;
constexpr float kSpaceTapMs[4] = {11.3f, 17.9f, 23.7f, 31.1f};
constexpr float kSpaceTapGain[4] = {0.50f, -0.40f, 0.32f, -0.25f};
constexpr float kAutoFloorDb = -20.0f;   // lift tracks quieter than this (RMS dBFS)
constexpr float kAutoCeilDb = -9.0f;     // trim only tracks hotter than this
constexpr float kAutoMaxBoostDb = 9.0f;
constexpr float kAutoMaxCutDb = 3.0f;
constexpr float kHarshRatioStart = 0.35f;
constexpr float kHarshMaxCut = 0.5f;     // up to about -6 dB of the 4.5-9 kHz band
constexpr float kSoftKnee = 0.7f;
// Background detail engine (band edges 200 / 1200 / 4500 Hz)
constexpr float kDetailMaxBoostDb[4] = {1.5f, 4.0f, 5.0f, 3.0f};  // treble capped: codec/hiss risk
constexpr float kMaskCutDb[4] = {2.0f, 2.0f, 1.5f, 1.0f};
constexpr float kSideBoostScale = 1.3f;   // backing/ambience lives to the sides
constexpr float kBassMaskMaxDb = 2.5f;
constexpr float kMicroBoost = 0.26f;      // about +2 dB on a quiet fine attack
constexpr float kMatchMaxCutDb = 6.0f;
constexpr float kMatchMaxLiftDb = 3.0f;

inline float clamp01(float v) noexcept { return std::max(0.0f, std::min(1.0f, v)); }
// Smooth 0->1 ramp as the slider travels from a to b.
inline float softClip(float x) noexcept {
    const float a = std::fabs(x);
    if (a <= kSoftKnee) return x;
    const float y = kSoftKnee + (1.0f - kSoftKnee) * std::tanh((a - kSoftKnee) / (1.0f - kSoftKnee));
    return x < 0.0f ? -y : y;
}
inline float stage(float x, float a, float b) noexcept {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

float ConsumerEnhancer::clampFinite(float v, float lo, float hi, float fb) noexcept {
    if (!std::isfinite(v)) return fb;
    return std::max(lo, std::min(v, hi));
}

float ConsumerEnhancer::expCoef(float ms, float fs) noexcept {
    return std::exp(-1.0f / (ms * 0.001f * fs));
}

void ConsumerEnhancer::OnePole::set(float fc, float fs) noexcept {
    fb = std::exp(-2.0f * static_cast<float>(kPi) * std::min(fc, fs * 0.45f) / fs);
    state = 0.0f;
}
float ConsumerEnhancer::OnePole::process(float x) noexcept {
    state = (1.0f - fb) * x + fb * state;
    if (!std::isfinite(state)) state = 0.0f;
    if (std::fabs(state) < 1.0e-20f) state = 0.0f;
    return state;
}

// ---------------------------------------------------------------- biquad

void ConsumerEnhancer::Biquad::set(double nb0, double nb1, double nb2,
                                   double na0, double na1, double na2) noexcept {
    const double inv = 1.0 / na0;
    b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv;
    a1 = na1 * inv; a2 = na2 * inv;
}

float ConsumerEnhancer::Biquad::process(float x) noexcept {
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

void ConsumerEnhancer::Biquad::setLowShelf(double fs, double f0, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double S = 0.8;
    const double alpha = s * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
    const double t = 2.0 * std::sqrt(A) * alpha;
    set(A * ((A + 1) - (A - 1) * c + t),
        2.0 * A * ((A - 1) - (A + 1) * c),
        A * ((A + 1) - (A - 1) * c - t),
        (A + 1) + (A - 1) * c + t,
        -2.0 * ((A - 1) + (A + 1) * c),
        (A + 1) + (A - 1) * c - t);
}

void ConsumerEnhancer::Biquad::setHighShelf(double fs, double f0, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double S = 0.8;
    const double alpha = s * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
    const double t = 2.0 * std::sqrt(A) * alpha;
    set(A * ((A + 1) + (A - 1) * c + t),
        -2.0 * A * ((A - 1) + (A + 1) * c),
        A * ((A + 1) + (A - 1) * c - t),
        (A + 1) - (A - 1) * c + t,
        2.0 * ((A - 1) - (A + 1) * c),
        (A + 1) - (A - 1) * c - t);
}

void ConsumerEnhancer::Biquad::setPeaking(double fs, double f0, double q,
                                          double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * q);
    set(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A,
        1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
}

void ConsumerEnhancer::Biquad::setLowPass(double fs, double f0) noexcept {
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * 0.70710678118654752);
    set((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5,
        1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

void ConsumerEnhancer::Biquad::setHighPass(double fs, double f0) noexcept {
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * 0.70710678118654752);
    set((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5,
        1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

// ---------------------------------------------------------------- setup

bool ConsumerEnhancer::prepare(const Config& requested) noexcept {
    prepared_ = false;
    if (!std::isfinite(requested.sampleRateHz) || requested.sampleRateHz < 8000.0f ||
        requested.sampleRateHz > 384000.0f) {
        return false;
    }
    fs_ = requested.sampleRateHz;
    const double fs = fs_;
    const double nyq = fs * 0.45;

    smoothCoef_ = expCoef(25.0f, fs_);
    bassEnvAttack_ = expCoef(3.0f, fs_);
    bassEnvRelease_ = expCoef(60.0f, fs_);
    clarityAttack_ = expCoef(1.0f, fs_);
    clarityRelease_ = expCoef(40.0f, fs_);
    dcCoef_ = 1.0f - 2.0f * static_cast<float>(kPi) * 10.0f / fs_;
    punchFastAtt_ = expCoef(0.5f, fs_);
    punchFastRel_ = expCoef(25.0f, fs_);
    punchSlowAtt_ = expCoef(25.0f, fs_);
    punchSlowRel_ = expCoef(25.0f, fs_);
    punchGainAtt_ = expCoef(0.4f, fs_);
    punchGainRel_ = expCoef(20.0f, fs_);
    glueAtt_ = expCoef(10.0f, fs_);
    glueRel_ = expCoef(150.0f, fs_);

    autoPowerCoef_ = expCoef(400.0f, fs_);
    autoAttack_ = expCoef(400.0f, fs_);
    autoRelease_ = expCoef(2500.0f, fs_);
    harshAtt_ = expCoef(1.0f, fs_);
    harshRel_ = expCoef(40.0f, fs_);
    harshFullAtt_ = expCoef(5.0f, fs_);
    harshFullRel_ = expCoef(80.0f, fs_);
    for (int k = 0; k < 2; ++k) {
        autoHp_[k].setHighPass(fs, std::min(80.0, nyq));
        harshHp_[k].setHighPass(fs, std::min(4500.0, nyq));
        harshLp_[k].setLowPass(fs, std::min(9000.0, nyq));
    }

    detailEnvAtt_ = expCoef(1.0f, fs_);
    detailEnvRel_ = expCoef(30.0f, fs_);
    detailRefCoef_ = expCoef(1200.0f, fs_);
    detailFloorFall_ = expCoef(20.0f, fs_);
    detailFloorRise_ = std::pow(2.0f, 1.0f / (8.0f * fs_));   // floor may rise 6 dB per 8 s
    microFastAtt_ = expCoef(0.3f, fs_);
    microFastRel_ = expCoef(8.0f, fs_);
    microSlowAtt_ = expCoef(10.0f, fs_);
    microSlowRel_ = expCoef(10.0f, fs_);
    microAtt_ = expCoef(0.5f, fs_);
    microRel_ = expCoef(10.0f, fs_);
    detailGainAtt_ = expCoef(4.0f, fs_);
    detailGainRel_ = expCoef(50.0f, fs_);
    matchCoef_ = expCoef(600.0f, fs_);
    const float edges[3] = {200.0f, 1200.0f, 4500.0f};
    for (auto& d : detail_)
        for (int k = 0; k < 3; ++k) d.lp[k].set(edges[k], fs_);
    for (auto& hp : outHp_) hp.setHighPass(fs, std::min(150.0, nyq));
    for (auto& hp : inHp_) hp.setHighPass(fs, std::min(150.0, nyq));
    spaceSafeCoef_ = std::pow(expCoef(200.0f, fs_), 4.0f);   // control runs every 4th frame

    try {
        const std::size_t size = static_cast<std::size_t>(std::ceil(0.040 * fs)) + 2U;
        spaceBuf_.assign(size, 0.0f);
    } catch (...) {
        spaceBuf_.clear();
        return false;
    }
    for (std::size_t k = 0; k < kSpaceTaps; ++k) {
        spaceDelay_[k] = static_cast<std::size_t>(kSpaceTapMs[k] * 0.001 * fs);
    }
    spaceHp_.setHighPass(fs, std::min(250.0, nyq));
    spaceLp_.setLowPass(fs, std::min(7000.0, nyq));
    sideWidenHp_.setHighPass(fs, std::min(450.0, nyq));

    // Harmonic bass: 4th-order low-pass at 100 Hz picks the fundamental region,
    // 4th-order high-pass at 140 Hz plus a 450 Hz low-pass keep only harmonics.
    bassLp1_.setLowPass(fs, std::min(100.0, nyq));
    bassLp2_.setLowPass(fs, std::min(100.0, nyq));
    bassHp1_.setHighPass(fs, std::min(140.0, nyq));
    bassHp2_.setHighPass(fs, std::min(140.0, nyq));
    bassBandLp_.setLowPass(fs, std::min(450.0, nyq));

    for (auto& c : ch_) {
        c.clarityHp.setHighPass(fs, std::min(3200.0, nyq));
        c.clarityHp2.setHighPass(fs, std::min(6000.0, nyq));
    }

    // Warmth: asymmetric tanh normalised to unity small-signal gain.
    satBias_ = 0.2f;
    satDrive_ = 2.2f;
    const float tb = std::tanh(satBias_);
    satOffset_ = tb;
    satNorm_ = 1.0f / (satDrive_ * (1.0f - tb * tb));

    clearStates();
    for (std::size_t i = 0; i < kCount; ++i) smooth_[i] = 0.0f;
    active_ = false;
    prepared_ = true;
    updateCoefficients();
    return true;
}

void ConsumerEnhancer::clearStates() noexcept {
    midLowShelf_.clear(); midHighShelf_.clear(); midPresence_.clear();
    midMud_.clear(); sideHighShelf_.clear();
    bassLp1_.clear(); bassLp2_.clear(); bassHp1_.clear(); bassHp2_.clear();
    bassBandLp_.clear();
    bassEnv_ = 0.0f;
    for (auto& c : ch_) {
        c.clarityHp.clear(); c.clarityHp2.clear();
        c.clarityEnv = 0.0f;
        c.warmthDc.clear();
    }
    punchFast_ = punchSlow_ = 0.0f;
    punchGain_ = 1.0f;
    gluePower_ = 0.0f;
    std::fill(spaceBuf_.begin(), spaceBuf_.end(), 0.0f);
    spaceWrite_ = 0U;
    for (int k = 0; k < 2; ++k) { autoHp_[k].clear(); harshHp_[k].clear(); harshLp_[k].clear(); }
    autoPower_ = 0.0f; autoGainDb_ = 0.0f;
    for (auto& d : detail_) {
        for (auto& lp : d.lp) lp.clear();
        for (auto& b : d.band) b = DetailBand();
    }
    detailCounter_ = 0U; sideSafe_ = 1.0f; bassMaskDb_ = 0.0f;
    for (auto& hp : outHp_) hp.clear();
    for (auto& hp : inHp_) hp.clear();
    inPower_ = outPower_ = 0.0f; matchDb_ = 0.0f; matchDesired_ = 0.0f; spaceSafe_ = 1.0f;
    harshBandEnv_ = harshFullEnv_ = 0.0f;
    spaceHp_.clear(); spaceLp_.clear(); sideWidenHp_.clear();
}

void ConsumerEnhancer::reset() noexcept {
    clearStates();
    for (std::size_t i = 0; i < kCount; ++i) smooth_[i] = 0.0f;
    active_ = false;
}

void ConsumerEnhancer::updateCoefficients() noexcept {
    const double fs = fs_;
    const double nyq = fs * 0.45;
    const float need = smooth_[kNeed];
    const double lowDb = smooth_[kBass] * (2.5 + 4.5 * need);        // up to +7 dB
    const double highDb = smooth_[kClarity] * (1.5 + 2.5 * need);    // up to +4 dB
    midLowShelf_.setLowShelf(fs, std::min(120.0, nyq), lowDb);
    midHighShelf_.setHighShelf(fs, std::min(7500.0, nyq), highDb);
    sideHighShelf_.setHighShelf(fs, std::min(7500.0, nyq), highDb);
    midPresence_.setPeaking(fs, std::min(2800.0, nyq), 0.9, 2.8 * smooth_[kMelody]);
    midMud_.setPeaking(fs, std::min(320.0, nyq), 0.8, -1.2 * smooth_[kMelody]);
}

// ---------------------------------------------------------------- controls

void ConsumerEnhancer::setMacroPercent(float p) noexcept {
    macroTarget_.store(clampFinite(p, 0.0f, 100.0f, 0.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setBassPercent(float p) noexcept {
    bassTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setPunchPercent(float p) noexcept {
    punchTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setClarityPercent(float p) noexcept {
    clarityTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setWarmthPercent(float p) noexcept {
    warmthTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setMelodyPercent(float p) noexcept {
    melodyTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setGluePercent(float p) noexcept {
    glueTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setSpacePercent(float p) noexcept {
    spaceTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setShieldPercent(float p) noexcept {
    shieldTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setDetailPercent(float p) noexcept {
    detailTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setMatchPercent(float p) noexcept {
    matchTrim_.store(clampFinite(p, 0.0f, 100.0f, 100.0f) * 0.01f, std::memory_order_relaxed);
}
void ConsumerEnhancer::setPlaybackVolume(float v) noexcept {
    playbackVolume_.store(clampFinite(v, 0.0f, 1.0f, 0.5f), std::memory_order_relaxed);
}
float ConsumerEnhancer::getTargetMacroPercent() const noexcept {
    return macroTarget_.load(std::memory_order_relaxed) * 100.0f;
}

float ConsumerEnhancer::saturate(float x) const noexcept {
    return (std::tanh(satDrive_ * x + satBias_) - satOffset_) * satNorm_;
}

// ---------------------------------------------------------------- background detail

void ConsumerEnhancer::processDetail(float& mid, float& side) noexcept {
    const float amount = smooth_[kDetail];
    const bool control = (detailCounter_++ & 3U) == 0U;

    if (control) {
        // Stereo safety and bass-masking decisions from the smoothed band envelopes.
        float sumM = 0.0f, sumS = 0.0f;
        for (int b = 0; b < 4; ++b) {
            sumM += detail_[0].band[b].env;
            sumS += detail_[1].band[b].env;
        }
        const float sideRatio = sumS / (sumM + 1.0e-6f);
        sideSafe_ = clamp01((1.3f - sideRatio) / 0.5f);         // wide/hollow material: no side lift
        const float spaceTarget = clamp01((1.1f - sideRatio) / 0.6f);
        spaceSafe_ = spaceTarget + spaceSafeCoef_ * (spaceSafe_ - spaceTarget);  // Space backs off on wide mixes
        const float lowShare = (detail_[0].band[0].env + detail_[0].band[1].env) /
                               (sumM + 1.0e-9f);
        bassMaskDb_ = kBassMaskMaxDb * clamp01((lowShare - 0.75f) / 0.2f);
    }

    const float in[2] = {mid, side};
    float outSig[2] = {0.0f, 0.0f};
    for (int s = 0; s < 2; ++s) {
        DetailSignal& d = detail_[s];
        const float l1 = d.lp[0].process(in[s]);
        const float l2 = d.lp[1].process(in[s]);
        const float l3 = d.lp[2].process(in[s]);
        const float bands[4] = {l1, l2 - l1, l3 - l2, in[s] - l3};   // sums back to in[s]
        float acc = 0.0f;
        for (int b = 0; b < 4; ++b) {
            DetailBand& db = d.band[b];
            const float a = std::fabs(bands[b]);

            const float ec = a > db.env ? detailEnvAtt_ : detailEnvRel_;
            db.env = ec * db.env + (1.0f - ec) * a;
            db.ref = detailRefCoef_ * db.ref + (1.0f - detailRefCoef_) * db.env;

            // Adaptive noise floor: follows the quietest recent level, creeps up slowly.
            if (db.env < db.floor) {
                db.floor = detailFloorFall_ * db.floor + (1.0f - detailFloorFall_) * db.env;
            } else {
                db.floor *= detailFloorRise_;
            }
            if (db.floor < 1.0e-7f) db.floor = 1.0e-7f;

            const float fc = a > db.fast ? microFastAtt_ : microFastRel_;
            db.fast = fc * db.fast + (1.0f - fc) * a;
            const float sc = a > db.slow ? microSlowAtt_ : microSlowRel_;
            db.slow = sc * db.slow + (1.0f - sc) * a;

            if (control) {
                const float e = db.env + 1.0e-9f;
                const float relDb = 6.0206f * std::log2(e / (db.ref + 1.0e-9f));
                const float noiseDb = 6.0206f * std::log2(e / (db.floor + 1.0e-9f));
                const float above = clamp01((relDb - 1.0f) / 6.0f);    // foreground event
                const float quiet = clamp01((-relDb - 3.0f) / 9.0f);  // quiet vs its own level
                const float clean = clamp01((noiseDb - 9.0f) / 12.0f); // well above the noise floor
                float boost = kDetailMaxBoostDb[b] * quiet * clean * amount;
                if (s == 1) boost *= kSideBoostScale * sideSafe_;
                float cut = kMaskCutDb[b] * above * amount;
                if (s == 0 && b < 2) cut += bassMaskDb_ * amount;
                db.target = std::exp2((boost - cut) * 0.16609640474f);
                db.quietNoise = quiet * clean;
            }
            const float gc = db.target < db.gain ? detailGainAtt_ : detailGainRel_;
            db.gain = db.target + gc * (db.gain - db.target);

            // Micro-detail: small attacks in the upper bands, only while the band is quiet.
            float microTarget = 1.0f;
            if (b >= 2) {
                const float attack = clamp01((db.fast - db.slow) / (db.fast + 1.0e-6f));
                microTarget = 1.0f + kMicroBoost * attack * db.quietNoise * amount;
            }
            const float mc = microTarget > db.micro ? microAtt_ : microRel_;
            db.micro = microTarget + mc * (db.micro - microTarget);

            acc += bands[b] * db.gain * db.micro;
        }
        outSig[s] = acc;
    }
    mid = outSig[0];
    side = outSig[1];
}

// ---------------------------------------------------------------- process

void ConsumerEnhancer::process(float* samples, std::size_t frames) noexcept {
    if (!prepared_ || samples == nullptr || frames == 0U) return;

    const float macro = clamp01(macroTarget_.load(std::memory_order_relaxed));
    const float eased = macro * (2.0f - macro);
    if (macro <= 1.0e-4f && !active_) return;  // exact bypass
    if (macro > 1.0e-4f && !active_) {
        clearStates();
        for (std::size_t i = 0; i < kCount; ++i) smooth_[i] = 0.0f;
        active_ = true;
        updateCoefficients();
    }

    float target[kCount];
    target[kMacro]   = eased;
    // The single slider travels through the stages in order, so low settings are a
    // tasteful tone lift and high settings add the full show.
    target[kMelody]  = stage(macro, 0.00f, 0.45f) * melodyTrim_.load(std::memory_order_relaxed);
    target[kBass]    = stage(macro, 0.00f, 0.50f) * bassTrim_.load(std::memory_order_relaxed);
    target[kWarmth]  = stage(macro, 0.05f, 0.70f) * warmthTrim_.load(std::memory_order_relaxed);
    target[kGlue]    = stage(macro, 0.10f, 0.80f) * glueTrim_.load(std::memory_order_relaxed);
    target[kClarity] = stage(macro, 0.20f, 0.90f) * clarityTrim_.load(std::memory_order_relaxed);
    target[kPunch]   = stage(macro, 0.25f, 1.00f) * punchTrim_.load(std::memory_order_relaxed);
    target[kSpace]   = stage(macro, 0.35f, 1.00f) * spaceTrim_.load(std::memory_order_relaxed);
    target[kDetail]  = stage(macro, 0.00f, 1.00f) * detailTrim_.load(std::memory_order_relaxed);
    target[kMatch]   = stage(macro, 0.00f, 0.20f) * matchTrim_.load(std::memory_order_relaxed);
    target[kLevel]   = stage(macro, 0.00f, 0.40f);
    target[kShield]  = stage(macro, 0.25f, 0.90f) * shieldTrim_.load(std::memory_order_relaxed);
    target[kMax]     = stage(macro, 0.30f, 0.90f);
    const float vol = playbackVolume_.load(std::memory_order_relaxed);
    target[kNeed]    = clamp01((0.65f - vol) / 0.65f);

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t idx = frame * 2U;
        float in[2] = {samples[idx], samples[idx + 1U]};
        for (float& v : in) {
            if (!std::isfinite(v)) v = 0.0f;
            v = std::max(-kSafeMagnitude, std::min(v, kSafeMagnitude));
        }

        for (std::size_t i = 0; i < kCount; ++i) {
            smooth_[i] = target[i] + smoothCoef_ * (smooth_[i] - target[i]);
        }
        if ((ctrlCounter_++ & 31U) == 0U) updateCoefficients();

        // ---- 0. Auto-level: slow, gated, asymmetric (lifts quiet, barely trims loud)
        {
            const float hpL = autoHp_[0].process(in[0]);
            const float hpR = autoHp_[1].process(in[1]);
            const float p = 0.5f * (hpL * hpL + hpR * hpR);
            autoPower_ = autoPowerCoef_ * autoPower_ + (1.0f - autoPowerCoef_) * p;
            const float levelDb = 10.0f * std::log10(autoPower_ + 1.0e-12f);
            const float weight = clamp01((levelDb + 60.0f) / 10.0f);  // silence/fades: hold
            const float boost = std::max(0.0f, std::min(kAutoMaxBoostDb, kAutoFloorDb - levelDb));
            const float cut = std::max(0.0f, std::min(kAutoMaxCutDb, levelDb - kAutoCeilDb));
            const float desired = (boost - cut) * weight;
            const float ac = desired < autoGainDb_ ? autoAttack_ : autoRelease_;
            autoGainDb_ = desired + ac * (autoGainDb_ - desired);
            const float g = std::exp2(autoGainDb_ * smooth_[kLevel] * 0.16609640474f);
            in[0] *= g;
            in[1] *= g;
        }

        // ---- 1+2. Mid/Side tone and harmonic bass
        float mid = 0.5f * (in[0] + in[1]);
        float side = 0.5f * (in[0] - in[1]);
        processDetail(mid, side);

        const float lowMid = bassLp2_.process(bassLp1_.process(mid));
        const float absLow = std::fabs(lowMid);
        const float bassCoef = absLow > bassEnv_ ? bassEnvAttack_ : bassEnvRelease_;
        bassEnv_ = bassCoef * bassEnv_ + (1.0f - bassCoef) * absLow;
        const float xn = std::max(-1.0f, std::min(1.0f, lowMid / (bassEnv_ + 1.0e-5f)));
        // Odd harmonics from the soft clip, even harmonics from the squared term.
        const float shaped = 0.6f * std::tanh(2.5f * xn) + 0.8f * (xn * xn - 0.5f);
        const float harmonics = bassBandLp_.process(bassHp2_.process(bassHp1_.process(shaped)));
        const float bassGate = clamp01((bassEnv_ - 2.0e-4f) / 2.0e-3f);
        const float bassHarm = harmonics * bassEnv_ * bassGate * smooth_[kBass] *
                               kBassHarmonicGain;

        float midT = midLowShelf_.process(mid);
        midT = midMud_.process(midT);
        midT = midPresence_.process(midT);
        midT = midHighShelf_.process(midT);
        midT += bassHarm;
        const float sideT = sideHighShelf_.process(side);

        float out[2] = {midT + sideT, midT - sideT};

        // ---- 3+4. Warmth and clarity per channel
        for (int c = 0; c < 2; ++c) {
            float x = out[c];

            const float delta = saturate(x) - x;
            DcBlocker& dc = ch_[c].warmthDc;
            const float blocked = delta - dc.xPrev + dcCoef_ * dc.yPrev;
            dc.xPrev = delta;
            dc.yPrev = std::fabs(blocked) < 1.0e-20f ? 0.0f : blocked;
            x += smooth_[kWarmth] * kWarmthMix * dc.yPrev;

            const float hp = ch_[c].clarityHp.process(x);
            const float absHp = std::fabs(hp);
            const float cc = absHp > ch_[c].clarityEnv ? clarityAttack_ : clarityRelease_;
            ch_[c].clarityEnv = cc * ch_[c].clarityEnv + (1.0f - cc) * absHp;
            const float hn = std::max(-1.0f, std::min(1.0f,
                                      hp / (ch_[c].clarityEnv + 1.0e-6f)));
            const float distortion = hn - 0.25f * std::tanh(4.0f * hn);
            const float air = ch_[c].clarityHp2.process(distortion);
            // Fade the exciter out below about -70 dBFS so hiss is never lifted.
            const float clarityGate = clamp01((ch_[c].clarityEnv - 3.0e-4f) / 1.5e-3f);
            x += air * ch_[c].clarityEnv * clarityGate * smooth_[kClarity] * kClarityGain;

            out[c] = x;
        }

        // ---- 4b. Harshness shield: dynamic cut of the 4.5-9 kHz band when it dominates
        {
            const float bL = harshLp_[0].process(harshHp_[0].process(out[0]));
            const float bR = harshLp_[1].process(harshHp_[1].process(out[1]));
            const float bandLevel = std::max(std::fabs(bL), std::fabs(bR));
            const float full = std::max(std::fabs(out[0]), std::fabs(out[1]));
            const float bc = bandLevel > harshBandEnv_ ? harshAtt_ : harshRel_;
            harshBandEnv_ = bc * harshBandEnv_ + (1.0f - bc) * bandLevel;
            const float fc2 = full > harshFullEnv_ ? harshFullAtt_ : harshFullRel_;
            harshFullEnv_ = fc2 * harshFullEnv_ + (1.0f - fc2) * full;
            const float ratio = harshBandEnv_ / (harshFullEnv_ + 1.0e-6f);
            const float gate = clamp01((harshFullEnv_ - 5.0e-4f) / 3.0e-3f);
            const float excess = clamp01((ratio - kHarshRatioStart) / 0.25f) * gate;
            const float cut = excess * kHarshMaxCut * smooth_[kShield];
            out[0] -= cut * bL;
            out[1] -= cut * bR;
        }

        // ---- 5. Punch (linked transient shaper)
        float attackAmount = 0.0f;
        {
            const float level = std::max(std::fabs(out[0]), std::fabs(out[1]));
            const float fc = level > punchFast_ ? punchFastAtt_ : punchFastRel_;
            punchFast_ = fc * punchFast_ + (1.0f - fc) * level;
            const float sc = level > punchSlow_ ? punchSlowAtt_ : punchSlowRel_;
            punchSlow_ = sc * punchSlow_ + (1.0f - sc) * level;
            const float attack = clamp01((punchFast_ - punchSlow_) / (punchFast_ + 1.0e-6f));
            const float gate = clamp01((punchFast_ - 5.0e-4f) / 4.0e-3f);
            attackAmount = attack * gate;
            const float targetGain = 1.0f + smooth_[kPunch] * kPunchMaxBoost * attack * gate;
            const float gc = targetGain > punchGain_ ? punchGainAtt_ : punchGainRel_;
            punchGain_ = targetGain + gc * (punchGain_ - targetGain);
            out[0] *= punchGain_;
            out[1] *= punchGain_;
        }

        // ---- 6. Glue: parallel compression
        {
            const float power = 0.5f * (out[0] * out[0] + out[1] * out[1]);
            const float gcoef = power > gluePower_ ? glueAtt_ : glueRel_;
            gluePower_ = gcoef * gluePower_ + (1.0f - gcoef) * power;
            const float levelDb = 10.0f * std::log10(gluePower_ + 1.0e-12f);
            const float over = levelDb - kGlueThresholdDb;
            const float slope = 1.0f - 1.0f / kGlueRatio;
            float reduction = 0.0f;
            if (over > 0.5f * kGlueKneeDb) {
                reduction = slope * over;
            } else if (over > -0.5f * kGlueKneeDb) {
                const float t = over + 0.5f * kGlueKneeDb;
                reduction = slope * t * t / (2.0f * kGlueKneeDb);
            }
            const float gain = std::exp2(-reduction * 0.16609640474f);
            const float wetGain = smooth_[kGlue] * kGlueMix * kGlueMakeup * gain;
            out[0] += out[0] * wetGain;
            out[1] += out[1] * wetGain;
        }

        // ---- 7. Space: ambience + airy width, Side only (mono sum untouched)
        {
            const float m = 0.5f * (out[0] + out[1]);
            float s = 0.5f * (out[0] - out[1]);
            const std::size_t size = spaceBuf_.size();
            spaceBuf_[spaceWrite_] = m;
            float amb = 0.0f;
            for (std::size_t k = 0; k < kSpaceTaps; ++k) {
                const std::size_t idx = (spaceWrite_ + size - spaceDelay_[k]) % size;
                amb += kSpaceTapGain[k] * spaceBuf_[idx];
            }
            spaceWrite_ = (spaceWrite_ + 1U) % size;
            amb = spaceLp_.process(spaceHp_.process(amb));
            const float tail = 1.0f - 0.6f * attackAmount;  // keep drum hits dry
            const float airySide = sideWidenHp_.process(s);
            s += smooth_[kSpace] * spaceSafe_ * (kSpaceAmbience * tail * amb + kSpaceWiden * airySide);
            out[0] = m + s;
            out[1] = m - s;
        }

        // ---- 8. Loudness match: hold output loudness to the (leveled) input loudness
        {
            const float iL = inHp_[0].process(in[0]);
            const float iR = inHp_[1].process(in[1]);
            const float oL = outHp_[0].process(out[0]);
            const float oR = outHp_[1].process(out[1]);
            inPower_ = autoPowerCoef_ * inPower_ +
                       (1.0f - autoPowerCoef_) * 0.5f * (iL * iL + iR * iR);
            outPower_ = autoPowerCoef_ * outPower_ +
                        (1.0f - autoPowerCoef_) * 0.5f * (oL * oL + oR * oR);
            if ((ctrlCounter_ & 3U) == 0U) {
                const float refPower = inPower_ + 1.0e-12f;
                const float diffDb = 10.0f * std::log10(refPower / (outPower_ + 1.0e-12f));
                const float weight = clamp01((10.0f * std::log10(refPower) + 60.0f) / 10.0f);
                matchDesired_ = std::max(-kMatchMaxCutDb, std::min(kMatchMaxLiftDb, diffDb)) *
                                weight * smooth_[kMatch];
            }
            matchDb_ = matchDesired_ + matchCoef_ * (matchDb_ - matchDesired_);
            const float mg = std::exp2(matchDb_ * 0.16609640474f);
            out[0] *= mg;
            out[1] *= mg;
        }

        for (int c = 0; c < 2; ++c) {
            float y = out[c];
            if (!std::isfinite(y)) y = 0.0f;
            y += smooth_[kMax] * (softClip(y) - y);
            samples[idx + static_cast<std::size_t>(c)] =
                std::max(-kSafeMagnitude, std::min(y, kSafeMagnitude));
        }
    }

    if (macro <= 1.0e-4f && smooth_[kMacro] < 1.0e-4f) {
        active_ = false;  // fully faded out; next call is a bit-exact bypass
    }
}

} // namespace stereo_dsp
