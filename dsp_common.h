// Shared DSP building blocks for the NOVA engine (filters, delays, FFT, room).
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nova {

constexpr double kPi = 3.14159265358979323846;

// ----------------------------------------------------------------------------
// small helpers
// ----------------------------------------------------------------------------
inline float fclamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Rational tanh approximation, accurate for |x| < 3, saturating outside.
inline float fastTanh(float x) {
    x = fclamp(x, -3.f, 3.f);
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float flushDenorm(float v) { return (std::fabs(v) < 1e-20f) ? 0.f : v; }

inline double timeCoef(double seconds, double fs) {
    return std::exp(-1.0 / std::max(1e-6, seconds * fs));
}

// ----------------------------------------------------------------------------
// Biquad (RBJ cookbook), double precision state
// ----------------------------------------------------------------------------
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline float process(float x) {
        const double xd = x;
        const double y = b0 * xd + z1;
        z1 = b1 * xd - a1 * y + z2;
        z2 = b2 * xd - a2 * y;
        return (float)y;
    }
    void reset() { z1 = z2 = 0; }
    void copyCoefs(const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void lowpass(double fs, double f, double q = 0.70710678) {
        f = std::min(f, fs * 0.45);
        const double w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(double fs, double f, double q = 0.70710678) {
        f = std::min(f, fs * 0.45);
        const double w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peaking(double fs, double f, double q, double dB) {
        f = std::min(f, fs * 0.45);
        const double A = std::pow(10.0, dB / 40.0);
        const double w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowshelf(double fs, double f, double dB) {
        f = std::min(f, fs * 0.45);
        const double A = std::pow(10.0, dB / 40.0);
        const double w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
        const double beta = 2 * std::sqrt(A) * (s / 2 * 1.41421356);
        set(A * ((A + 1) - (A - 1) * c + beta), 2 * A * ((A - 1) - (A + 1) * c),
            A * ((A + 1) - (A - 1) * c - beta), (A + 1) + (A - 1) * c + beta,
            -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - beta);
    }
    void highshelf(double fs, double f, double dB) {
        f = std::min(f, fs * 0.45);
        const double A = std::pow(10.0, dB / 40.0);
        const double w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
        const double beta = 2 * std::sqrt(A) * (s / 2 * 1.41421356);
        set(A * ((A + 1) + (A - 1) * c + beta), -2 * A * ((A - 1) + (A + 1) * c),
            A * ((A + 1) + (A - 1) * c - beta), (A + 1) - (A - 1) * c + beta,
            2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - beta);
    }
};

// A stereo pair of identical biquads.
struct Biquad2 {
    Biquad l, r;
    void copyCoefs(const Biquad& c) { l.copyCoefs(c); r.copyCoefs(c); }
    void reset() { l.reset(); r.reset(); }
};

// Asymmetric attack/release envelope follower on rectified input.
struct Follower {
    double env = 0, a = 0, r = 0;
    void setTimes(double attS, double relS, double fs) { a = timeCoef(attS, fs); r = timeCoef(relS, fs); }
    inline float run(float absx) {
        const double x = absx;
        env = (x > env) ? a * env + (1 - a) * x : r * env + (1 - r) * x;
        return (float)env;
    }
};

// One-pole smoother toward a target (used on gains).
struct Smooth {
    double v = 1.0;
    inline float run(float target, double aCoef, double rCoef) {
        const double c = (target > v) ? aCoef : rCoef;
        v = c * v + (1 - c) * target;
        return (float)v;
    }
};

struct DCBlock {
    double x1 = 0, y1 = 0;
    inline float process(float x) {
        const double y = x - x1 + 0.9975 * y1;
        x1 = x; y1 = y;
        return (float)y;
    }
    void reset() { x1 = y1 = 0; }
};

// Plain circular delay line.
struct Delay {
    std::vector<float> buf;
    int idx = 0;
    void init(int maxSamples) { buf.assign((size_t)std::max(2, maxSamples + 2), 0.f); idx = 0; }
    inline void write(float x) { buf[(size_t)idx] = x; if (++idx >= (int)buf.size()) idx = 0; }
    // delay in samples (integer), 1 = previous sample
    inline float read(int d) const {
        int p = idx - d;
        while (p < 0) p += (int)buf.size();
        return buf[(size_t)p];
    }
    // fractional read (linear)
    inline float readFrac(float d) const {
        const int di = (int)d;
        const float fr = d - (float)di;
        const float a = read(di), b = read(di + 1);
        return a + (b - a) * fr;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); idx = 0; }
};

// Schroeder all-pass (unity gain, scrambles phase -> decorrelation)
struct Allpass {
    std::vector<float> buf;
    int idx = 0;
    float g = 0.5f;
    void init(int len, float gain) { buf.assign((size_t)std::max(1, len), 0.f); idx = 0; g = gain; }
    inline float process(float x) {
        const float wd = buf[(size_t)idx];
        const float w = x + g * wd;
        buf[(size_t)idx] = w;
        if (++idx >= (int)buf.size()) idx = 0;
        return -g * w + wd;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); idx = 0; }
};

// Two-band transient shaper working on a detector signal.
struct TransientShaper {
    Follower fast, slow;
    Smooth gain;
    void init(double fs, double fastAtt, double fastRel, double slowAtt, double slowRel) {
        fast.setTimes(fastAtt, fastRel, fs);
        slow.setTimes(slowAtt, slowRel, fs);
        gain.v = 1.0;
    }
    // attackAmt: dB boost per dB of transient excess. sustainAmt: dB cut per dB of decay.
    inline float run(float detector, float attackAmt, float sustainAmt, float maxBoostDb,
                     float maxCutDb, double aCoef, double rCoef) {
        const float a = std::fabs(detector);
        const float f = fast.run(a), s = slow.run(a);
        float gDb = 0.f;
        if (f > 1e-4f || s > 1e-4f) {
            const float diffDb = 6.0206f * std::log2((f + 1e-5f) / (s + 1e-5f));
            gDb = (diffDb > 0.f) ? fclamp(attackAmt * diffDb, 0.f, maxBoostDb)
                                 : fclamp(sustainAmt * diffDb, -maxCutDb, 0.f);
        }
        const float target = std::exp2(gDb * 0.16609640f);
        return gain.run(target, aCoef, rCoef);
    }
};

// 4-line feedback delay network with modulated delays + damping.
// ----------------------------------------------------------------------------
// Small math helpers used by the scene stage
// ----------------------------------------------------------------------------
inline float smoothstep(float x, float a, float b) {
    float t = (x - a) / (b - a);
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return t * t * (3.f - 2.f * t);
}
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// ----------------------------------------------------------------------------
// Radix-2 complex FFT (float). Forward is unnormalised; inverse is unnormalised too
// (caller divides by N).
// ----------------------------------------------------------------------------
struct cf {
    float re = 0.f, im = 0.f;
};

class FFT {
public:
    void init(int n) {
        n_ = n;
        int lg = 0;
        while ((1 << lg) < n) lg++;
        rev_.assign((size_t)n, 0);
        for (int i = 0; i < n; i++) {
            int r = 0;
            for (int b = 0; b < lg; b++) if (i & (1 << b)) r |= 1 << (lg - 1 - b);
            rev_[(size_t)i] = r;
        }
        wr_.assign((size_t)n / 2, 0.f);
        wi_.assign((size_t)n / 2, 0.f);
        for (int i = 0; i < n / 2; i++) {
            const double a = -2.0 * kPi * (double)i / (double)n;
            wr_[(size_t)i] = (float)std::cos(a);
            wi_[(size_t)i] = (float)std::sin(a);
        }
    }
    int size() const { return n_; }

    // In-place transform of n_ complex values. inverse=true conjugates the twiddles.
    void run(cf* x, bool inverse) const {
        const int n = n_;
        for (int i = 0; i < n; i++) {
            const int j = rev_[(size_t)i];
            if (j > i) std::swap(x[i], x[j]);
        }
        const float sgn = inverse ? -1.f : 1.f;
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int k = 0; k < half; k++) {
                    const float wr = wr_[(size_t)(k * step)];
                    const float wi = sgn * wi_[(size_t)(k * step)];
                    cf& a = x[i + k];
                    cf& b = x[i + k + half];
                    const float tr = b.re * wr - b.im * wi;
                    const float ti = b.re * wi + b.im * wr;
                    b.re = a.re - tr; b.im = a.im - ti;
                    a.re += tr; a.im += ti;
                }
            }
        }
    }

private:
    int n_ = 0;
    std::vector<int> rev_;
    std::vector<float> wr_, wi_;
};

