#include "engine_selector.hpp"

#include <algorithm>
#include <cmath>

namespace stereo_dsp {
static_assert(std::atomic<std::uint8_t>::is_always_lock_free &&
              std::atomic<float>::is_always_lock_free,
              "EngineSelector requires lock-free atomics");

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kChunk = 256U;   // frames handled at once while cross-fading with the dry input

inline std::uint8_t toByte(EngineSelector::Engine e) noexcept { return static_cast<std::uint8_t>(e); }
inline EngineSelector::Engine fromByte(std::uint8_t b) noexcept {
    return b == 1U ? EngineSelector::Engine::ConsumerEnhancer
         : b == 2U ? EngineSelector::Engine::HaloBloom
                   : EngineSelector::Engine::Off;
}
// Raised-cosine ramps for progress p in 1..n
inline float rampUp(std::size_t p, std::size_t n) noexcept {
    return 0.5f * (1.0f - static_cast<float>(std::cos(kPi * static_cast<double>(p) / static_cast<double>(n))));
}
inline float rampDown(std::size_t p, std::size_t n) noexcept {
    return 0.5f * (1.0f + static_cast<float>(std::cos(kPi * static_cast<double>(p) / static_cast<double>(n))));
}
} // namespace

bool EngineSelector::prepare(const Config& requested) noexcept {
    prepared_ = false;
    if (!std::isfinite(requested.sampleRateHz) || requested.sampleRateHz < 8000.0f ||
        requested.sampleRateHz > 384000.0f) {
        return false;
    }
    ConsumerEnhancer::Config ec;
    ec.sampleRateHz = requested.sampleRateHz;
    HaloEngine::Config hc;
    hc.sampleRateHz = requested.sampleRateHz;
    if (!enhancer_.prepare(ec) || !halo_.prepare(hc)) return false;

    duckSamples_ = std::max<std::size_t>(16U,
        static_cast<std::size_t>(std::lround(0.012 * requested.sampleRateHz)));
    duckInSamples_ = std::max<std::size_t>(16U,
        static_cast<std::size_t>(std::lround(0.040 * requested.sampleRateHz)));
    xfadeSamples_ = std::max<std::size_t>(32U,
        static_cast<std::size_t>(std::lround(0.030 * requested.sampleRateHz)));
    current_ = Engine::Off;
    phase_ = Phase::Steady;
    fadeLeft_ = 0U;
    halo_.setBloomButton(false);
    publish();
    prepared_ = true;
    return true;
}

void EngineSelector::reset() noexcept {
    enhancer_.reset();
    halo_.reset();
    halo_.setBloomButton(false);
    current_ = Engine::Off;
    phase_ = Phase::Steady;
    fadeLeft_ = 0U;
    publish();
}

void EngineSelector::setEngine(Engine engine) noexcept {
    requested_.store(toByte(engine), std::memory_order_relaxed);
}
const char* EngineSelector::engineName(Engine engine) noexcept {
    switch (engine) {
        case Engine::ConsumerEnhancer: return "Consumer Enhancer";
        case Engine::HaloBloom: return "Halo Bloom";
        case Engine::Off: break;
    }
    return "Off";
}
EngineSelector::Engine EngineSelector::selectedEngine() const noexcept {
    return fromByte(requested_.load(std::memory_order_relaxed));
}
EngineSelector::Engine EngineSelector::activeEngine() const noexcept {
    return fromByte(published_.load(std::memory_order_relaxed));
}
void EngineSelector::setEnhancerPercent(float percent) noexcept {
    if (!std::isfinite(percent)) return;
    sliderPercent_.store(std::max(0.0f, std::min(100.0f, percent)), std::memory_order_relaxed);
}
void EngineSelector::setPlaybackVolume(float normalized) noexcept {
    enhancer_.setPlaybackVolume(normalized);
}

void EngineSelector::publish() noexcept {
    published_.store(toByte(current_), std::memory_order_relaxed);
}

void EngineSelector::run(Engine engine, float* samples, std::size_t frames) noexcept {
    switch (engine) {
        case Engine::ConsumerEnhancer: enhancer_.process(samples, frames); break;
        case Engine::HaloBloom: halo_.process(samples, frames); break;
        case Engine::Off: break;
    }
}

