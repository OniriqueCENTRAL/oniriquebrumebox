// Rend l'interface dans un PNG, sans hôte, avec une boucle de batterie et une nappe qui la traversent,
// pour regarder un changement d'interface tout droit sorti de la compilation.
//
//   shaper_snapshot out.png [largeur hauteur] [--boost] [--closed] [--scale 2] [--tip x y]
//   shaper_snapshot --selftest
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

/** Une frappe tous les quarts de seconde (grave, puis claquement), sur un accord tenu. */
float sampleAt (int n)
{
    static uint32_t rng = 1;
    const double t = n / kSr;
    const int beat = (int) (t / 0.25);
    const double since = t - beat * 0.25;

    float s = 0.0f;
    for (double hz : { 110.0, 164.8, 261.6, 392.0 })
        s += 0.04f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * t);

    rng = rng * 1664525u + 1013904223u;
    const float noise = (float) (rng >> 8) / 8388608.0f - 1.0f;

    if (beat % 2 == 0)
        s += 0.6f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-since * 40.0)) * since) * std::exp (-since * 12.0));
    else
        s += 0.45f * noise * (float) std::exp (-since * 45.0);

    return s;
}

struct Director
{
    Director (juce::File destination, int width, int height, bool boost, juce::Point<int> tip, float scaleToUse, bool closed)
        : file (std::move (destination)), tipAt (tip), scale (scaleToUse)
    {
        processor.reset (static_cast<onirique::ShaperProcessor*> (createPluginFilter()));
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        set ("gain mode", boost ? 1.0f : 0.0f);
        set ("cut depth", 6.4f);
        set ("boost depth", 5.5f);
        set ("sensitivity", 4.7f);
        set ("decay", 5.6f);
        set ("decay lf/hf", 3.9f);
        set ("band0 on", 1.0f);
        set ("band0 freq", 35.0f);
        set ("band1 freq", 317.0f);
        set ("band1 sens", -7.3f);
        set ("band1 q", 0.4f);
        set ("band2 freq", 1400.0f);
        set ("band2 sens", 10.0f);
        set ("band3 freq", 6200.0f);
        set ("band3 sens", 12.0f);
        set ("band3 q", 1.4f);
        set ("band4 on", 1.0f);
        set ("band4 freq", 14000.0f);
        processor->state.state.setProperty ("presetName", juce::String::fromUTF8 ("Batterie : démo"), nullptr);

        editor.reset (processor->createEditor());
        editor->setSize (width, height);

        if (auto* e = dynamic_cast<onirique::ShaperEditor*> (editor.get()))
            e->getDisplay().select (closed ? -1 : 1);
    }

