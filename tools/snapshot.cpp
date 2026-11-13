// Rend l'interface dans un PNG, sans hôte, avec une boucle de batterie et une nappe qui la traversent,
// pour regarder un changement d'interface tout droit sorti de la compilation.
//
//   brumebox_snapshot out.png [largeur hauteur] [--boost] [--closed] [--scale 2] [--tip x y]
//   brumebox_snapshot --selftest
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
        processor.reset (static_cast<onirique::BrumeBoxProcessor*> (createPluginFilter()));
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

        if (auto* e = dynamic_cast<onirique::BrumeBoxEditor*> (editor.get()))
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

        if (auto* e = dynamic_cast<onirique::BrumeBoxEditor*> (editor.get()))
            e->getDisplay().refreshNow();

        // Prise juste après une frappe claquée, quand l'écran montre le plus.
        if (++ticks < 90 || std::fmod (position / kSr, 0.5) < 0.27 || std::fmod (position / kSr, 0.5) > 0.33)
            return false;

        if (tipAt.x >= 0)
            if (auto* e = dynamic_cast<onirique::BrumeBoxEditor*> (editor.get()))
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
    std::unique_ptr<onirique::BrumeBoxProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    int position = 0, ticks = 0;
};
} // namespace

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

    std::unique_ptr<onirique::BrumeBoxProcessor> p (static_cast<onirique::BrumeBoxProcessor*> (createPluginFilter()));

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
           && s.phase == bb::Phase::minimum && std::abs (s.cutDepth - 5.0f) < 0.01f,
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
        std::printf ("usage : brumebox_snapshot out.png [largeur hauteur] [--boost] [--tip x y]\n");
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
