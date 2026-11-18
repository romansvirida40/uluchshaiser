// Offline DSP verification for Uluchshaizer.
// Builds a synthetic singing voice (band-limited glottal source + three formant resonators,
// vibrato, out-of-tune notes, consonant noise bursts), runs it through the real processing chain
// and measures the result with an independent high-resolution pitch analyser.
//
// NOTE: synthetic voice != real vocal recording. This proves the algorithms do what they claim;
// listening tests on real vocals are still required.

#include "../Source/dsp/VocalProcessor.h"
#include "../Source/dsp/Scales.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <random>

using namespace ulz;
static const double SR = 48000.0;
static const double PI = 3.14159265358979323846;
static int failures = 0;

#define CHECK(cond, ...) do { if (cond) { std::printf ("  PASS  "); } else { std::printf ("  FAIL  "); ++failures; } std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

// ---------------------------------------------------------------- synthetic voice
struct Seg { double startMidi, endMidi, dur; double vibDepthCents; bool consonantAfter; };

struct Reson { double b0, a1, a2, y1 = 0, y2 = 0;
    Reson (double f, double bw) { const double r = std::exp (-PI * bw / SR); a1 = -2 * r * std::cos (2 * PI * f / SR); a2 = r * r; b0 = 1 - r; }
    double p (double x) { const double y = b0 * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; } };

static std::vector<float> makeVoice (const std::vector<Seg>& segs, std::vector<double>& truthMidi, double f1 = 700, double f2 = 1220, double f3 = 2600)
{
    std::vector<float> out;
    std::mt19937 rng (1234);
    std::normal_distribution<double> nd (0.0, 1.0);
    Reson r1 (f1, 90), r2 (f2, 110), r3 (f3, 160);
    double phase = 0.0, vibPhase = 0.0;
    for (const auto& s : segs)
    {
        const int n = (int) (s.dur * SR);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / n;
            // glide over first 12 % of the note, like a singer's transition
            const double g = std::min (1.0, t / 0.12);
            const double ease = g * g * (3 - 2 * g);
            double midi = s.startMidi + (s.endMidi - s.startMidi) * ease;
            const double vibOn = std::min (1.0, std::max (0.0, (t - 0.3) / 0.2));
            vibPhase += 2 * PI * 5.5 / SR;
            midi += vibOn * s.vibDepthCents / 100.0 * std::sin (vibPhase);
            truthMidi.push_back (midi);
            const double f0 = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
            phase += 2 * PI * f0 / SR;
            if (phase > 2 * PI * 1000) phase -= 2 * PI * 1000;
            double src = 0;
            const int K = (int) (0.45 * SR / f0);
            for (int k = 1; k <= K; ++k) src += std::sin (k * phase) / (double) k;
            double env = std::min (1.0, std::min (i / (0.02 * SR), (n - i) / (0.02 * SR)));
            double y = r1.p (src) * 1.0 + r2.p (src) * 0.6 + r3.p (src) * 0.3;
            out.push_back ((float) (0.25 * env * y));
        }
        if (s.consonantAfter)
        {
            const int nc = (int) (0.06 * SR);
            Reson hs (5500, 2500);
            for (int i = 0; i < nc; ++i) { out.push_back ((float) (0.05 * hs.p (nd (rng)))); truthMidi.push_back (0); }
        }
    }
    // normalise to -6 dBFS peak
    float pk = 0; for (float v : out) pk = std::max (pk, std::fabs (v));
    for (float& v : out) v *= 0.5f / pk;
    return out;
}

// ---------------------------------------------------------------- independent analyser
// Full-rate YIN with a long window (offline quality, no decimation) — separate from the plugin's detector.
static double analysePitch (const std::vector<float>& x, size_t centre, double minHz = 70, double maxHz = 1200)
{
    const int tauMax = (int) (SR / minHz), tauMin = (int) (SR / maxHz), W = 1200;
    if (centre < (size_t) (W / 2 + 4) || centre + W / 2 + tauMax + 4 >= x.size()) return 0;
    const size_t st = centre - W / 2;
    std::vector<double> d (tauMax + 1), c (tauMax + 1);
    double run = 0; c[0] = 1;
    double e = 0; for (int j = 0; j < W; ++j) e += x[st + j] * x[st + j];
    if (e / W < 1e-6) return 0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0;
        for (int j = 0; j < W; ++j) { const double v = x[st + j] - x[st + j + tau]; s += v * v; }
        d[tau] = s; run += s; c[tau] = s * tau / run;
    }
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
        if (c[tau] < 0.1) { while (tau + 1 <= tauMax && c[tau + 1] < c[tau]) ++tau; best = tau; break; }
    if (best < 0 || c[best] > 0.2) return 0;
    double tb = best;
    if (best > 1 && best < tauMax) { const double den = d[best - 1] - 2 * d[best] + d[best + 1]; if (den > 0) tb += 0.5 * (d[best - 1] - d[best + 1]) / den; }
    return hzToMidi ((float) (SR / tb));
}