    void set (const juce::String& id, float value)
    {
        if (auto* p = processor->state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    /** Un tic d'écran : un peu de son, puis l'écran relit le moteur. Rend vrai quand la photo est prise. */
    bool tick()
    {
        // Quelques blocs de son par tic, pour que l'écran suive comme dans un DAW.
        for (int b = 0; b < 4; ++b)
        {
            juce::AudioBuffer<float> buffer (2, kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                const auto s = sampleAt (position + i);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            position += kBlock;
            juce::MidiBuffer midi;
            processor->processBlock (buffer, midi);
        }

        if (auto* e = dynamic_cast<onirique::ShaperEditor*> (editor.get()))
            e->getDisplay().refreshNow();

        // Prise juste après une frappe claquée, quand l'écran montre le plus.
        if (++ticks < 90 || std::fmod (position / kSr, 0.5) < 0.27 || std::fmod (position / kSr, 0.5) > 0.33)
            return false;

        if (tipAt.x >= 0)
            if (auto* e = dynamic_cast<onirique::ShaperEditor*> (editor.get()))
                e->pinDescription (tipAt);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);

        editor.reset();
        processor.reset();
        return true;
    }

    juce::File file;
    juce::Point<int> tipAt;
    float scale = 1.0f;
    std::unique_ptr<onirique::ShaperProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    int position = 0, ticks = 0;
};
} // namespace

/** Les quatre usages de spiff, mesurés sur le plugin et ses presets d'usine, avec des sons fabriqués :
    une voix avec des bruits de bouche, une corde pincée, un mix écrasé par un compresseur. */
namespace usages
{
using Buffer = std::vector<float>;

float noise()
{
    static uint32_t rng = 7;
    rng = rng * 1664525u + 1013904223u;
    return (float) (rng >> 8) / 8388608.0f - 1.0f;
}

/** Une voix : 140 Hz avec vibrato et harmoniques qui décroissent, des syllabes de 300 ms. */
Buffer voice (int n)
{
    Buffer v ((size_t) n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        phase += 2.0 * juce::MathConstants<double>::pi * 140.0 * (1.0 + 0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t)) / kSr;
        float s = 0.0f;
        for (int h = 1; h <= 30; ++h)
            s += (float) std::sin (phase * h) / (float) h * (h > 3 && h < 8 ? 1.6f : 1.0f);
        const double syl = std::fmod (t, 0.4);
        const float env = (float) (std::min (1.0, syl / 0.04) * std::min (1.0, (0.36 - syl) / 0.05));
        v[(size_t) i] = 0.12f * s * std::max (0.0f, env);
    }
    return v;
}

/** Les bruits de bouche : un claquement de 1 ms, aigu, aux instants donnés. */
std::vector<int> addClicks (Buffer& v)
{
    std::vector<int> at;
    for (double t = 0.53; t < (double) v.size() / kSr - 0.1; t += 0.71)
    {
        const int start = (int) (t * kSr);
        at.push_back (start);
        float prev = 0.0f;
        for (int i = 0; i < 48; ++i)
        {
            const float white = noise();
            const float hp = white - prev;   // de l'aigu, comme un « tss » de langue
            prev = white;
            v[(size_t) (start + i)] += 0.25f * hp * std::exp (-(float) i / 12.0f);
        }
    }
    return at;
}

/** Une corde pincée toutes les 400 ms : un claquement de médiator puis une note qui s'éteint. */
Buffer pluck (int n, std::vector<int>& attacks)
{
    Buffer v ((size_t) n);
    for (int start = (int) (0.2 * kSr); start < n; start += (int) (0.4 * kSr))
    {
        attacks.push_back (start);
        const double hz = 196.0 * std::pow (2.0, (attacks.size() % 5) / 12.0);
        for (int i = 0; start + i < n && i < (int) (0.4 * kSr); ++i)
        {
            const double t = i / kSr;
            float s = 0.0f;
            for (int h = 1; h <= 12; ++h)
                s += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * hz * h * t) / h * std::exp (-t * (2.0 + h)));
            s += 0.8f * noise() * std::exp (-(float) i / 90.0f);
            v[(size_t) (start + i)] += 0.15f * s;
        }
    }
    return v;
}

/** Un mix : batterie (grosse caisse, caisse claire) sur un accord, puis écrasé par un compresseur rapide. */
Buffer squashedMix (int n)
{
    Buffer v ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        const double beat = std::fmod (t, 0.25);
        float s = 0.0f;
        for (double hz : { 110.0, 164.8, 220.0, 277.2, 329.6 })
            s += 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * t);
        s += 0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * beat * (1.0 + 4.0 * std::exp (-beat * 40.0))) * (float) std::exp (-beat * 18.0);
        if (((int) (t / 0.25)) % 2 == 1)
            s += 0.4f * noise() * (float) std::exp (-beat * 30.0);
        v[(size_t) i] = s;
    }
    // le compresseur : 2 ms d'attaque, 60 ms de relâchement, ratio 10, seuil bas ; le gain remonté ensuite
    float env = 0.0f;
    for (auto& s : v)
    {
        const float a = std::abs (s);
        env += (a - env) * (a > env ? 1.0f - std::exp (-1.0f / (0.002f * (float) kSr)) : 1.0f - std::exp (-1.0f / (0.06f * (float) kSr)));
        const float over = juce::Decibels::gainToDecibels (std::max (env, 1.0e-6f)) + 24.0f;
        s *= juce::Decibels::decibelsToGain (over > 0.0f ? -over * 0.9f : 0.0f) * 3.0f;
    }
    return v;
}

