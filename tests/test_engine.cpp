// Onirique Shaper — test hors ligne du moteur, sans hôte ni JUCE.
// Usage : test_engine [dossier]   (écrit aussi des .wav d'écoute dans ce dossier)
#include "dsp/ShaperEngine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
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

/** Une note tenue (la2, à gauche plus fort) et une frappe bruitée toutes les demi-secondes, au centre. */
struct Signal { std::vector<float> l, r; std::vector<int> hits; };

Signal makeSignal (double seconds, float tone = 0.15f)
{
    const int n = (int) (kSr * seconds);
    Signal s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n), {} };
    uint32_t rng = 12345;
    for (int i = 0; i < n; ++i)
    {
        const float t = tone * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / kSr);
        s.l[(size_t) i] = t;
        s.r[(size_t) i] = 0.5f * t;
    }
    for (int start = (int) (kSr * 0.25); start < n; start += (int) (kSr * 0.5))
    {
        s.hits.push_back (start);
        for (int i = 0; i < (int) (kSr * 0.03) && start + i < n; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            const float noise = 0.6f * ((float) (rng >> 8) / 8388608.0f - 1.0f) * std::exp (-(float) i / (float) (kSr * 0.004));
            s.l[(size_t) (start + i)] += noise;
            s.r[(size_t) (start + i)] += noise;
        }
    }
    return s;
}

struct Result { std::vector<float> l, r; std::vector<float> shareAtHit, shareBetween; int latency = 0; };

