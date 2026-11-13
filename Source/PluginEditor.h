// Onirique BrumeBox — interface du prototype : les réglages génériques de JUCE,
// sous une barre qui porte la pastille de licence ONIRIQUE.
#pragma once

#include "PluginProcessor.h"
#include "License/LicenseOverlay.h"

class BrumeBoxEditor : public juce::AudioProcessorEditor
{
public:
    explicit BrumeBoxEditor (BrumeBoxProcessor& p)
        : AudioProcessorEditor (p), generic (p)
    {
        addAndMakeVisible (generic);

        // En dernier : la licence couvre toute l'interface.
        licenseOverlay = std::make_unique<onirique::license::Overlay> ("BRUMEBOX", p.license);
        addAndMakeVisible (*licenseOverlay);

        setSize (juce::jmax (520, generic.getWidth()), generic.getHeight() + kBar);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b0b14));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("ONIRIQUE BRUMEBOX", 14, 0, 260, kBar, juce::Justification::centredLeft);
    }

    void resized() override
    {
        generic.setBounds (getLocalBounds().withTrimmedTop (kBar));
        if (licenseOverlay != nullptr)
            licenseOverlay->fit (getLocalBounds(), 1.0f, { getWidth() - 258, 7, 248, 26 },
                                 juce::Justification::centredRight);
    }

private:
    static constexpr int kBar = 40;

    juce::GenericAudioProcessorEditor generic;
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeBoxEditor)
};