int presetIndex (const char* name)
{
    for (int i = 0; i < (int) std::size (onirique::presets::factory); ++i)
        if (juce::String::fromUTF8 (onirique::presets::factory[i].name) == juce::String::fromUTF8 (name))
            return i;
    return -1;
}

/** Le son passé par le plugin avec un preset, recalé de sa latence (mono : les deux canaux pareils). */
Buffer process (onirique::ShaperProcessor& p, const char* preset, const Buffer& in)
{
    const int index = presetIndex (preset);
    jassert (index >= 0);
    p.loadFactoryPreset (index);
    p.prepareToPlay (kSr, kBlock);
    const int latency = p.getLatencySamples();
    const int n = (int) in.size();

    Buffer out ((size_t) n, 0.0f);
    juce::AudioBuffer<float> block (2, kBlock);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < n + latency; pos += kBlock)
    {
        const int len = kBlock;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i)
                block.setSample (c, i, pos + i < n ? in[(size_t) (pos + i)] : 0.0f);
        p.processBlock (block, midi);
        for (int i = 0; i < len; ++i)
            if (const int k = pos + i - latency; k >= 0 && k < n)
                out[(size_t) k] = block.getSample (0, i);
    }
    p.releaseResources();
    return out;
}

double energyDb (const Buffer& v, int from, int to)
{
    double e = 1.0e-20;
    for (int i = std::max (0, from); i < std::min ((int) v.size(), to); ++i)
        e += (double) v[(size_t) i] * v[(size_t) i];
    return 10.0 * std::log10 (e);
}

/** L'énergie autour de chaque instant (de `before` avant à `after` après), additionnée, en dB. */
double aroundDb (const Buffer& v, const std::vector<int>& at, int before, int after)
{
    double e = 1.0e-20;
    for (int a : at)
        for (int i = std::max (0, a - before); i < std::min ((int) v.size(), a + after); ++i)
            e += (double) v[(size_t) i] * v[(size_t) i];
    return 10.0 * std::log10 (e);
}

/** Le facteur de crête : la moyenne des pics de chaque quart de seconde sur le niveau moyen, en dB. */
double crestDb (const Buffer& v)
{
    const int span = (int) (0.25 * kSr);
    double peaks = 0.0, rms = 0.0;
    int count = 0;
    for (int s = span; s + span <= (int) v.size(); s += span, ++count)
    {
        float pk = 0.0f;
        for (int i = s; i < s + span; ++i)
            pk = std::max (pk, std::abs (v[(size_t) i]));
        peaks += pk;
    }
    for (int i = span; i < (int) v.size(); ++i)
        rms += (double) v[(size_t) i] * v[(size_t) i];
    rms = std::sqrt (rms / (double) (v.size() - (size_t) span));
    return 20.0 * std::log10 (peaks / std::max (1, count) / rms);
}

