// Onirique BrumeBox — le plugin
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

namespace
{
    using Range = NormalisableRange<float>;

    std::unique_ptr<AudioParameterFloat> param (const String& id, const String& name, Range range, float def,
                                                std::function<String (float)> text)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([text] (float v, int) { return text (v); }));
    }

    std::unique_ptr<AudioParameterFloat> tenth (const String& id, const String& name, float def)
    {
        return param (id, name, Range (0.0f, 10.0f), def, [] (float v) { return String (v, 1); });
    }

    std::unique_ptr<AudioParameterFloat> hzParam (const String& id, const String& name, float def)
    {
        Range r (20.0f, 20000.0f);
        r.setSkewForCentre (632.0f);   // le milieu de la course : le milieu des octaves
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes()
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

    std::unique_ptr<AudioParameterChoice> choice (const String& id, const String& name, StringArray items, int def)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def);
    }

    std::unique_ptr<AudioParameterBool> toggle (const String& id, const String& name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
    }

    float get (const AudioProcessorValueTreeState& s, const String& id)
    {
        return s.getRawParameterValue (id)->load();
    }

    /** Balance 0..1 écrite comme dans spiff : « 100/100 », « 100/40 », « 0/100 ». */
    String balanceText (float v)
    {
        const int a = roundToInt (100.0f * jmin (1.0f, 2.0f * (1.0f - v)));
        const int b = roundToInt (100.0f * jmin (1.0f, 2.0f * v));
        return String (a) + "/" + String (b);
    }

    const StringArray kSlopes { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    const StringArray kModes { "Low shelf", "Parametric", "High shelf", "Tilt shelf" };
    constexpr float kDefaultFreq[bb::kNumBands] = { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f };
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

    // Les mêmes paramètres que spiff, sous les mêmes identifiants.
    l.add (choice ("gain mode", "Mode", { "Cut", "Boost" }, 0));
    l.add (tenth ("cut depth", "Cut depth", 5.0f));
    l.add (tenth ("boost depth", "Boost depth", 5.0f));
    l.add (tenth ("sensitivity", "Sensitivity", 5.0f));
    l.add (tenth ("sharpness", "Sharpness", 5.0f));
    l.add (tenth ("decay", "Decay", 5.0f));
    l.add (tenth ("decay lf/hf", "Decay LF/HF", 5.0f));

    for (int b = 0; b < bb::kNumBands; ++b)
    {
        const bool cut = b == 0 || b == bb::kNumBands - 1;
        const auto name = cut ? String (b == 0 ? "Low cut" : "High cut") : "Band " + String (b);

        l.add (toggle (bandId (b, "on"), name + " on", ! cut));
        l.add (hzParam (bandId (b, "freq"), name + " freq", kDefaultFreq[b]));

        if (! cut)
        {
            l.add (param (bandId (b, "sens"), name + " sens", Range (-18.0f, 18.0f), 0.0f,
                          [] (float v) { return String (v, 1) + " dB"; }));
            Range q (0.1f, 10.0f);
            q.setSkewForCentre (1.0f);
            l.add (param (bandId (b, "q"), name + " q", q, 0.7f, [] (float v) { return String (v, 2); }));
        }

        l.add (choice (bandId (b, "mode"), name + (cut ? " slope" : " mode"), cut ? kSlopes : kModes, 1));
    }

    l.add (choice ("stereo mode", "Stereo mode", { "Left/Right", "Mid/Side" }, 0));
    l.add (param ("stereo link", "Stereo link", Range (0.0f, 100.0f), 100.0f, [] (float v) { return String (roundToInt (v)) + " %"; }));
    l.add (param ("stereo balance", "Balance", Range (0.0f, 1.0f), 0.5f, balanceText));

    l.add (choice ("resolution", "Resolution", { "Normal", "High" }, 0));
    l.add (choice ("oversample", "Oversample", { "1x", "2x", "4x" }, 0));
    l.add (choice ("window length", "Window", { "Short", "Medium", "Long" }, 1));
    l.add (choice ("phase mode", "Phase", { "Linear", "Minimum" }, 0));

    l.add (param ("mix", "Mix", Range (0.0f, 100.0f), 100.0f, [] (float v) { return String (roundToInt (v)) + " %"; }));
    l.add (param ("trim", "Trim", Range (-12.0f, 12.0f), 0.0f, [] (float v) { return String (v, 1) + " dB"; }));
    l.add (toggle ("delta", "Delta", false));
    l.add (toggle ("bypass", "Bypass", false));
    return l;
}

bb::Settings BrumeBoxProcessor::readSettings() const
{
    auto s = bb::defaultSettings();
    s.boost = get (state, "gain mode") > 0.5f;
    s.cutDepth = get (state, "cut depth");
    s.boostDepth = get (state, "boost depth");
    s.sensitivity = get (state, "sensitivity");
    s.sharpness = get (state, "sharpness");
    s.decay = get (state, "decay");
    s.decayLfHf = get (state, "decay lf/hf");

    for (int b = 0; b < bb::kNumBands; ++b)
    {
        auto& band = s.bands[b];
        const bool cut = b == 0 || b == bb::kNumBands - 1;
        band.on = get (state, bandId (b, "on")) > 0.5f;
        band.freq = get (state, bandId (b, "freq"));
        band.mode = (int) get (state, bandId (b, "mode"));
        if (! cut)
        {
            band.sens = get (state, bandId (b, "sens"));
            band.q = get (state, bandId (b, "q"));
        }
    }

    s.stereoMode = get (state, "stereo mode") > 0.5f ? bb::StereoMode::midSide : bb::StereoMode::leftRight;
    s.stereoLink = get (state, "stereo link");
    s.balance = get (state, "stereo balance");
    s.highResolution = get (state, "resolution") > 0.5f;
    s.oversample = (int) get (state, "oversample");
    s.window = (bb::Window) (int) get (state, "window length");
    s.phase = get (state, "phase mode") > 0.5f ? bb::Phase::minimum : bb::Phase::linear;
    s.mix = get (state, "mix");
    s.trimDb = get (state, "trim");
    s.delta = get (state, "delta") > 0.5f;
    s.bypass = get (state, "bypass") > 0.5f;
    s.listenBand = listenBand.load();
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
    engine.setSettings (readSettings());
    engine.prepare (sampleRate, jmin (2, getTotalNumOutputChannels()));
    setLatencySamples (engine.latency());
}

void BrumeBoxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setSettings (readSettings());
    engine.process (buffer.getArrayOfWritePointers(), jmin (2, buffer.getNumChannels()), buffer.getNumSamples());

    // La fenêtre et l'oversample changent la latence : l'hôte en est prévenu.
    if (engine.latency() != getLatencySamples())
        setLatencySamples (engine.latency());

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
