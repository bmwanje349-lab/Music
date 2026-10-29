#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace stereo_dsp {

// Consumer-style "fun" enhancement chain (interleaved stereo float).
//
// Stages, in order (all realtime-safe, no allocation after prepare()):
//   1. Mid/Side tone: volume-aware loudness contour (low/high shelves),
//      melody presence (+2.8 kHz) and mud cut (-320 Hz) on the Mid only.
//   2. Harmonic ("virtual") bass: level-normalised waveshaper on the mono
//      low band, band-passed so the harmonics (2nd/3rd...) add perceived bass
//      weight on small speakers/earbuds. Stays centred (mono). A "thump" lift of the
//      real low end follows bass events (kick / bass-note onsets) for impact.
//   3. Warmth: asymmetric tube-style saturation, normalised to unity small-signal gain.
//   4. Clarity: gated high-band harmonic exciter ("air"/sparkle).
//   5. Punch: transient shaper (fast vs slow envelope) for drum snap.
//   6. Glue: parallel compression for density / perceived loudness.
//   7. Space: decorrelated ambience taps fed ONLY into the Side channel (depth,
//      "room" feel; cancels exactly in mono) plus airy high-frequency Side lift.
//      Ambience ducks on drum attacks so punch stays dry and centred while
//      tails and sustained notes bloom wide.
//
//   Background Detail engine (right after auto-level): reveals quiet inner detail,
//      tails, backing parts and micro-attacks band by band, separately for Mid and
//      Side, reduces foreground masking, protects the noise floor adaptively.
//   Loudness match (before soft clip): output loudness is held to the input loudness so
//      "louder" is never mistaken for "better".
//   0. Auto-level (first): slow leveler lifts quiet tracks, trims only very hot ones.
//   Harshness shield: dynamic 4.5-9 kHz cut so the brightness never turns fatiguing.
//   Soft clip (last): rounds peaks for loudness without limiter pumping.
//
// Run it BEFORE StereoWidener::process so the widener's look-ahead peak guard
// protects the final output.
//
// ONE slider ("Fun") drives everything. Each stage has its own response curve on
// that slider (see process()). At 0% the call is a bit-exact bypass.
class ConsumerEnhancer {
public:
    struct Config {
        float sampleRateHz = 48000.0f;
    };

    bool prepare(const Config& requested) noexcept;
    void reset() noexcept;
    void process(float* samples, std::size_t frames) noexcept;

    // Controls: safe to call from any thread.
    // THE slider. 0 = off (bit-exact bypass), 100 = full effect. It walks through the
    // stages in a designed order: tone/melody/bass first, then warmth, density and
    // clarity, then punch and spatial depth, so every position sounds intentional.
    void setMacroPercent(float percent) noexcept;    // 0..100, "Fun"
    // Normalised listening volume 0..1 (app/system volume). Lower volume =>
    // stronger bass/treble contour (equal-loudness compensation).
    void setPlaybackVolume(float normalized) noexcept;