// ----------------------------------------------------------------------------
// 8-line feedback delay network with modulated delays and per-line damping.
// Stereo in, stereo out. RT60 and damping can change at run time.
// ----------------------------------------------------------------------------
struct FDN8 {
    static constexpr int N = 8;
    Delay d[N];
    float lp[N];
    float len[N];
    float g[N];
    float damp = 0.30f;
    double lfoPh[N], lfoInc[N];
    float modDepth = 0.f;
    float outL = 0.f, outR = 0.f;
    double fs = 48000.0;
    Allpass inAp[2][2];

    void init(double fs_, double rt60) {
        fs = fs_;
        const double base[N] = {1087.0, 1319.0, 1523.0, 1741.0, 1999.0, 2297.0, 2593.0, 2909.0};  // @48k
        const double rates[N] = {0.09, 0.13, 0.17, 0.21, 0.25, 0.29, 0.33, 0.37};
        for (int i = 0; i < N; i++) {
            len[i] = (float)(base[i] * fs / 48000.0);
            d[i].init((int)len[i] + 64);
            lp[i] = 0.f;
            lfoPh[i] = 0.7 * i;
            lfoInc[i] = 2 * kPi * rates[i] / fs;
        }
        modDepth = (float)(0.0005 * fs);
        const int ap[2][2] = {{142, 107}, {157, 113}};
        for (int c = 0; c < 2; c++) for (int s = 0; s < 2; s++) inAp[c][s].init(std::max(1, (int)std::lround(ap[c][s] * fs / 48000.0)), 0.6f);
        setRt60(rt60);
    }
    void setRt60(double rt60) {
        for (int i = 0; i < N; i++) g[i] = (float)std::pow(10.0, -3.0 * len[i] / (std::max(0.1, rt60) * fs));
    }
    void clear() {
        for (int i = 0; i < N; i++) { d[i].clear(); lp[i] = 0.f; }
        for (int c = 0; c < 2; c++) for (int s = 0; s < 2; s++) inAp[c][s].clear();
        outL = outR = 0.f;
    }
    inline void process(float inL, float inR) {
        inL = inAp[0][1].process(inAp[0][0].process(inL));
        inR = inAp[1][1].process(inAp[1][0].process(inR));
        float o[N];
        for (int i = 0; i < N; i++) {
            lfoPh[i] += lfoInc[i];
            if (lfoPh[i] > 2 * kPi) lfoPh[i] -= 2 * kPi;
            const float dl = len[i] + modDepth * (float)std::sin(lfoPh[i]);
            const float v = d[i].readFrac(dl);
            lp[i] = flushDenorm((1.f - damp) * v + damp * lp[i]);
            o[i] = lp[i] * g[i];
        }
        // 8x8 Hadamard (fast Walsh-Hadamard), scaled to be energy preserving
        float a[N];
        for (int i = 0; i < N; i++) a[i] = o[i];
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; j++) { const float x = a[j], y = a[j + h]; a[j] = x + y; a[j + h] = x - y; }
        const float sc = 0.35355339f;  // 1/sqrt(8)
        for (int i = 0; i < N; i++) {
            const float in = ((i & 1) ? inR : inL) * ((i & 2) ? -0.5f : 0.5f);
            d[i].write(flushDenorm(a[i] * sc + in));
        }
        float l = 0.f, r = 0.f;
        for (int i = 0; i < N; i += 2) { l += o[i] * ((i & 2) ? -1.f : 1.f); r += o[i + 1] * ((i & 2) ? 1.f : -1.f); }
        outL = l * 0.5f;
        outR = r * 0.5f;
    }
};

}  // namespace nova