template <typename Check>
void run (onirique::ShaperProcessor& p, Check&& check)
{
    const int n = (int) (6.0 * kSr);
    const int skip = (int) (0.4 * kSr);   // le temps que la détection apprenne le son

    // 1. Les bruits de bouche s'en vont, la voix reste.
    {
        const auto clean = voice (n);
        auto dirty = clean;
        auto clicks = addClicks (dirty);
        clicks.erase (std::remove_if (clicks.begin(), clicks.end(), [&] (int c) { return c < skip; }), clicks.end());
        const auto out = process (p, "Voix : bruits de bouche", dirty);

        // ce qui reste du bruit : la sortie moins la voix propre, autour de chaque claquement
        Buffer residue ((size_t) n), noiseIn ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            residue[(size_t) i] = out[(size_t) i] - clean[(size_t) i];
            noiseIn[(size_t) i] = dirty[(size_t) i] - clean[(size_t) i];
        }
        const double clickDrop = aroundDb (residue, clicks, 0, 240) - aroundDb (noiseIn, clicks, 0, 240);
        // la voix loin des claquements : le reste du signal
        std::vector<bool> nearClick ((size_t) n, false);
        for (int c : clicks)
            for (int i = std::max (0, c - 2400); i < std::min (n, c + 2400); ++i)
                nearClick[(size_t) i] = true;
        double eIn = 1e-20, eErr = 1e-20;
        for (int i = skip; i < n; ++i)
            if (! nearClick[(size_t) i])
            {
                eIn += (double) clean[(size_t) i] * clean[(size_t) i];
                eErr += (double) (out[(size_t) i] - clean[(size_t) i]) * (out[(size_t) i] - clean[(size_t) i]);
            }
        const double voiceKept = 10.0 * std::log10 (eIn / eErr);
        std::printf ("      bruits de bouche : %.1f dB sur les claquements, voix intacte a %.1f dB pres\n", clickDrop, voiceKept);
        check (clickDrop < -6.0, "usage 1 : « Voix : bruits de bouche » baisse les claquements de plus de 6 dB");
        check (voiceKept > 20.0, "usage 1 : la voix entre les claquements reste la meme (ecart sous -20 dB)");
    }

    // 2. De la clarté et de l'expression : l'attaque de chaque note ressort, la note tenue ne bouge pas.
    {
        std::vector<int> attacks;
        const auto in = pluck (n, attacks);
        attacks.erase (std::remove_if (attacks.begin(), attacks.end(), [&] (int c) { return c < skip; }), attacks.end());
        const auto out = process (p, "Instrument : plus d'expression", in);
        const double attackUp = aroundDb (out, attacks, 0, 480) - aroundDb (in, attacks, 0, 480);
        std::vector<int> tails;
        for (int a : attacks) tails.push_back (a + (int) (0.15 * kSr));
        const double tailMove = aroundDb (out, tails, 0, (int) (0.15 * kSr)) - aroundDb (in, tails, 0, (int) (0.15 * kSr));
        std::printf ("      expression : attaques %+.1f dB, notes tenues %+.1f dB\n", attackUp, tailMove);
        check (attackUp > 2.0, "usage 2 : « Instrument : plus d'expression » fait ressortir les attaques de plus de 2 dB");
        check (std::abs (tailMove) < 1.0, "usage 2 : les notes tenues ne bougent pas (moins de 1 dB)");
    }

    // 3. En avant ou en arrière, sans changer le volume.
    {
        std::vector<int> attacks;
        const auto in = pluck (n, attacks);
        attacks.erase (std::remove_if (attacks.begin(), attacks.end(), [&] (int c) { return c < skip; }), attacks.end());
        const auto front = process (p, "Mettre en avant", in);
        const auto back = process (p, "Mettre en arrière", in);
        const double inAttack = aroundDb (in, attacks, 0, 480);
        const double frontAttack = aroundDb (front, attacks, 0, 480) - inAttack, backAttack = aroundDb (back, attacks, 0, 480) - inAttack;
        const double frontLevel = energyDb (front, skip, n) - energyDb (in, skip, n), backLevel = energyDb (back, skip, n) - energyDb (in, skip, n);
        std::printf ("      en avant : attaques %+.1f dB, volume %+.2f dB ; en arriere : attaques %+.1f dB, volume %+.2f dB\n",
                     frontAttack, frontLevel, backAttack, backLevel);
        check (frontAttack > 2.0 && backAttack < -2.0, "usage 3 : « Mettre en avant / en arriere » deplacent les attaques de plus de 2 dB");
        check (std::abs (frontLevel) < 1.0 && std::abs (backLevel) < 1.0, "usage 3 : le volume d'ensemble bouge de moins de 1 dB");
    }

    // 4. De la dynamique et du détail dans un mix trop compressé.
    {
        const auto in = squashedMix (n);
        const auto out = process (p, "Mix trop compressé : rendre la dynamique", in);
        const double before = crestDb (in), after = crestDb (out);
        std::printf ("      mix ecrase : crete / moyenne %.1f dB -> %.1f dB\n", before, after);
        check (after - before > 1.5, "usage 4 : « Mix trop compresse » rend plus de 1,5 dB de dynamique (crete sur moyenne)");
    }
}
} // namespace usages

