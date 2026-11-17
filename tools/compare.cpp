// Un outil de développement : passe les mêmes sons dans n'importe quel VST3 (Shaper, spiff...) et dit ce qu'il en a
// enlevé ou ajouté, sur les attaques et sur ce qui est tenu, dans les graves, les médiums et les aigus.
//
//   shaper_compare <plugin.vst3> --list
//   shaper_compare <plugin.vst3> <dossier de sortie> [nom=valeur ...]
//
// Les réglages se donnent par le nom du paramètre et sa valeur en texte, comme l'hôte les affiche
// (« depth=5 », « gain mode=boost »). Les sons d'entrée et de sortie sont écrits dans le dossier, en .wav.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;
using Buffer = std::vector<float>;

float noise()
{
    static uint32_t rng = 7;
    rng = rng * 1664525u + 1013904223u;
    return (float) (rng >> 8) / 8388608.0f - 1.0f;
}

const double twoPi = juce::MathConstants<double>::twoPi;

struct Sound { const char* name; Buffer audio; std::vector<int> events; };

/** Une voix (140 Hz, vibrato, syllabes de 300 ms) et un bruit de bouche de 1 ms toutes les 710 ms. */
Sound voiceWithClicks (int n)
{
    Sound s { "voix+clics", Buffer ((size_t) n), {} };
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        phase += twoPi * 140.0 * (1.0 + 0.01 * std::sin (twoPi * 5.0 * t)) / kSr;
        float v = 0.0f;
        for (int h = 1; h <= 30; ++h)
            v += (float) std::sin (phase * h) / (float) h * (h > 3 && h < 8 ? 1.6f : 1.0f);
        const double syl = std::fmod (t, 0.4);
        s.audio[(size_t) i] = 0.12f * v * (float) std::max (0.0, std::min (1.0, syl / 0.04) * std::min (1.0, (0.36 - syl) / 0.05));
    }
    for (double t = 0.53; t < n / kSr - 0.1; t += 0.71)
    {
        const int start = (int) (t * kSr);
        s.events.push_back (start);
        float prev = 0.0f;
        for (int i = 0; i < 48; ++i)
        {
            const float w = noise();
            s.audio[(size_t) (start + i)] += 0.25f * (w - prev) * std::exp (-(float) i / 12.0f);
            prev = w;
        }
    }
    return s;
}

/** Une corde pincée toutes les 400 ms. */
Sound pluck (int n)
{
    Sound s { "corde", Buffer ((size_t) n), {} };
    for (int start = (int) (0.2 * kSr); start < n; start += (int) (0.4 * kSr))
    {
        s.events.push_back (start);
        const double hz = 196.0 * std::pow (2.0, (double) (s.events.size() % 5) / 12.0);
        for (int i = 0; start + i < n && i < (int) (0.4 * kSr); ++i)
        {
            const double t = i / kSr;
            float v = 0.0f;
            for (int h = 1; h <= 12; ++h)
                v += (float) (std::sin (twoPi * hz * h * t) / h * std::exp (-t * (2.0 + h)));
            v += 0.8f * noise() * std::exp (-(float) i / 90.0f);
            s.audio[(size_t) (start + i)] += 0.15f * v;
        }
    }
    return s;
}

