// Onirique BrumeBox — l'interface
#pragma once

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/BrumeDisplay.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

namespace onirique
{

/** Les paramètres de spiff, ni plus ni moins, avec l'allure ONIRIQUE.

    L'en-tête : la licence, DELTA, BYPASS. La première colonne : CUT ou BOOST,
    DEPTH, puis la détection (SENSITIVITY, DECAY, SHARPNESS, DECAY LF/HF). La
    deuxième : la stéréo (mode, link, balance), la sortie (mix, trim) et
    l'avancé (resolution, oversample, window, phase). L'écran : le spectre, la
    réduction, et les cinq bandes de la courbe de sensibilité. */
class BrumeBoxEditor final : public AudioProcessorEditor,
                             private Timer
{
public:
    explicit BrumeBoxEditor (BrumeBoxProcessor&);
    ~BrumeBoxEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    BrumeDisplay& getDisplay() noexcept { return display; }

    /** Le menu des presets transformé en champ où taper le nom du preset à enregistrer (aussi pour les captures). */
    void showSaveField() { beginSaving(); }

    /** Montre tout de suite, et garde, la description de ce qui est sous un point (pour les captures). */
    void pinDescription (Point<int> at);

private:
    using ButtonAttachment = AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = AudioProcessorValueTreeState::ComboBoxAttachment;
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

    BrumeBoxProcessor& processor;
    BrumeDisplay display;

    // L'en-tête : les presets, DELTA, BYPASS.
    TextButton previousButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, openButton { "OPEN" };
    ComboBox presetBox;
    Label presetNameEditor;
    std::vector<BrumeBoxProcessor::Preset> presetList;
    std::unique_ptr<FileChooser> chooser;
    TextButton deltaButton { "DELTA" }, bypassButton { "BYPASS" };

    void refreshPresets();
    void stepPreset (int direction);
    void beginSaving();
    void openPresetFile();

    // Première colonne : cut ou boost, et la détection.
    TextButton cutButton { "CUT" }, boostButton { "BOOST" };
    std::unique_ptr<ParameterAttachment> modeAttachment;
    Knob depth { "DEPTH" }, sensitivity { "SENSITIVITY" }, decay { "DECAY" }, sharpness { "SHARPNESS" }, decayLfHf { "DECAY LF/HF" };

    // Deuxième colonne : stéréo, sortie, avancé.
    ComboBox stereoModeBox, resolutionBox, oversampleBox, windowBox, phaseBox;
    Knob link { "STEREO LINK" }, balance { "BALANCE" }, mix { "MIX" }, trim { "TRIM" };

    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    Image logo;
    double openedAt = Time::getMillisecondCounterHiRes();

    // En dernier : détruit en premier, dessiné par-dessus tout.
    std::unique_ptr<license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeBoxEditor)
};

} // namespace onirique