/** Les presets sans fenêtre : chaque identifiant existe, enregistrer puis rouvrir rend les mêmes valeurs,
    et un fichier .preset de spiff s'ouvre. */
int selftest()
{
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    };

    std::unique_ptr<onirique::ShaperProcessor> p (static_cast<onirique::ShaperProcessor*> (createPluginFilter()));

    juce::StringArray unknown;
    for (const auto& f : onirique::presets::factory)
        for (const auto& [id, value] : f.values)
            if (p->state.getParameter (id) == nullptr)
                unknown.add (juce::String::fromUTF8 (f.name) + " : " + id);
    check (unknown.isEmpty(), "les " + juce::String ((int) std::size (onirique::presets::factory))
                                  + " presets d'usine n'utilisent que des parametres existants " + unknown.joinIntoString (", "));

    p->loadFactoryPreset (3);   // Kick : attaque plus nette
    const auto s = p->readSettings();
    check (s.boost && std::abs (s.boostDepth - 4.5f) < 0.01f && s.bands[1].mode == 0 && std::abs (s.bands[1].freq - 120.0f) < 0.5f
           && s.phase == shaper::Phase::minimum && std::abs (s.cutDepth - 5.0f) < 0.01f,
           "charger un preset d'usine : ses valeurs, et le reste par defaut (" + p->getPresetName() + ")");

    const auto name = juce::String ("zz test ") + juce::String (juce::Random::getSystemRandom().nextInt (100000));
    const auto file = p->saveUserPreset (name);
    check (file.existsAsFile(), "enregistrer un preset : " + file.getFullPathName());
    p->loadFactoryPreset (0);
    const bool reopened = p->loadPresetFile (file);
    const auto back = p->readSettings();
    check (reopened && back.boost && std::abs (back.boostDepth - 4.5f) < 0.01f && p->getPresetName() == name,
           "le rouvrir rend les memes reglages");
    bool listed = false;
    for (const auto& preset : p->listPresets())
        listed = listed || preset.name == name;
    check (listed, "il apparait dans la liste, sous MES PRESETS");
    file.deleteFile();

    {   // un projet enregistré du temps de BrumeBox garde ses réglages
        p->loadFactoryPreset (3);
        auto xml = p->state.copyState().createXml();
        xml->setTagName ("BrumeBox");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        p->loadFactoryPreset (0);
        p->setStateInformation (old.getData(), (int) old.getSize());
        check (p->readSettings().boost, "un projet BrumeBox s'ouvre avec ses reglages");
    }

    usages::run (*p, check);

    const juce::File spiff ("/Library/Application Support/oeksound/spiff/Presets/full reset.preset");
    if (spiff.existsAsFile())
    {
        const bool ok = p->loadPresetFile (spiff);
        const auto v = p->readSettings();
        check (ok && v.cutDepth < 0.01f && v.bands[1].on && std::abs (v.bands[2].freq - 1000.0f) < 1.0f,
               "un fichier .preset de spiff s'ouvre (" + p->getPresetName() + ")");
    }

    std::printf (failures == 0 ? "\ntout passe\n" : "\n%d echec(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--selftest")
    {
        const juce::ScopedJuceInitialiser_GUI gui;
        return selftest();
    }

    if (argc < 2)
    {
        std::printf ("usage : shaper_snapshot out.png [largeur hauteur] [--boost] [--tip x y]\n");
        return 1;
    }

    const juce::ScopedJuceInitialiser_GUI gui;

    int width = 1260, height = 720;
    bool boost = false, closed = false;
    float scale = 1.0f;
    juce::Point<int> tip { -1, -1 };

    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--boost") boost = true;
        else if (a == "--closed") closed = true;
        else if (a == "--scale" && i + 1 < argc) scale = (float) atof (argv[++i]);
        else if (a == "--tip" && i + 2 < argc) { tip = { atoi (argv[i + 1]), atoi (argv[i + 2]) }; i += 2; }
        else if (i + 1 < argc && a.containsOnly ("0123456789")) { width = a.getIntValue(); height = atoi (argv[++i]); }
    }

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    Director director (file, width, height, boost, tip, scale, closed);
    while (! director.tick()) {}

    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    return 0;
}