static double nearestScaleDist (double midi, uint16_t mask)
{
    double best = 99;
    for (int n = (int) midi - 7; n <= (int) midi + 7; ++n)
        if ((mask >> (((n % 12) + 12) % 12)) & 1) best = std::min (best, std::fabs (midi - n));
    return best;
}

struct Stats { double median = 0, p90 = 0; int n = 0; };
static Stats stats (std::vector<double> v)
{
    Stats s; s.n = (int) v.size(); if (v.empty()) return s;
    std::sort (v.begin(), v.end());
    s.median = v[v.size() / 2]; s.p90 = v[(size_t) (v.size() * 0.9)];
    return s;
}

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>* R = nullptr)
{
    FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return;
    const int ch = R ? 2 : 1; const uint32_t n = (uint32_t) L.size();
    const uint32_t dataBytes = n * ch * 2, sr = (uint32_t) SR, br = sr * ch * 2;
    const uint16_t ba = (uint16_t) (ch * 2), bits = 16, fmt = 1, chs = (uint16_t) ch;
    const uint32_t riff = 36 + dataBytes, fsz = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fsz, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&chs, 2, 1, f);
    std::fwrite (&sr, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
        for (int c = 0; c < ch; ++c)
        {
            float v = c == 0 ? L[i] : (*R)[i];
            int16_t s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f);
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
}

struct RunResult { std::vector<float> L, R; int latency; double secondsCpu; };

static RunResult run (const std::vector<float>& x, const VocalSettings& s, bool stereo = false, int block = 512)
{
    VocalProcessor p; p.prepare (SR);
    RunResult r; r.L = x; r.R = x;
    // first call configures; latency becomes valid after the first process()
    std::vector<float> l (block), rr (block);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < x.size(); i += block)
    {
        const int n = (int) std::min<size_t> (block, x.size() - i);
        float* chans[2] = { r.L.data() + i, r.R.data() + i };
        p.process (chans, stereo ? 2 : 1, n, s);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    r.secondsCpu = std::chrono::duration<double> (t1 - t0).count();
    r.latency = p.getLatency();
    if (! stereo) r.R = r.L;
    return r;
}

