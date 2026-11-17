// Onirique Shaper — l'interface
#pragma once

#include "../License/OniriqueThemeButton.h"
#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/ShaperDisplay.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "ui/OniriqueDA.h"

namespace onirique
{

/** Les paramètres de spiff, ni plus ni moins, avec l'allure ONIRIQUE.

    L'en-tête : les presets, la licence, DELTA, BYPASS. Une seule colonne : CUT ou BOOST,
    DEPTH, puis SENSITIVITY, DECAY, SHARPNESS et DECAY LF/HF. L'écran : le spectre, la
    réduction, et les cinq bandes de la courbe de sensibilité. Le pied, sous l'écran,
    comme sur ResoBox : MIX, OUT | STEREO, LINK, BAL | WINDOW, PHASE. Resolution et
    oversample restent dans les presets et l'automation, sans bouton. */
class ShaperEditor final : public AudioProcessorEditor,
                             private Timer
{
public:
    explicit ShaperEditor (ShaperProcessor&);
    ~ShaperEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    ShaperDisplay& getDisplay() noexcept { return display; }

    /** Le menu des presets transformé en champ où taper le nom du preset à enregistrer (aussi pour les captures). */
    void showSaveField() { beginSaving(); }

    /** Montre tout de suite, et garde, la description de ce qui est sous un point (pour les captures). */
    void pinDescription (Point<int> at);

private:
    using ButtonAttachment = AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void paintHeader (Graphics&, Rectangle<int> header);
    void paintSection (Graphics&, const String& title, int y, int left, int right);
    int titleRight() const;

    struct HoverTip final : Component
    {
        void paint (Graphics& g) override { getLookAndFeel().drawTooltip (g, text, getWidth(), getHeight()); }
        String text;
    };

    struct Described
    {
        Component* component;
        String text;
    };

    void describe (std::initializer_list<Component*>, const String& text);
    String descriptionAt (Point<int>) const;
    void updateHoverTip();

    std::vector<Described> described;
    HoverTip hoverTip;
    String restingOn;
    Point<int> restingAt;
    uint32 restingSince = 0;
    bool pinned = false;

    ShaperProcessor& processor;
    ShaperDisplay display;

    // L'en-tête : les presets, DELTA, BYPASS.
    TextButton previousButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    ComboBox presetBox;
    Label presetNameEditor;
    std::vector<ShaperProcessor::Preset> presetList;
    TextButton deltaButton { "DELTA" }, bypassButton { "BYPASS" };

    void refreshPresets();
    void stepPreset (int direction);
    void beginSaving();

    // Première colonne : cut ou boost, et la détection.
    TextButton cutButton { "CUT" }, boostButton { "BOOST" };
    std::unique_ptr<ParameterAttachment> modeAttachment;
    Knob depth { "DEPTH" }, sensitivity { "SENSITIVITY" }, decay { "DECAY" }, sharpness { "SHARPNESS" }, decayLfHf { "DECAY LF/HF" };

    // Le pied, comme celui de ResoBox.
    ValueField mix { "MIX" }, output { "OUT" }, link { "LINK" }, balance { "BAL" };
    ComboBox stereoBox, windowBox, phaseBox;

    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    Image logo;

    // En dernier : détruit en premier, dessiné par-dessus tout.
    std::unique_ptr<license::Overlay> licenseOverlay;

    onirique::look::ThemeButton themeButton;   // ⋯ : la couleur de l'interface
    int themeSeen = -1;
    Image daBackdrop;   // le fond de VOCALBOX, calculé une fois par taille

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperEditor)
};

} // namespace onirique
