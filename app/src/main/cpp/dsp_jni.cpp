#include <jni.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

#include "engine_selector.hpp"
#include "peq_engine.h"

namespace {
constexpr int kChannels = 2;
constexpr int kMaxChunkFrames = 4096;
constexpr int kDrainMaxFrames = 8192;
constexpr float kInvInt16 = 1.0f / 32768.0f;
constexpr float kInt16Max = 32767.0f;

struct NativeProcessor {
    stereo_dsp::EngineSelector selector;
    peq::Engine parametricEq{16};
    std::vector<float> work;
    bool configured = false;

    NativeProcessor() : work(static_cast<std::size_t>(kMaxChunkFrames) * kChannels, 0.0f) {}
};

NativeProcessor* get(jlong handle) {
    return reinterpret_cast<NativeProcessor*>(static_cast<std::uintptr_t>(handle));
}

int16_t toPcm16(float value) {
    if (!std::isfinite(value)) value = 0.0f;
    value = std::max(-1.0f, std::min(0.9999695f, value));
    const float scaled = value * (value < 0.0f ? 32768.0f : kInt16Max);
    return static_cast<int16_t>(std::lrintf(scaled));
}

void setBand(NativeProcessor* p, jint index, jfloat freq, jfloat gain, jfloat q, jboolean enabled) {
    if (p == nullptr || index < 0 || index >= peq::kMaxBands) return;
    peq::BandParams band;
    band.type = peq::kPeaking;
    band.freq = std::max(20.0f, std::min(20000.0f, freq));
    band.gainDb = std::max(-12.0f, std::min(12.0f, gain));
    band.q = std::max(0.25f, std::min(8.0f, q));
    band.enabled = enabled == JNI_TRUE;
    p->parametricEq.setBand(index, band);
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeCreate(JNIEnv*, jclass) {
    auto* p = new (std::nothrow) NativeProcessor();
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(p));
}

extern "C" JNIEXPORT void JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeRelease(JNIEnv*, jclass, jlong handle) {
    delete get(handle);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeConfigure(
        JNIEnv*, jclass, jlong handle, jint sampleRate) {
    NativeProcessor* p = get(handle);
    if (p == nullptr || sampleRate < 8000 || sampleRate > 384000) return JNI_FALSE;

    stereo_dsp::EngineSelector::Config cfg;
    cfg.sampleRateHz = static_cast<float>(sampleRate);
    if (!p->selector.prepare(cfg)) return JNI_FALSE;

    p->parametricEq.setSampleRate(sampleRate);
    p->parametricEq.setPeqEnabled(true);
    p->parametricEq.setGraphicEqEnabled(false);
    p->parametricEq.setLimiterEnabled(true);
    p->parametricEq.setLimiterCeilingDb(-1.0f);
    p->parametricEq.setLimiterSafetyDb(0.1f);
    p->parametricEq.setLimiterLookaheadMs(2.0f);
    p->parametricEq.setLimiterReleaseMs(180.0f);
    p->parametricEq.setDitherEnabled(false);
    p->configured = true;
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeReset(
        JNIEnv*, jclass, jlong handle) {
    NativeProcessor* p = get(handle);
    if (p == nullptr) return;
    p->selector.reset();
    p->parametricEq.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeSetEngine(
        JNIEnv*, jclass, jlong handle, jint engine) {
    NativeProcessor* p = get(handle);
    if (p == nullptr) return;
    const auto selected = engine == 1 ? stereo_dsp::EngineSelector::Engine::ConsumerEnhancer
                         : engine == 2 ? stereo_dsp::EngineSelector::Engine::HaloBloom
                                       : stereo_dsp::EngineSelector::Engine::Off;
    p->selector.setEngine(selected);
}

extern "C" JNIEXPORT void JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeSetEnhancerPercent(
        JNIEnv*, jclass, jlong handle, jfloat percent) {
    NativeProcessor* p = get(handle);
    if (p != nullptr) p->selector.setEnhancerPercent(percent);
}

extern "C" JNIEXPORT void JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeSetPeqEnabled(
        JNIEnv*, jclass, jlong handle, jboolean enabled) {
    NativeProcessor* p = get(handle);
    if (p != nullptr) p->parametricEq.setPeqEnabled(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeSetBand(
        JNIEnv*, jclass, jlong handle, jint index, jfloat freq, jfloat gain,
        jfloat q, jboolean enabled) {
    setBand(get(handle), index, freq, gain, q, enabled);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeProcess(
        JNIEnv* env, jclass, jlong handle, jobject inputBuffer, jobject outputBuffer,
        jint inputBytes) {
    NativeProcessor* p = get(handle);
    if (p == nullptr || !p->configured || inputBuffer == nullptr || outputBuffer == nullptr) return 0;

    const auto* input = static_cast<const int16_t*>(env->GetDirectBufferAddress(inputBuffer));
    auto* output = static_cast<int16_t*>(env->GetDirectBufferAddress(outputBuffer));
    const jlong outputCapacity = env->GetDirectBufferCapacity(outputBuffer);
    if (input == nullptr || output == nullptr || outputCapacity <= 0 || inputBytes <= 0) return 0;

    const int inputFrames = inputBytes / (sizeof(int16_t) * kChannels);
    const int maxOutputFrames = static_cast<int>(outputCapacity / (sizeof(int16_t) * kChannels));
    int consumed = 0;
    int written = 0;

    while (consumed < inputFrames && written < maxOutputFrames) {
        const int chunkFrames = std::min(kMaxChunkFrames, inputFrames - consumed);
        float* audio = p->work.data();
        for (int frame = 0; frame < chunkFrames; ++frame) {
            const int inIndex = (consumed + frame) * kChannels;
            audio[frame * kChannels] = static_cast<float>(input[inIndex]) * kInvInt16;
            audio[frame * kChannels + 1] = static_cast<float>(input[inIndex + 1]) * kInvInt16;
        }

        p->selector.process(audio, static_cast<std::size_t>(chunkFrames));
        const int outputFrames = p->parametricEq.processFloat(audio, chunkFrames, kChannels);
        const int safeFrames = std::max(0, std::min(outputFrames, maxOutputFrames - written));
        for (int frame = 0; frame < safeFrames; ++frame) {
            output[(written + frame) * kChannels] = toPcm16(audio[frame * kChannels]);
            output[(written + frame) * kChannels + 1] = toPcm16(audio[frame * kChannels + 1]);
        }
        consumed += chunkFrames;
        written += safeFrames;
    }

    return written;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_bmwanje_luma_playback_DspAudioProcessor_nativeDrain(
        JNIEnv* env, jclass, jlong handle, jobject outputBuffer, jint maxFrames) {
    NativeProcessor* p = get(handle);
    if (p == nullptr || !p->configured || outputBuffer == nullptr) return 0;
    auto* output = static_cast<int16_t*>(env->GetDirectBufferAddress(outputBuffer));
    const jlong capacity = env->GetDirectBufferCapacity(outputBuffer);
    if (output == nullptr || capacity < sizeof(int16_t) * kChannels) return 0;

    const int capacityFrames = static_cast<int>(capacity / (sizeof(int16_t) * kChannels));
    const int requestedFrames = std::max(0, std::min({maxFrames, kDrainMaxFrames, capacityFrames}));
    if (requestedFrames == 0) return 0;

    const int produced = p->parametricEq.drainFloat(p->work.data(), requestedFrames, kChannels);
    const int safeFrames = std::max(0, std::min(produced, requestedFrames));
    for (int frame = 0; frame < safeFrames; ++frame) {
        output[frame * kChannels] = toPcm16(p->work[frame * kChannels]);
        output[frame * kChannels + 1] = toPcm16(p->work[frame * kChannels + 1]);
    }
    return safeFrames;
}
