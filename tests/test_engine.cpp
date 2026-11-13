// Onirique BrumeBox — test hors ligne du moteur, sans hôte ni JUCE.
// Usage : test_engine [dossier]   (écrit aussi des .wav d'écoute dans ce dossier)
#include "dsp/BrumeEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

/** Une note tenue (la2) et une frappe bruitée toutes les demi-secondes. */
std::vector<float> makeSignal (int numSamples, std::vector<int>& hitStarts, float tone = 0.15f)
{
    std::vector<float> s ((size_t) numSamples);
    uint32_t rng = 12345;
    for (int i = 0; i < numSamples; ++i)
        s[(size_t) i] = tone * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / kSr);

    const int every = (int) (kSr * 0.5);
    for (int start = (int) (kSr * 0.25); start < numSamples; start += every)
    {
        hitStarts.push_back (start);
        for (int i = 0; i < (int) (kSr * 0.03) && start + i < numSamples; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            const float noise = (float) (rng >> 8) / 8388608.0f - 1.0f;
            s[(size_t) (start + i)] += 0.6f * noise * std::exp (-(float) i / (float) (kSr * 0.004));
        }
    }
    return s;
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 2 * 2, sr = (uint32_t) kSr, byteRate = sr * 4;
    const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
    const uint32_t riff = 36 + dataBytes, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&sr, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
        for (const auto* c : { &l, &r })
        {
            const float v = std::max (-1.0f, std::min (1.0f, (*c)[i]));
            const int16_t s = (int16_t) std::lrint (v * 32767.0f);
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
}

struct Result
{
    std::vector<float> l, r;
    std::vector<float> shareAtHit, shareBetween;
};

Result run (const bb::Settings& s, const std::vector<float>& in, const std::vector<int>& hits)
{
    bb::BrumeEngine e;
    e.prepare (kSr, 2);
    e.setSettings (s);

    Result res { in, in, {}, {} };
    const int block = 256;
    for (int pos = 0; pos < (int) in.size(); pos += block)
    {
        const int n = std::min (block, (int) in.size() - pos);
        float* io[2] = { res.l.data() + pos, res.r.data() + pos };
        e.process (io, 2, n);

        // La trame qui vient d'être analysée couvre les fftSize derniers échantillons.
        const int end = pos + n;
        for (int h : hits)
        {
            if (end > h + 600 && end <= h + 600 + block) res.shareAtHit.push_back (e.lastTransientShare());
            if (end > h + 12000 && end <= h + 12000 + block) res.shareBetween.push_back (e.lastTransientShare());
        }
    }
    return res;
}

float mean (const std::vector<float>& v)
{
    float s = 0; for (float x : v) s += x;
    return v.empty() ? 0.0f : s / (float) v.size();
}
} // namespace

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<int> hits;
    const auto in = makeSignal ((int) (kSr * 4.0), hits);

    bb::BrumeEngine probe;
    probe.prepare (kSr, 2);
    const int latency = probe.latency();

    // 1. Neutre : attaques + reste = le signal, retardé de la latence.
    {
        bb::Settings s;
        s.punchDb = 0; s.reso = 0; s.haze = 0; s.blur = 0;
        const auto out = run (s, in, hits);
        double err = 0, ref = 0;
        for (size_t i = (size_t) latency * 2; i < in.size(); ++i)
        {
            const double d = out.l[i] - in[i - (size_t) latency];
            err += d * d; ref += (double) in[i - (size_t) latency] * in[i - (size_t) latency];
        }
        const double snr = 10.0 * std::log10 (ref / std::max (err, 1e-30));
        std::printf ("      reconstruction : %.1f dB\n", snr);
        check (snr > 80.0, "reglages neutres : le son ressort intact (> 80 dB)");

        std::printf ("      part d'attaque : %.3f sur les frappes, %.3f entre elles\n",
                     mean (out.shareAtHit), mean (out.shareBetween));
        check (mean (out.shareAtHit) > 4.0f * mean (out.shareBetween) + 0.02f,
               "les frappes sont detectees, la note tenue non");
    }

    // 2. Attaques coupées : moins d'énergie autour des frappes.
    {
        bb::Settings s;
        s.punchDb = -24; s.reso = 0; s.haze = 0; s.blur = 0;
        // Les frappes seules : avec la note, elle domine l'energie mesuree.
        std::vector<int> h2;
        const auto clicks = makeSignal ((int) (kSr * 4.0), h2, 0.0f);
        const auto& in = clicks;
        const auto out = run (s, clicks, h2);
        double eIn = 0, eOut = 0;
        for (int h : h2)
            for (int i = 0; i < 1200 && h + latency + i < (int) in.size(); ++i)
            {
                eIn += (double) in[(size_t) (h + i)] * in[(size_t) (h + i)];
                eOut += (double) out.l[(size_t) (h + latency + i)] * out.l[(size_t) (h + latency + i)];
            }
        const double db = 10.0 * std::log10 (eOut / eIn);
        std::printf ("      frappes : %.1f dB\n", db);
        check (db < -6.0, "punch -24 dB : les frappes baissent d'au moins 6 dB");
    }

    // 3. Le réglage onirique : stable, borné, et une traîne après la fin du son.
    {
        bb::Settings s;
        s.punchDb = -6; s.reso = 0.8f; s.decayS = 4; s.haze = 0.6f; s.hazeS = 6; s.blur = 0.7f;
        auto silent = in;
        silent.resize (silent.size() + (size_t) (kSr * 3), 0.0f);
        const auto out = run (s, silent, hits);

        bool finite = true; float peak = 0;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
            peak = std::max (peak, std::max (std::abs (out.l[i]), std::abs (out.r[i])));
        }
        double tail = 0;
        const size_t tailStart = in.size() + (size_t) latency + (size_t) (kSr * 0.5);
        for (size_t i = tailStart; i < tailStart + (size_t) kSr; ++i) tail += (double) out.l[i] * out.l[i];
        const double tailDb = 10.0 * std::log10 (tail / kSr + 1e-30);

        double diff = 0, sum = 0;
        for (size_t i = (size_t) latency; i < out.l.size(); ++i)
        {
            diff += std::abs (out.l[i] - out.r[i]); sum += std::abs (out.l[i]) + std::abs (out.r[i]);
        }
        std::printf ("      crete %.2f, traine %.1f dB RMS, ecart G/D %.1f %%\n", peak, tailDb, 100.0 * diff / sum);
        check (finite, "aucun NaN ni infini");
        check (peak < 1.5f, "crete sous 1.5");
        check (tailDb > -70.0, "une traine reste apres la fin du son");
        check (diff / sum > 0.01, "la stereo s'ouvre (G et D differents)");

        writeWav (dir + "/brumebox_reve.wav", out.l, out.r);
        auto dryL = in; dryL.resize (out.l.size(), 0.0f);
        writeWav (dir + "/brumebox_sec.wav", dryL, dryL);
    }

    std::printf (failures == 0 ? "\ntout passe\n" : "\n%d echec(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