Result run (const shaper::Settings& s, const Signal& in)
{
    shaper::ShaperEngine e;
    e.setSettings (s);
    e.prepare (kSr, 2);
    e.setSettings (s);

    Result res { in.l, in.r, {}, {}, 0 };
    const int block = 256;
    for (int pos = 0; pos < (int) in.l.size(); pos += block)
    {
        const int n = std::min (block, (int) in.l.size() - pos);
        float* io[2] = { res.l.data() + pos, res.r.data() + pos };
        e.process (io, 2, n);
        res.latency = e.latency();

        const int end = pos + n;
        for (int h : in.hits)
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

/** Écart entre la sortie et l'entrée retardée de la latence, en dB sous le signal. */
double reconstructionDb (const Result& out, const std::vector<float>& in, const std::vector<float>& o)
{
    double err = 0, ref = 0;
    const auto L = (size_t) out.latency;
    for (size_t i = L * 2; i < in.size(); ++i)
    {
        const double d = o[i] - in[i - L];
        err += d * d; ref += (double) in[i - L] * in[i - L];
    }
    return 10.0 * std::log10 (ref / std::max (err, 1e-30));
}

/** Énergie autour des frappes, à gauche, en dB par rapport à l'entrée. */
double hitsDb (const Result& out, const Signal& in)
{
    double eIn = 0, eOut = 0;
    for (int h : in.hits)
        for (int i = 0; i < 1200 && h + out.latency + i < (int) in.l.size(); ++i)
        {
            eIn += (double) in.l[(size_t) (h + i)] * in.l[(size_t) (h + i)];
            eOut += (double) out.l[(size_t) (h + out.latency + i)] * out.l[(size_t) (h + out.latency + i)];
        }
    return 10.0 * std::log10 (eOut / eIn);
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, sr = (uint32_t) kSr, byteRate = sr * 4, riff = 36 + dataBytes, fmtLen = 16;
    const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&sr, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
        for (const auto* c : { &l, &r })
        {
            const int16_t s = (int16_t) std::lrint (std::max (-1.0f, std::min (1.0f, (*c)[i])) * 32767.0f);
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
}
} // namespace

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const auto in = makeSignal (4.0);
    const auto clicks = makeSignal (4.0, 0.0f);

    // 1. Depth à 0 : le son ressort intact, dans toutes les configurations.
    {
        const char* windows[] = { "court", "moyen", "long" };
        const char* oversamples[] = { "1x", "2x", "4x" };
        bool all = true;
        double worst = 1e9;
        for (int w = 0; w < 3; ++w)
            for (int os = 0; os < 3; ++os)
                for (int res = 0; res < 2; ++res)
                    for (int ph = 0; ph < 2; ++ph)
                    {
                        auto s = shaper::defaultSettings();
                        s.cutDepth = 0;
                        s.window = (shaper::Window) w; s.oversample = os; s.highResolution = res; s.phase = (shaper::Phase) ph;
                        const auto out = run (s, in);
                        const auto db = reconstructionDb (out, in.l, out.l);
                        worst = std::min (worst, db);
                        if (db < 60.0)
                        {
                            all = false;
                            std::printf ("      %s %s res %d phase %d : %.1f dB\n", windows[w], oversamples[os], res, ph, db);
                        }
                    }
        std::printf ("      pire reconstruction sur 36 configurations : %.1f dB\n", worst);
        check (all, "depth 0 : intact dans toutes les fenetres, oversamples, resolutions et phases (> 60 dB)");
    }

    // 2. Détection : les frappes, pas la note tenue.
    {
        auto s = shaper::defaultSettings();
        s.cutDepth = 0;
        const auto out = run (s, in);
        std::printf ("      part detectee : %.3f sur les frappes, %.3f entre elles\n", mean (out.shareAtHit), mean (out.shareBetween));
        check (mean (out.shareAtHit) > 4.0f * mean (out.shareBetween) + 0.02f, "les frappes sont detectees, la note tenue non");
    }

    // 3. Cut et boost.
    {
        auto s = shaper::defaultSettings();
        s.cutDepth = 10;
        const auto cut = run (s, clicks);
        s.boost = true; s.boostDepth = 10;
        const auto boost = run (s, clicks);
        s.phase = shaper::Phase::minimum;
        const auto boostMin = run (s, clicks);
        std::printf ("      frappes : cut %.1f dB, boost %+.1f dB, boost phase minimale %+.1f dB\n",
                     hitsDb (cut, clicks), hitsDb (boost, clicks), hitsDb (boostMin, clicks));
        check (hitsDb (cut, clicks) < -12.0, "cut depth 10 : les frappes baissent de plus de 12 dB");
        check (hitsDb (boost, clicks) > 4.0, "boost depth 10 : les frappes montent de plus de 4 dB");
        check (hitsDb (boostMin, clicks) > 4.0, "phase minimale : le boost marche aussi");
    }

    // 4. Les bandes : -18 dB partout, et les coupe-bas / coupe-haut, font ignorer.
    {
        auto s = shaper::defaultSettings();
        s.cutDepth = 10;
        s.bands[1] = { true, 20000.0f, -18.0f, 2.0f, (int) shaper::BandMode::lowShelf };
        s.bands[2].on = false;
        s.bands[3].on = false;
        const auto quiet = run (s, clicks);
        std::printf ("      etagere grave a -18 dB sur tout le spectre : frappes %.1f dB\n", hitsDb (quiet, clicks));
        check (hitsDb (quiet, clicks) > -3.0, "une bande a -18 dB ne laisse presque plus rien traiter (sous 20 kHz)");

        auto c = shaper::defaultSettings();
        c.cutDepth = 10;
        c.bands[0] = { true, 18000.0f, 0.0f, 0.7f, 3 };
        const auto lowcut = run (c, clicks);
        std::printf ("      coupe-bas a 18 kHz : frappes %.1f dB\n", hitsDb (lowcut, clicks));
        check (hitsDb (lowcut, clicks) > -3.0, "le coupe-bas fait ignorer ce qu'il coupe");
    }

    // 5. Stéréo : balance 100/0 en L/R ne touche pas la droite.
    {
        auto s = shaper::defaultSettings();
        s.cutDepth = 10; s.balance = 0.0f;
        const auto out = run (s, clicks);
        double eIn = 0, eOut = 0;
        for (int h : clicks.hits)
            for (int i = 0; i < 1200 && h + out.latency + i < (int) clicks.r.size(); ++i)
            {
                eIn += (double) clicks.r[(size_t) (h + i)] * clicks.r[(size_t) (h + i)];
                eOut += (double) out.r[(size_t) (h + out.latency + i)] * out.r[(size_t) (h + out.latency + i)];
            }
        const auto right = 10.0 * std::log10 (eOut / eIn);
        std::printf ("      balance 100/0 : gauche %.1f dB, droite %.1f dB\n", hitsDb (out, clicks), right);
        check (hitsDb (out, clicks) < -12.0 && std::abs (right) < 0.5, "balance 100/0 : seule la gauche est traitee");

        s.balance = 0.5f; s.stereoMode = shaper::StereoMode::midSide;
        const auto ms = run (s, clicks);
        std::printf ("      mid/side : frappes %.1f dB\n", hitsDb (ms, clicks));
        check (hitsDb (ms, clicks) < -12.0, "mid/side : les frappes au centre sont traitees");
        s.cutDepth = 0;
        const auto msNeutral = run (s, in);
        check (reconstructionDb (msNeutral, in.r, msNeutral.r) > 60.0, "mid/side, depth 0 : intact");
    }

    // 6. Delta, bypass, trim, écoute d'une bande.
    {
        auto s = shaper::defaultSettings();
        s.cutDepth = 0; s.delta = true;
        const auto d = run (s, in);
        float peak = 0;
        for (size_t i = (size_t) d.latency * 2; i < in.l.size(); ++i) peak = std::max (peak, std::abs (d.l[i]));
        check (peak < 1.0e-3f, "delta, depth 0 : presque rien");

        auto b = shaper::defaultSettings();
        b.cutDepth = 10; b.bypass = true;
        const auto by = run (b, in);
        check (reconstructionDb (by, in.l, by.l) > 100.0, "bypass : le son d'origine, latence comprise");

        auto t = shaper::defaultSettings();
        t.cutDepth = 0; t.trimDb = -6.0f;
        const auto trimmed = run (t, in);
        double ratio = 0, ref = 0;
        for (size_t i = (size_t) trimmed.latency * 2; i < in.l.size(); ++i)
        {
            ratio += (double) trimmed.l[i] * trimmed.l[i];
            ref += (double) in.l[i - (size_t) trimmed.latency] * in.l[i - (size_t) trimmed.latency];
        }
        const auto trimDb = 10.0 * std::log10 (ratio / ref);
        std::printf ("      trim -6 dB : %.2f dB\n", trimDb);
        check (std::abs (trimDb + 6.0) < 0.1, "trim : -6 dB sur le signal traite");

        auto l = shaper::defaultSettings();
        l.cutDepth = 10; l.listenBand = 2;
        const auto listen = run (l, clicks);
        double e = 0;
        for (size_t i = 0; i < listen.l.size(); ++i) e += (double) listen.l[i] * listen.l[i];
        check (e > 0.0 && std::isfinite (e), "ecoute d'une bande : on entend ce qu'elle change");
    }

    // 7. Les réglages extrêmes : ni NaN ni explosion.
    {
        auto s = shaper::defaultSettings();
        s.boost = true; s.boostDepth = 10; s.sensitivity = 10; s.sharpness = 10; s.decay = 10; s.decayLfHf = 10;
        s.bands[1].sens = 18; s.bands[2].sens = 18; s.bands[3].sens = 18;
        s.window = shaper::Window::longWindow; s.oversample = 2; s.highResolution = true; s.phase = shaper::Phase::minimum;
        const auto out = run (s, in);
        bool finite = true; float peak = 0;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
            peak = std::max (peak, std::abs (out.l[i]));
        }
        std::printf ("      reglages extremes : crete %.2f\n", peak);
        check (finite && peak < 8.0f, "reglages extremes : fini et borne");
        writeWav (dir + "/shaper_boost.wav", out.l, out.r);

        auto c = shaper::defaultSettings();
        c.cutDepth = 8;
        const auto cut = run (c, in);
        writeWav (dir + "/shaper_cut.wav", cut.l, cut.r);
        writeWav (dir + "/shaper_sec.wav", in.l, in.r);
    }

    std::printf (failures == 0 ? "\ntout passe\n" : "\n%d echec(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