void EngineSelector::begin(Engine engine) noexcept {
    // The engine starts from clean state, exactly like a freshly prepared one.
    if (engine == Engine::ConsumerEnhancer) {
        enhancer_.reset();
        enhancer_.setMacroPercent(sliderPercent_.load(std::memory_order_relaxed));
    } else if (engine == Engine::HaloBloom) {
        halo_.reset();
        halo_.setBloomButton(true);
    }
}

void EngineSelector::end(Engine engine) noexcept {
    if (engine == Engine::ConsumerEnhancer) {
        enhancer_.setMacroPercent(0.0f);
        enhancer_.reset();
    } else if (engine == Engine::HaloBloom) {
        halo_.setBloomButton(false);
        halo_.reset();
    }
}

void EngineSelector::process(float* samples, std::size_t frames) noexcept {
    if (!prepared_ || samples == nullptr || frames == 0U) return;

    // The audio thread is the only writer of the engines' controls.
    if (current_ == Engine::ConsumerEnhancer && phase_ != Phase::FadeOut) {
        enhancer_.setMacroPercent(sliderPercent_.load(std::memory_order_relaxed));
    }

    float dry[2U * kChunk];
    std::size_t done = 0U;
    while (done < frames) {
        float* ptr = samples + 2U * done;
        const std::size_t left = frames - done;

        switch (phase_) {
        case Phase::Steady: {
            const Engine req = fromByte(requested_.load(std::memory_order_relaxed));
            if (req == current_) {
                run(current_, ptr, left);   // the one active engine (or nothing when Off)
                done = frames;
            } else if (current_ == Engine::Off) {
                begin(req);
                current_ = req;
                publish();
                phase_ = Phase::FadeIn;
                fadeLeft_ = xfadeSamples_;
            } else if (req == Engine::Off) {
                phase_ = Phase::FadeOut;
                fadeLeft_ = xfadeSamples_;
            } else {
                phase_ = Phase::DuckOut;
                fadeLeft_ = duckSamples_;
            }
            break;
        }

        case Phase::DuckOut: {
            const std::size_t n = std::min(fadeLeft_, left);
            run(current_, ptr, n);
            for (std::size_t k = 0; k < n; ++k) {
                const float g = rampDown(duckSamples_ - fadeLeft_ + k + 1U, duckSamples_);
                ptr[2U * k] *= g;
                ptr[2U * k + 1U] *= g;
            }
            fadeLeft_ -= n;
            done += n;
            if (fadeLeft_ == 0U) {
                // Silent: swap engines. Never both in the path at once.
                const Engine next = fromByte(requested_.load(std::memory_order_relaxed));
                if (next != current_) {
                    end(current_);
                    current_ = next;
                    begin(next);
                    publish();
                }
                phase_ = Phase::DuckIn;
                fadeLeft_ = duckInSamples_;
            }
            break;
        }

        case Phase::DuckIn: {
            const std::size_t n = std::min(fadeLeft_, left);
            run(current_, ptr, n);
            for (std::size_t k = 0; k < n; ++k) {
                const float r = rampUp(duckInSamples_ - fadeLeft_ + k + 1U, duckInSamples_);
                const float g = r * r;   // slow start: the new engine settles before it is heard
                ptr[2U * k] *= g;
                ptr[2U * k + 1U] *= g;
            }
            fadeLeft_ -= n;
            done += n;
            if (fadeLeft_ == 0U) phase_ = Phase::Steady;
            break;
        }

        case Phase::FadeIn:
        case Phase::FadeOut: {
            const bool fadingIn = phase_ == Phase::FadeIn;
            const std::size_t n = std::min({fadeLeft_, left, kChunk});
            std::copy(ptr, ptr + 2U * n, dry);
            run(current_, ptr, n);
            for (std::size_t k = 0; k < n; ++k) {
                const std::size_t p = xfadeSamples_ - fadeLeft_ + k + 1U;
                const float w = fadingIn ? rampUp(p, xfadeSamples_) : rampDown(p, xfadeSamples_);
                ptr[2U * k] = w * ptr[2U * k] + (1.0f - w) * dry[2U * k];
                ptr[2U * k + 1U] = w * ptr[2U * k + 1U] + (1.0f - w) * dry[2U * k + 1U];
            }
            fadeLeft_ -= n;
            done += n;
            if (fadeLeft_ == 0U) {
                if (!fadingIn) {
                    end(current_);
                    current_ = Engine::Off;
                    publish();
                }
                phase_ = Phase::Steady;
            }
            break;
        }
        }
    }
}

} // namespace stereo_dsp