/** Batterie (grosse caisse, caisse claire, charleston) sur un accord tenu. */
Sound drumsOnChord (int n)
{
    Sound s { "batterie+accord", Buffer ((size_t) n), {} };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        float v = 0.0f;
        for (double hz : { 110.0, 164.8, 220.0, 277.2, 329.6, 440.0, 659.3, 880.0 })
            v += 0.03f * (float) std::sin (twoPi * hz * t + hz);
        v += 0.004f * noise();   // un peu d'air
        s.audio[(size_t) i] = v;
    }
    for (int k = 0, start = (int) (0.25 * kSr); start < n; start += (int) (0.25 * kSr), ++k)
    {
        s.events.push_back (start);
        for (int i = 0; start + i < n && i < (int) (0.2 * kSr); ++i)
        {
            const double t = i / kSr;
            float v = 0.0f;
            if (k % 2 == 0)
                v += 0.6f * (float) std::sin (twoPi * 55.0 * t * (1.0 + 3.0 * std::exp (-t * 40.0))) * (float) std::exp (-t * 18.0);
            else
                v += 0.35f * noise() * (float) std::exp (-t * 30.0) + 0.2f * (float) std::sin (twoPi * 190.0 * t) * (float) std::exp (-t * 25.0);
            v += 0.08f * noise() * (float) std::exp (-t * 120.0);
            s.audio[(size_t) (start + i)] += v;
        }
    }
    return s;
}

/** Une nappe tenue, rien d'autre : tout ce qui y est enlevé, c'est de la clarté perdue. */
Sound sustainedPad (int n)
{
    Sound s { "nappe seule", Buffer ((size_t) n), {} };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        float v = 0.0f;
        int h = 0;
        for (double hz : { 220.0, 277.2, 329.6, 440.0, 554.4, 659.3, 880.0, 1318.5, 1760.0, 2637.0, 3520.0, 5274.0 })
            v += 0.04f / (1.0f + 0.3f * (float) h++) * (float) std::sin (twoPi * hz * t * (1.0 + 0.002 * std::sin (twoPi * 0.3 * t + hz)));
        v *= 0.8f + 0.2f * (float) std::sin (twoPi * 0.5 * t);
        s.audio[(size_t) i] = v;
    }
    return s;
}

/** Le son passé par le plugin, recalé de sa latence. */
Buffer process (juce::AudioPluginInstance& p, const Buffer& in)
{
    p.prepareToPlay (kSr, kBlock);
    const int latency = p.getLatencySamples();
    const int n = (int) in.size();
    Buffer out ((size_t) n, 0.0f);
    juce::AudioBuffer<float> block (std::max (2, p.getTotalNumInputChannels()), kBlock);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < n + latency; pos += kBlock)
    {
        block.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBlock; ++i)
                block.setSample (c, i, pos + i < n ? in[(size_t) (pos + i)] : 0.0f);
        p.processBlock (block, midi);
        for (int i = 0; i < kBlock; ++i)
            if (const int k = pos + i - latency; k >= 0 && k < n)
                out[(size_t) k] = block.getSample (0, i);
    }
    p.releaseResources();
    return out;
}

/** L'énergie par trame de 5 ms (FFT 1024) dans trois zones : graves < 300 Hz, médiums, aigus > 3 kHz. */
struct Map { std::vector<std::array<double, 3>> frames; int hop = 240; };

Map analyse (const Buffer& x)
{
    constexpr int order = 10, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win (size), buf (2 * size);
    for (int i = 0; i < size; ++i)
        win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (twoPi * i / size);
    Map m;
    for (int start = 0; start + size <= (int) x.size(); start += m.hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < size; ++i)
            buf[(size_t) i] = x[(size_t) (start + i)] * win[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        std::array<double, 3> e { 1e-20, 1e-20, 1e-20 };
        for (int k = 1; k < size / 2; ++k)
        {
            const double hz = k * kSr / size;
            e[(size_t) (hz < 300.0 ? 0 : hz < 3000.0 ? 1 : 2)] += (double) buf[(size_t) k] * buf[(size_t) k];
        }
        m.frames.push_back (e);
    }
    return m;
}

/** Le changement moyen (en dB) sur les attaques (de 0 à 25 ms après chaque événement) et sur le reste (à plus de
    120 ms de tout événement), par zone. Le centre de la trame sert de repère. */