    float getTargetMacroPercent() const noexcept;

private:
    friend struct ConsumerEnhancerTestAccess;
    void setBassPercent(float percent) noexcept;     // 0..100 trim
    void setShieldPercent(float percent) noexcept;   // 0..100 trim
    void setDetailPercent(float percent) noexcept;   // 0..100 trim
    void setMatchPercent(float percent) noexcept;    // 0..100 trim (test hook)
    void setPunchPercent(float percent) noexcept;    // 0..100 trim
    void setClarityPercent(float percent) noexcept;  // 0..100 trim
    void setWarmthPercent(float percent) noexcept;   // 0..100 trim
    void setMelodyPercent(float percent) noexcept;   // 0..100 trim
    void setGluePercent(float percent) noexcept;     // 0..100 trim
    void setSpacePercent(float percent) noexcept;    // 0..100 trim, depth/ambience + airy width

    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        void clear() noexcept { z1 = z2 = 0.0; }
        float process(float x) noexcept;
        void setLowShelf(double fs, double f0, double gainDb) noexcept;
        void setHighShelf(double fs, double f0, double gainDb) noexcept;
        void setPeaking(double fs, double f0, double q, double gainDb) noexcept;
        void setLowPass(double fs, double f0) noexcept;
        void setHighPass(double fs, double f0) noexcept;
        void set(double nb0, double nb1, double nb2, double na0, double na1,
                 double na2) noexcept;
    };

    struct DcBlocker {
        float xPrev = 0.0f, yPrev = 0.0f;
        void clear() noexcept { xPrev = yPrev = 0.0f; }
    };

    struct ChannelState {
        Biquad clarityHp;       // 3.2 kHz detector high-pass
        Biquad clarityHp2;      // 6 kHz harmonic high-pass
        float clarityEnv = 0.0f;
        DcBlocker warmthDc;
    };

    enum Param : std::size_t {
        kMacro = 0, kBass, kPunch, kClarity, kWarmth, kMelody, kGlue, kSpace, kLevel, kShield, kMax, kDetail, kMatch, kNeed, kCount
    };

    void clearStates() noexcept;
    void updateCoefficients() noexcept;
    static float clampFinite(float v, float lo, float hi, float fb) noexcept;
    static float expCoef(float ms, float fs) noexcept;
    float saturate(float x) const noexcept;

    std::atomic<float> macroTarget_{0.0f};
    std::atomic<float> bassTrim_{1.0f};
    std::atomic<float> punchTrim_{1.0f};
    std::atomic<float> clarityTrim_{1.0f};
    std::atomic<float> warmthTrim_{1.0f};
    std::atomic<float> melodyTrim_{1.0f};
    std::atomic<float> glueTrim_{1.0f};
    std::atomic<float> spaceTrim_{1.0f};
    std::atomic<float> shieldTrim_{1.0f};
    std::atomic<float> detailTrim_{1.0f};
    std::atomic<float> matchTrim_{1.0f};
    std::atomic<float> playbackVolume_{0.5f};

    bool prepared_ = false;
    bool active_ = false;
    float fs_ = 48000.0f;

    float smooth_[kCount] = {};
    float smoothCoef_ = 0.0f;
    std::uint32_t ctrlCounter_ = 0U;

    // Mid/Side tone
    Biquad midLowShelf_, midHighShelf_, midPresence_, midMud_, sideHighShelf_;

    // Harmonic bass
    Biquad bassLp1_, bassLp2_, bassHp1_, bassHp2_, bassBandLp_;
    float bassEnv_ = 0.0f;
    float bassEnvAttack_ = 0.0f, bassEnvRelease_ = 0.0f;

    ChannelState ch_[2];
    float clarityAttack_ = 0.0f, clarityRelease_ = 0.0f;
    float dcCoef_ = 0.999f;

    // Warmth constants
    float satBias_ = 0.2f, satDrive_ = 2.2f, satNorm_ = 1.0f, satOffset_ = 0.0f;

    // Punch
    float punchFast_ = 0.0f, punchSlow_ = 0.0f, punchGain_ = 1.0f;
    float punchFastAtt_ = 0.0f, punchFastRel_ = 0.0f;
    float punchSlowAtt_ = 0.0f, punchSlowRel_ = 0.0f;
    float punchGainAtt_ = 0.0f, punchGainRel_ = 0.0f;

    // Background Detail engine: 4 bands x {Mid, Side}, band-wise upward detail recovery,
    // masking reduction, adaptive noise floor, micro-detail, bass-masking and side safety.
    struct OnePole {
        float fb = 0.0f, state = 0.0f;
        void set(float fc, float fs) noexcept;
        float process(float x) noexcept;
        void clear() noexcept { state = 0.0f; }
    };
    struct DetailBand {
        float env = 0.0f, ref = 0.0f, floor = 1.0f;
        float fast = 0.0f, slow = 0.0f;
        float gain = 1.0f, target = 1.0f;
        float micro = 1.0f, quietNoise = 0.0f;
    };
    struct DetailSignal {
        OnePole lp[3];
        DetailBand band[4];
    };
    void processDetail(float& mid, float& side) noexcept;
    DetailSignal detail_[2];                 // 0 = Mid, 1 = Side
    std::uint32_t detailCounter_ = 0U;
    float sideSafe_ = 1.0f, spaceSafe_ = 1.0f, spaceSafeCoef_ = 0.0f, bassMaskDb_ = 0.0f;
    float detailEnvAtt_ = 0.0f, detailEnvRel_ = 0.0f, detailRefCoef_ = 0.0f;
    float detailFloorFall_ = 0.0f, detailFloorRise_ = 1.0f;
    float microFastAtt_ = 0.0f, microFastRel_ = 0.0f, microSlowAtt_ = 0.0f, microSlowRel_ = 0.0f;
    float microAtt_ = 0.0f, microRel_ = 0.0f, detailGainAtt_ = 0.0f, detailGainRel_ = 0.0f;

    // Loudness match: output loudness is steered back to the (leveled) input loudness
    Biquad inHp_[2], outHp_[2];   // 150 Hz high-pass: ear-like low-frequency weighting
    float inPower_ = 0.0f, outPower_ = 0.0f, matchDb_ = 0.0f, matchDesired_ = 0.0f;
    float matchCoef_ = 0.0f;

    // Auto-level (slow, asymmetric, gated) so every track hits the stages at a sensible level
    Biquad autoHp_[2];
    float autoPower_ = 0.0f, autoGainDb_ = 0.0f;
    float autoPowerCoef_ = 0.0f, autoAttack_ = 0.0f, autoRelease_ = 0.0f;

    // Harshness shield (dynamic 4.5-9 kHz reduction)
    Biquad harshHp_[2], harshLp_[2];
    float harshBandEnv_ = 0.0f, harshFullEnv_ = 0.0f;
    float harshAtt_ = 0.0f, harshRel_ = 0.0f, harshFullAtt_ = 0.0f, harshFullRel_ = 0.0f;

    // Space: original ambience plus a Bloom-inspired panned room layer.
    static constexpr std::size_t kSpaceTaps = 4;
    std::vector<float> spaceBuf_;
    std::size_t spaceWrite_ = 0U;
    std::size_t spaceDelay_[kSpaceTaps] = {};
    Biquad spaceHp_, spaceLp_, sideWidenHp_;

    static constexpr std::size_t kHaloRoomTaps = 3;
    std::vector<float> haloRoomBuf_;
    std::size_t haloRoomWrite_ = 0U;
    std::size_t haloRoomDelay_[kHaloRoomTaps] = {};
    Biquad haloRoomSideHp_, haloRoomSendHp_, haloRoomTapLp_[kHaloRoomTaps];
    Biquad haloAir_, haloFocus_, haloDip_, haloWarmth_;
    float haloRoomTapGainL_[kHaloRoomTaps] = {};
    float haloRoomTapGainR_[kHaloRoomTaps] = {};
    float haloRoomFeedback_ = 0.0f;

    // Glue
    float gluePower_ = 0.0f;
    float glueAtt_ = 0.0f, glueRel_ = 0.0f;
};

} // namespace stereo_dsp
