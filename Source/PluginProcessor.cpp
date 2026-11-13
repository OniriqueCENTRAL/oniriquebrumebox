// Onirique BrumeBox — le plugin
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

namespace
{
    using Range = NormalisableRange<float>;

    String fr (const char* utf8) { return String::fromUTF8 (utf8); }

    std::unique_ptr<AudioParameterFloat> param (const String& id, const String& name, Range range, float def,
                                                const String& unit = {}, int decimals = 1)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withLabel (unit)
                .withStringFromValueFunction ([decimals, unit] (float v, int)
                {
                    return String (v, decimals) + (unit.isEmpty() ? String() : " " + unit);
                }));
    }

    std::unique_ptr<AudioParameterFloat> hzParam (const String& id, const String& name, float def)
    {
        Range r (20.0f, 20000.0f);
        r.setSkewForCentre (1000.0f);
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int)
                {
                    return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz";
                })
                .withValueFromStringFunction ([] (const String& t)
                {
                    const auto v = t.getFloatValue();
                    return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                }));
    }

    Range skewed (float lo, float hi, float centre)
    {
        Range r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    float get (const AudioProcessorValueTreeState& s, const String& id)
    {
        return s.getRawParameterValue (id)->load();
    }

    // Les bandes de départ : une étagère grave, trois cloches, une étagère aiguë, toutes à plat.
    struct BandDefault { float freq; bb::BandShape shape; };
    constexpr BandDefault kBandDefaults[bb::kNumBands] = {
        { 100.0f, bb::BandShape::lowShelf }, { 330.0f, bb::BandShape::bell }, { 1200.0f, bb::BandShape::bell },
        { 4000.0f, bb::BandShape::bell }, { 10000.0f, bb::BandShape::highShelf },
    };
}

BrumeBoxProcessor::BrumeBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "BrumeBox", createLayout())
{
}

AudioProcessorValueTreeState::ParameterLayout BrumeBoxProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;

    // Les attaques : couper ou pousser, et de combien.
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "boost", 1 }, "Mode", StringArray { "Cut", "Boost" }, 0));
    l.add (param ("depth", "Depth", Range (0.0f, 10.0f), 5.0f));

    // Détection
    l.add (param ("sensitivity", fr ("Sensibilité"), Range (0.0f, 10.0f), 5.0f));
    l.add (param ("release", "Retour", skewed (10.0f, 1000.0f, 120.0f), 80.0f, "ms", 0));
    l.add (param ("tilt", "Tilt", Range (-1.0f, 1.0f), 0.0f, {}, 2));

    for (int b = 0; b < bb::kNumBands; ++b)
    {
        const auto n = "Bande " + String (b + 1) + " ";
        l.add (std::make_unique<AudioParameterBool> (ParameterID { bandId (b, "on"), 1 }, n + "on", true));
        l.add (hzParam (bandId (b, "freq"), n + "freq", kBandDefaults[b].freq));
        l.add (param (bandId (b, "gain"), n + "sens", Range (-12.0f, 12.0f), 0.0f, "dB"));
        l.add (param (bandId (b, "q"), n + "q", skewed (0.2f, 6.0f, 1.0f), 1.0f, {}, 2));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "shape"), 1 }, n + "forme",
                                                       StringArray { "Cloche", fr ("Étagère grave"), fr ("Étagère aiguë") },
                                                       (int) kBandDefaults[b].shape));
    }

    // Résonateurs
    l.add (param ("reso", fr ("Résonance"), Range (0.0f, 1.0f), 0.5f));
    l.add (param ("decay", fr ("Durée reso"), skewed (0.2f, 10.0f, 2.0f), 2.0f, "s"));
    l.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "root", 1 }, "Note",
        StringArray { "Do", "Do#", fr ("Ré"), fr ("Ré#"), "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" }, 9));
    l.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "chord", 1 }, "Accord",
        StringArray { "Octaves", "Quintes", "Mineur 9", "Majeur 7", "Sus2", "Lydien" }, 2));
    l.add (param ("brightness", fr ("Éclat"), Range (0.0f, 1.0f), 0.5f));

    // Brume
    l.add (param ("haze", "Brume", Range (0.0f, 1.0f), 0.3f));
    l.add (param ("hazeTime", fr ("Durée brume"), skewed (0.5f, 20.0f, 4.0f), 4.0f, "s"));
    l.add (param ("blur", "Flou", Range (0.0f, 1.0f), 0.5f));

    // Sortie
    l.add (param ("mix", "Mix", Range (0.0f, 1.0f), 1.0f));
    l.add (param ("out", "Sortie", Range (-24.0f, 12.0f), 0.0f, "dB"));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "delta", 1 }, "Delta", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

bb::Settings BrumeBoxProcessor::readSettings() const
{
    bb::Settings s;
    s.boost = get (state, "boost") > 0.5f;
    s.depth = get (state, "depth");
    s.sensitivity = get (state, "sensitivity");
    s.releaseMs = get (state, "release");
    s.tilt = get (state, "tilt");

    for (int b = 0; b < bb::kNumBands; ++b)
    {
        auto& band = s.bands[b];
        band.on = get (state, bandId (b, "on")) > 0.5f;
        band.freq = get (state, bandId (b, "freq"));
        band.gainDb = get (state, bandId (b, "gain"));
        band.q = get (state, bandId (b, "q"));
        band.shape = (bb::BandShape) (int) get (state, bandId (b, "shape"));
    }

    s.reso = get (state, "reso");
    s.decayS = get (state, "decay");
    s.root = (int) get (state, "root");
    s.chord = (bb::Chord) (int) get (state, "chord");
    s.brightness = get (state, "brightness");
    s.haze = get (state, "haze");
    s.hazeS = get (state, "hazeTime");
    s.blur = get (state, "blur");
    s.mix = get (state, "mix");
    s.outDb = get (state, "out");
    s.delta = get (state, "delta") > 0.5f;
    s.bypass = get (state, "bypass") > 0.5f;
    return s;
}

bool BrumeBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void BrumeBoxProcessor::prepareToPlay (double sampleRate, int)
{
    license.refresh();
    engine.prepare (sampleRate, jmin (2, getTotalNumOutputChannels()));
    engine.setSettings (readSettings());
    setLatencySamples (engine.latency());
}

void BrumeBoxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setSettings (readSettings());
    engine.process (buffer.getArrayOfWritePointers(), jmin (2, buffer.getNumChannels()), buffer.getNumSamples());

    license.process (buffer, getSampleRate(), isNonRealtime());   // en dernier, sur toutes les sorties
}

AudioProcessorEditor* BrumeBoxProcessor::createEditor()
{
    return new BrumeBoxEditor (*this);
}

void BrumeBoxProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BrumeBoxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (ValueTree::fromXml (*xml));
}

} // namespace onirique

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::BrumeBoxProcessor();
}