void report (const Sound& s, const Buffer& out)
{
    const auto a = analyse (s.audio), b = analyse (out);
    const int skip = (int) (0.5 * kSr);
    std::array<double, 3> inA {}, outA {}, inS {}, outS {};
    for (size_t f = 0; f < a.frames.size(); ++f)
    {
        const int centre = (int) f * a.hop + 512;
        if (centre < skip)
            continue;
        int nearest = 1 << 30;
        bool attack = false;
        for (int e : s.events)
        {
            nearest = std::min (nearest, std::abs (centre - e));
            attack = attack || (centre >= e && centre < e + (int) (0.025 * kSr));
        }
        const bool steady = nearest > (int) (0.12 * kSr);
        for (int z = 0; z < 3; ++z)
        {
            if (attack) { inA[(size_t) z] += a.frames[f][(size_t) z]; outA[(size_t) z] += b.frames[f][(size_t) z]; }
            if (steady) { inS[(size_t) z] += a.frames[f][(size_t) z]; outS[(size_t) z] += b.frames[f][(size_t) z]; }
        }
    }
    auto db = [] (double o, double i) { return i > 1e-12 ? 10.0 * std::log10 (std::max (o, 1e-20) / i) : 0.0; };
    double eIn = 0, eOut = 0;
    for (int i = skip; i < (int) s.audio.size(); ++i) { eIn += (double) s.audio[(size_t) i] * s.audio[(size_t) i]; eOut += (double) out[(size_t) i] * out[(size_t) i]; }
    std::printf ("  %-16s attaques  %+6.1f %+6.1f %+6.1f | tenu  %+6.1f %+6.1f %+6.1f | volume %+5.1f dB\n", s.name,
                 db (outA[0], inA[0]), db (outA[1], inA[1]), db (outA[2], inA[2]),
                 db (outS[0], inS[0]), db (outS[1], inS[1]), db (outS[2], inS[2]), db (eOut, eIn));
}

void writeWav (const juce::File& f, const Buffer& x)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), kSr, 1, 24, {}, 0));
    if (w == nullptr) return;
    const float* ch[] = { x.data() };
    w->writeFromFloatArrays (ch, 1, (int) x.size());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::printf ("usage : shaper_compare <plugin.vst3> --list | <dossier> [nom=valeur ...]\n");
        return 1;
    }

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, juce::String::fromUTF8 (argv[1]));
    if (found.isEmpty())
    {
        std::printf ("aucun plugin dans %s\n", argv[1]);
        return 1;
    }
    juce::String error;
    auto plugin = formats.createPluginInstance (*found[0], kSr, kBlock, error);
    if (plugin == nullptr)
    {
        std::printf ("chargement impossible : %s\n", error.toRawUTF8());
        return 1;
    }
    plugin->enableAllBuses();
    plugin->setPlayConfigDetails (2, 2, kSr, kBlock);

    if (juce::String (argv[2]) == "--list")
    {
        for (auto* p : plugin->getParameters())
            std::printf ("%-28s = %s\n", p->getName (64).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
        return 0;
    }

    for (int a = 3; a < argc; ++a)
    {
        const juce::String arg = juce::String::fromUTF8 (argv[a]);
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false).trim(), text = arg.fromFirstOccurrenceOf ("=", false, false).trim();
        bool set = false;
        for (auto* p : plugin->getParameters())
            if (p->getName (64).equalsIgnoreCase (name))
            {
                p->setValue (p->getValueForText (text));
                std::printf ("  %s = %s\n", name.toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
                set = true;
            }
        if (! set)
            std::printf ("  (pas de parametre « %s »)\n", name.toRawUTF8());
    }

    const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])));
    dir.createDirectory();
    const int n = (int) (6.0 * kSr);
    std::printf ("  %-16s           graves  medium aigus  |       graves  medium aigus  |\n", "");
    for (const auto& s : { voiceWithClicks (n), pluck (n), drumsOnChord (n), sustainedPad (n) })
    {
        const auto out = process (*plugin, s.audio);
        report (s, out);
        writeWav (dir.getChildFile (juce::String (s.name) + "-in.wav"), s.audio);
        writeWav (dir.getChildFile (juce::String (s.name) + "-out.wav"), out);
    }
    return 0;
}
