// Rend l'interface dans un PNG, sans hôte, avec une boucle de batterie et une nappe qui la traversent,
// pour regarder un changement d'interface tout droit sorti de la compilation.
//
//   brumebox_snapshot out.png [largeur hauteur] [--boost] [--tip x y]
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

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
    Director (juce::File destination, int width, int height, bool boost, juce::Point<int> tip)
        : file (std::move (destination)), tipAt (tip)
    {
        processor.reset (static_cast<onirique::BrumeBoxProcessor*> (createPluginFilter()));
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        set ("depth", 6.4f);
        set ("boost", boost ? 1.0f : 0.0f);
        set ("sensitivity", 6.0f);
        set ("b1_freq", 330.0f);
        set ("b1_gain", -5.0f);
        set ("b2_freq", 2000.0f);
        set ("b2_gain", 7.0f);
        set ("b2_q", 0.8f);
        set ("b3_freq", 6200.0f);
        set ("b3_gain", 9.0f);
        set ("b3_q", 1.6f);
        set ("b4_gain", -9.0f);
        set ("reso", 0.7f);
        set ("haze", 0.45f);

        editor.reset (processor->createEditor());
        editor->setSize (width, height);

        if (auto* e = dynamic_cast<onirique::BrumeBoxEditor*> (editor.get()))
            e->getDisplay().select (1);
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

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);

        editor.reset();
        processor.reset();
        return true;
    }

    juce::File file;
    juce::Point<int> tipAt;
    std::unique_ptr<onirique::BrumeBoxProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    int position = 0, ticks = 0;
};
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage : brumebox_snapshot out.png [largeur hauteur] [--boost] [--tip x y]\n");
        return 1;
    }

    const juce::ScopedJuceInitialiser_GUI gui;

    int width = 1260, height = 720;
    bool boost = false;
    juce::Point<int> tip { -1, -1 };

    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--boost") boost = true;
        else if (a == "--tip" && i + 2 < argc) { tip = { atoi (argv[i + 1]), atoi (argv[i + 2]) }; i += 2; }
        else if (i + 1 < argc && a.containsOnly ("0123456789")) { width = a.getIntValue(); height = atoi (argv[++i]); }
    }

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    Director director (file, width, height, boost, tip);
    while (! director.tick()) {}

    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    return 0;
}
