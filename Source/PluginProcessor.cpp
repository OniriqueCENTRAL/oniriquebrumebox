// Onirique BrumeBox — le plugin
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Range = juce::NormalisableRange<float>;

    std::unique_ptr<juce::AudioParameterFloat> param (const char* id, const char* name, Range range,
                                                      float def, const char* unit = "")
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def, juce::AudioParameterFloatAttributes().withLabel (unit));
    }

    Range skewed (float lo, float hi, float centre)
    {
        Range r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    float get (const juce::AudioProcessorValueTreeState& s, const char* id)
    {
        return s.getRawParameterValue (id)->load();
    }
}

BrumeBoxProcessor::BrumeBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "BrumeBox", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout BrumeBoxProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // Détection
    l.add (param ("sensitivity", "Sensibilite", Range (0.0f, 10.0f), 5.0f));
    l.add (param ("release", "Retour", skewed (10.0f, 1000.0f, 120.0f), 80.0f, "ms"));
    l.add (param ("tilt", "Tilt", Range (-1.0f, 1.0f), 0.0f));

    // Attaques et résonateurs
    l.add (param ("punch", "Attaque", Range (-24.0f, 12.0f), 0.0f, "dB"));
    l.add (param ("reso", "Resonance", Range (0.0f, 1.0f), 0.5f));
    l.add (param ("decay", "Duree reso", skewed (0.2f, 10.0f, 2.0f), 2.0f, "s"));
    l.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "root", 1 }, "Note",
        juce::StringArray { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" }, 9));
    l.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "chord", 1 }, "Accord",
        juce::StringArray { "Octaves", "Quintes", "Mineur 9", "Majeur 7", "Sus2", "Lydien" }, 2));
    l.add (param ("brightness", "Eclat", Range (0.0f, 1.0f), 0.5f));

    // Brume
    l.add (param ("haze", "Brume", Range (0.0f, 1.0f), 0.3f));
    l.add (param ("hazeTime", "Duree brume", skewed (0.5f, 20.0f, 4.0f), 4.0f, "s"));
    l.add (param ("blur", "Flou", Range (0.0f, 1.0f), 0.5f));

    // Sortie
    l.add (param ("mix", "Mix", Range (0.0f, 1.0f), 1.0f));
    l.add (param ("out", "Sortie", Range (-24.0f, 12.0f), 0.0f, "dB"));
    return l;
}

bb::Settings BrumeBoxProcessor::readSettings() const
{
    bb::Settings s;
    s.sensitivity = get (state, "sensitivity");
    s.releaseMs = get (state, "release");
    s.tilt = get (state, "tilt");
    s.punchDb = get (state, "punch");
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
    return s;
}

bool BrumeBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void BrumeBoxProcessor::prepareToPlay (double sampleRate, int)
{
    license.refresh();
    engine.prepare (sampleRate, juce::jmin (2, getTotalNumOutputChannels()));
    engine.setSettings (readSettings());
    setLatencySamples (engine.latency());
}

void BrumeBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setSettings (readSettings());
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()),
                    buffer.getNumSamples());

    license.process (buffer, getSampleRate(), isNonRealtime());   // en dernier, sur toutes les sorties
}

juce::AudioProcessorEditor* BrumeBoxProcessor::createEditor()
{
    return new BrumeBoxEditor (*this);
}

void BrumeBoxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BrumeBoxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrumeBoxProcessor();
}
