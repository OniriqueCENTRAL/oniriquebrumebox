// Onirique BrumeBox — le plugin
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/BrumeEngine.h"
#include "License/LicenseGuard.h"

namespace onirique
{
using namespace juce;

class BrumeBoxProcessor : public AudioProcessor
{
public:
    BrumeBoxProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;
    using AudioProcessor::processBlock;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Les réglages tels que les paramètres les donnent (aussi pour l'écran). */
    bb::Settings readSettings() const;

    const bb::BrumeEngine& getEngine() const noexcept { return engine; }

    /** Les identifiants des paramètres : les mêmes noms que spiff (« band2 freq », « decay lf/hf »...). */
    static String bandId (int band, const char* what) { return "band" + String (band) + " " + what; }

    /** La bande écoutée, ou -1. Un état de l'interface, pas un paramètre (comme dans spiff). */
    std::atomic<int> listenBand { -1 };

    AudioProcessorValueTreeState state;

    // Licence ONIRIQUE : fiche « brumebox » sur CENTRAL, réservée aux fondateurs.
    license::Guard license { "brumebox" };

private:
    static AudioProcessorValueTreeState::ParameterLayout createLayout();

    bb::BrumeEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeBoxProcessor)
};

} // namespace onirique