// deviation from scale of output, on frames where the *input* is a stable sung note
static Stats measureScaleDeviation (const std::vector<float>& y, int latency, const std::vector<double>& truth, uint16_t mask, double offsetSt = 0)
{
    std::vector<double> dev;
    for (size_t c = 4000; c + 4000 < truth.size(); c += 480)
    {
        if (truth[c] <= 0 || truth[c - 2400] <= 0 || truth[c + 2400] <= 0) continue;   // skip near consonants
        const double m = analysePitch (y, c + latency);
        if (m <= 0) continue;
        dev.push_back (100.0 * nearestScaleDist (m - offsetSt, mask));
    }
    return stats (dev);
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const uint16_t cMajor = scaleMask (0, ScaleType::Major);

    // Out-of-tune melody in C major (A3..A4 range, alto/tenor), with vibrato and consonants
    std::vector<Seg> melody {
        { 64.35, 64.35, 0.9, 0,  true  },  // E4 +35c
        { 64.35, 67.42, 1.0, 30, false },  // glide to G4 -58c? (67.42 = G4 +42c)
        { 67.42, 69.27, 1.2, 45, true  },  // A4 +27c with vibrato
        { 66.0,  65.62, 1.0, 20, true  },  // F#4 sliding to F4 +38... (off-scale area)
        { 62.3,  62.3,  1.2, 0,  false },  // D4 +30c
        { 60.0,  59.7,  1.0, 35, true  },  // C4 -> B3 +70c region
    };
    std::vector<double> truth;
    const auto voice = makeVoice (melody, truth);
    writeWav (outDir + "/00_input_synthetic_voice.wav", voice);

    std::printf ("Synthetic voice: %.2f s\n", voice.size() / SR);
    {
        std::vector<double> dev;
        for (size_t c = 4000; c + 4000 < truth.size(); c += 480)
            if (truth[c] > 0 && truth[c - 2400] > 0 && truth[c + 2400] > 0) { const double m = analysePitch (voice, c); if (m > 0) dev.push_back (100.0 * nearestScaleDist (m, cMajor)); }
        auto s = stats (dev);
        std::printf ("Input deviation from C major: median %.1f c, p90 %.1f c (n=%d)\n\n", s.median, s.p90, s.n);
    }

    // 1. Hard tune
    std::printf ("[1] Retune Speed 100, Amount 100 %% (hard tune), C major\n");
    {
        VocalSettings s; s.retuneSpeed = 100; s.amount = 1; s.sustain = 0; s.flexTune = 0; s.noteMask = cMajor;
        auto r = run (voice, s);
        auto st = measureScaleDeviation (r.L, r.latency, truth, cMajor);
        std::printf ("        latency %d samples (%.1f ms), CPU %.3f s for %.1f s audio (%.1f%% of realtime)\n",
                     r.latency, 1000.0 * r.latency / SR, r.secondsCpu, voice.size() / SR, 100 * r.secondsCpu / (voice.size() / SR));
        CHECK (st.median < 6.0 && st.p90 < 15.0, "output median %.1f c, p90 %.1f c from scale (n=%d)", st.median, st.p90, st.n);
        writeWav (outDir + "/01_hardtune_speed100.wav", r.L);
    }
    // 2. Medium speed: still in tune on sustained notes, vibrato partly kept
    std::printf ("[2] Retune Speed 32.7 (default)\n");
    {
        VocalSettings s; s.retuneSpeed = 32.7f; s.amount = 1; s.sustain = 0; s.noteMask = cMajor;
        auto r = run (voice, s);
        auto st = measureScaleDeviation (r.L, r.latency, truth, cMajor);
        CHECK (st.median < 15.0, "median %.1f c, p90 %.1f c", st.median, st.p90);
        writeWav (outDir + "/02_speed_32_7.wav", r.L);
    }
    // 3. Amount 0 => no correction (pitch preserved)
    std::printf ("[3] Amount 0 => transparent pitch\n");
    {
        VocalSettings s; s.amount = 0; s.noteMask = cMajor;
        auto r = run (voice, s);
        std::vector<double> err;
        for (size_t c = 4000; c + 4000 < truth.size(); c += 480)
        {
            if (truth[c] <= 0 || truth[c - 2400] <= 0 || truth[c + 2400] <= 0) continue;
            const double a = analysePitch (voice, c), b = analysePitch (r.L, c + r.latency);
            if (a > 0 && b > 0) err.push_back (100 * std::fabs (a - b));
        }
        auto st = stats (err);
        CHECK (st.median < 3.0 && st.p90 < 8.0, "|out - in| median %.1f c, p90 %.1f c", st.median, st.p90);
        // latency alignment: cross-correlation peak near reported latency
        double best = -1; int bestLag = 0;
        for (int lag = r.latency - 600; lag <= r.latency + 600; ++lag)
        {
            double s2 = 0; for (size_t i = 20000; i < 120000; ++i) s2 += voice[i] * r.L[i + lag];
            if (s2 > best) { best = s2; bestLag = lag; }
        }
        CHECK (std::abs (bestLag - r.latency) < 300, "output aligned with reported latency (xcorr peak at %d, reported %d)", bestLag, r.latency);
    }
    // 4. Manual Pitch +3 st
    std::printf ("[4] Pitch +3.0 st (no correction)\n");
    {
        VocalSettings s; s.amount = 0; s.pitchSt = 3.0f;
        auto r = run (voice, s);
        std::vector<double> err;
        for (size_t c = 8000; c + 4000 < truth.size(); c += 480)
        {
            if (truth[c] <= 0 || truth[c - 2400] <= 0 || truth[c + 2400] <= 0) continue;
            const double a = analysePitch (voice, c), b = analysePitch (r.L, c + r.latency);
            if (a > 0 && b > 0) err.push_back (100 * std::fabs ((b - a) - 3.0));
        }
        auto st = stats (err);
        CHECK (st.median < 5.0, "shift error median %.1f c, p90 %.1f c", st.median, st.p90);
        writeWav (outDir + "/04_pitch_plus3.wav", r.L);
    }
    // 5. Formant +100 % : f0 unchanged, spectral envelope moves up
    std::printf ("[5] Formant +100 %% (pitch must stay, timbre must move)\n");
    {
        std::vector<Seg> held { { 57.0, 57.0, 2.0, 0, false } };   // A3 steady
        std::vector<double> tr; auto v = makeVoice (held, tr);
        auto centroid = [] (const std::vector<float>& y, size_t from) {
            // average magnitude spectrum (DFT on 4096 frame, 200..4000 Hz) -> spectral centroid
            const int N = 4096; double num = 0, den = 0;
            for (int k = (int) (200.0 * N / SR); k < (int) (4000.0 * N / SR); ++k)
            {
                double re = 0, im = 0;
                for (int n = 0; n < N; ++n) { const double w = 0.5 - 0.5 * std::cos (2 * PI * n / N); re += w * y[from + n] * std::cos (2 * PI * k * n / N); im -= w * y[from + n] * std::sin (2 * PI * k * n / N); }
                const double m = std::sqrt (re * re + im * im); num += m * k * SR / N; den += m;
            }
            return num / den;
        };
        VocalSettings s0; s0.amount = 0;
        VocalSettings s1 = s0; s1.formantPct = 100;
        VocalSettings s2 = s0; s2.formantPct = -100;
        auto r0 = run (v, s0), r1 = run (v, s1), r2 = run (v, s2);
        const size_t at = 40000;
        const double c0 = centroid (r0.L, at + r0.latency), c1 = centroid (r1.L, at + r1.latency), c2 = centroid (r2.L, at + r2.latency);
        const double p0 = analysePitch (r0.L, at + r0.latency), p1 = analysePitch (r1.L, at + r1.latency), p2 = analysePitch (r2.L, at + r2.latency);
        CHECK (c1 > c0 * 1.12 && c2 < c0 * 0.9, "spectral centroid neutral %.0f Hz, +100%% %.0f Hz, -100%% %.0f Hz", c0, c1, c2);
        CHECK (std::fabs (p1 - p0) < 0.08 && std::fabs (p2 - p0) < 0.08, "f0 unchanged: %.2f / %.2f / %.2f (MIDI)", p0, p1, p2);
        writeWav (outDir + "/05a_formant_neutral.wav", r0.L);
        writeWav (outDir + "/05b_formant_plus100.wav", r1.L);
        writeWav (outDir + "/05c_formant_minus100.wav", r2.L);
    }
    // 6. Disabled notes: only C, E, G allowed
    std::printf ("[6] Custom notes: only C E G enabled\n");
    {
        const uint16_t ceg = (1 << 0) | (1 << 4) | (1 << 7);
        VocalSettings s; s.retuneSpeed = 100; s.amount = 1; s.noteMask = ceg;
        auto r = run (voice, s);
        auto st = measureScaleDeviation (r.L, r.latency, truth, ceg);
        CHECK (st.median < 8.0, "output median %.1f c, p90 %.1f c from {C,E,G}", st.median, st.p90);
    }
    // 7. Minor key with flats: Eb minor naming, mask
    std::printf ("[7] Scales and spelling\n");
    {
        CHECK (keyName (3, ScaleType::Minor) == "Eb Minor", "root 3 minor -> %s", keyName (3, ScaleType::Minor).c_str());
        CHECK (keyName (8, ScaleType::Major) == "Ab Major", "root 8 major -> %s", keyName (8, ScaleType::Major).c_str());
        CHECK (keyName (6, ScaleType::Minor) == "F# Minor", "root 6 minor -> %s", keyName (6, ScaleType::Minor).c_str());
        CHECK (scaleMask (9, ScaleType::Minor) == scaleMask (0, ScaleType::Major), "A minor has the same notes as C major");
        CHECK (scaleMask (0, ScaleType::Chromatic) == 0x0FFF, "chromatic = all 12");
    }
    // 8. Doubles: stereo width
    std::printf ("[8] Doubles Dry/Wet + Width\n");
    {
        VocalSettings s; s.amount = 1; s.retuneSpeed = 60; s.noteMask = cMajor; s.doubleMix = 0.5f; s.doublesWidth = 1.0f;
        auto rw = run (voice, s, true);
        s.doublesWidth = 0.0f; auto rn = run (voice, s, true);
        s.doubleMix = 0.0f; auto r0 = run (voice, s, true);
        auto corr = [] (const RunResult& r) { double a = 0, b = 0, c = 0; for (size_t i = 0; i < r.L.size(); ++i) { a += r.L[i] * r.R[i]; b += r.L[i] * r.L[i]; c += r.R[i] * r.R[i]; } return a / std::sqrt (b * c + 1e-12); };
        const double cw = corr (rw), cn = corr (rn), c0 = corr (r0);
        CHECK (cw < 0.9 && cn > 0.999 && c0 > 0.999, "L/R correlation: width 100%% = %.3f, width 0%% = %.4f, doubles off = %.4f", cw, cn, c0);
        double eD = 0, e0 = 0; for (size_t i = 0; i < rw.L.size(); ++i) { eD += rw.L[i] * rw.L[i]; e0 += r0.L[i] * r0.L[i]; }
        CHECK (eD > e0 * 1.1, "doubles add energy (%.2f dB)", 10 * std::log10 (eD / e0));
        writeWav (outDir + "/08_doubles_wide.wav", rw.L, &rw.R);
    }
    // 9. Low latency + input types
    std::printf ("[9] Latency per Input Type (48 kHz)\n");
    {
        const char* names[] = { "Soprano", "Alto/Tenor", "Low Male", "Instrument", "Bass Instrument" };
        for (int t = 0; t < 5; ++t)
        {
            const int a = VocalProcessor::latencyFor (SR, (InputType) t, false), b = VocalProcessor::latencyFor (SR, (InputType) t, true);
            std::printf ("        %-16s normal %5d (%5.1f ms)   low latency %5d (%5.1f ms)\n", names[t], a, 1000.0 * a / SR, b, 1000.0 * b / SR);
        }
        VocalSettings s; s.retuneSpeed = 100; s.noteMask = cMajor; s.lowLatency = true;
        auto r = run (voice, s);
        auto st = measureScaleDeviation (r.L, r.latency, truth, cMajor);
        CHECK (r.latency == VocalProcessor::latencyFor (SR, InputType::AltoTenor, true), "low-latency mode reports %d samples", r.latency);
        CHECK (st.median < 8.0, "low-latency hard tune: median %.1f c, p90 %.1f c", st.median, st.p90);
        writeWav (outDir + "/09_lowlatency_hardtune.wav", r.L);
    }
    // 10. Low male voice with Low Male input type
    std::printf ("[10] Low male voice (G2..D3) with Input Type = Low Male\n");
    {
        std::vector<Seg> low { { 43.3, 43.3, 1.0, 0, true }, { 47.4, 47.4, 1.0, 30, false }, { 50.35, 50.35, 1.0, 0, false } };
        std::vector<double> tr; auto v = makeVoice (low, tr, 600, 1000, 2400);
        VocalSettings s; s.retuneSpeed = 100; s.noteMask = cMajor; s.inputType = InputType::LowMale;
        auto r = run (v, s);
        std::vector<double> dev;
        for (size_t c = 6000; c + 6000 < tr.size(); c += 480)
        {
            if (tr[c] <= 0 || tr[c - 2400] <= 0 || tr[c + 2400] <= 0) continue;
            const double m = analysePitch (r.L, c + r.latency, 60, 600);
            if (m > 0) dev.push_back (100 * nearestScaleDist (m, cMajor));
        }
        auto st = stats (dev);
        CHECK (st.median < 8.0, "median %.1f c, p90 %.1f c (n=%d)", st.median, st.p90, st.n);
    }
    // 11. Silence / unvoiced robustness: no NaN, no blow-up, output bounded
    std::printf ("[11] Robustness\n");
    {
        std::vector<float> noise (48000 * 2);
        std::mt19937 rng (7); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        for (size_t i = 0; i < noise.size(); ++i) noise[i] = i < 48000 ? 0.0f : u (rng);
        VocalSettings s; s.retuneSpeed = 100; s.pitchSt = -12; s.formantPct = 100; s.doubleMix = 1; s.volume = 1;
        auto r = run (noise, s, true, 37);
        bool finite = true; float pk = 0;
        for (size_t i = 0; i < r.L.size(); ++i) { finite &= std::isfinite (r.L[i]) && std::isfinite (r.R[i]); pk = std::max (pk, std::max (std::fabs (r.L[i]), std::fabs (r.R[i]))); }
        CHECK (finite && pk <= 1.0f, "finite output, peak %.3f <= 1.0 (soft clip), odd block size 37", pk);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
