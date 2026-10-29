#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "consumer_enhancer.hpp"
#include "halo_engine.hpp"

namespace stereo_dsp {

// One entry point for both sound engines. Exactly ONE engine processes audio at a time:
//   Off               exact bypass (nothing runs)
//   ConsumerEnhancer  the single-slider consumer enhancer (set with setEnhancerPercent)
//   HaloBloom         Halo at immersion 100 with the Bloom preset (no controls)
//
// The two engines stay completely separate classes with no shared state. The selector only
// decides which one gets the audio, and it swaps them without clicks:
//   Engine -> Engine : the output fades out (12 ms), the engines swap while silent, it fades
//                      back in (40 ms, so the new engine settles unheard). The two engines are never in the signal path together.
//   Off <-> Engine   : a 30 ms cross-fade between the untouched input and the engine.
//
// Call prepare() before audio starts. setEngine (or the select... helpers) / setEnhancerPercent /
// setPlaybackVolume may be called from any thread while process() runs on the audio thread.
class EngineSelector {
public:
    enum class Engine : std::uint8_t { Off = 0, ConsumerEnhancer = 1, HaloBloom = 2 };

    struct Config {
        float sampleRateHz = 48000.0f;
    };

    bool prepare(const Config& requested) noexcept;
    void reset() noexcept;
    void process(float* samples, std::size_t frames) noexcept;   // interleaved stereo float

    // Engine selection: always YOUR choice. Nothing in the selector ever changes the engine
    // by itself (no auto-switching, no cycling); it only reacts to these calls.
    static constexpr int kEngineCount = 3;   // for building a picker: Off, Consumer Enhancer, Halo Bloom
    static const char* engineName(Engine engine) noexcept;
    void setEngine(Engine engine) noexcept;
    void selectOff() noexcept { setEngine(Engine::Off); }
    void selectConsumerEnhancer() noexcept { setEngine(Engine::ConsumerEnhancer); }
    void selectHaloBloom() noexcept { setEngine(Engine::HaloBloom); }
    Engine selectedEngine() const noexcept;   // what you chose
    // Which engine is actually processing right now (lags the request by a few ms).
    Engine activeEngine() const noexcept;

    // Controls
    void setEnhancerPercent(float percent) noexcept;   // 0..100, the enhancer's one slider
    void setPlaybackVolume(float normalized) noexcept; // 0..1, for the enhancer's loudness contour

private:
    enum class Phase : std::uint8_t { Steady, DuckOut, DuckIn, FadeIn, FadeOut };

    void run(Engine engine, float* samples, std::size_t frames) noexcept;
    void begin(Engine engine) noexcept;
    void end(Engine engine) noexcept;
    void publish() noexcept;

    ConsumerEnhancer enhancer_;
    HaloEngine halo_;

    std::atomic<std::uint8_t> requested_{static_cast<std::uint8_t>(Engine::Off)};
    std::atomic<std::uint8_t> published_{static_cast<std::uint8_t>(Engine::Off)};
    std::atomic<float> sliderPercent_{60.0f};

    // Audio-thread state
    bool prepared_ = false;
    Engine current_ = Engine::Off;
    Phase phase_ = Phase::Steady;
    std::size_t fadeLeft_ = 0U;
    std::size_t duckSamples_ = 576U;      // fade-out before an engine swap
    std::size_t duckInSamples_ = 1920U;   // slower fade-in lets the new engine settle
    std::size_t xfadeSamples_ = 1440U;
};

} // namespace stereo_dsp
