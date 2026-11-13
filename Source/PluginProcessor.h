// Onirique BrumeBox — le plugin
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/BrumeEngine.h"
#include "License/LicenseGuard.h"

class BrumeBoxProcessor : public juce::AudioProcessor
{
public:
    BrumeBoxProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState state;

    // Licence ONIRIQUE. Le plugin « brumebox » reste non publié dans ONIRIQUE CENTRAL : seuls les
    // comptes de l'équipe (fondateurs) avec « Mes licences » activé y ont droit et le téléchargent.
    onirique::license::Guard license { "brumebox" };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    bb::Settings readSettings() const;

    bb::BrumeEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeBoxProcessor)
};
