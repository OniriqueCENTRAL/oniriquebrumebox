// Onirique BrumeBox — l'interface
#pragma once

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/BrumeDisplay.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

namespace onirique
{

/** Quatre zones, chacune son rôle : l'en-tête (la licence, delta, bypass) ;
    la première colonne, ce qui est fait aux attaques et comment elles sont
    trouvées ; la deuxième, ce qui en naît (les résonateurs et la brume) ; et
    l'écran, où tout cela se passe. */
class BrumeBoxEditor final : public AudioProcessorEditor,
                             private Timer
{
public:
    explicit BrumeBoxEditor (BrumeBoxProcessor&);
    ~BrumeBoxEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    BrumeDisplay& getDisplay() noexcept { return display; }

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

    // L'en-tête.
    TextButton deltaButton { "DELTA" }, bypassButton { "BYPASS" };

    // Première colonne : les attaques.
    TextButton cutButton { "CUT" }, boostButton { "BOOST" };
    std::unique_ptr<ParameterAttachment> modeAttachment;
    Knob depth { "DEPTH" }, sensitivity { theme::fr ("SENSIBILITÉ") }, release { "RETOUR" }, tilt { "TILT" }, mix { "MIX" };

    // Deuxième colonne : ce qui en naît.
    ComboBox rootBox, chordBox;
    Knob reso { theme::fr ("RÉSO") }, decay { theme::fr ("DURÉE") }, brightness { theme::fr ("ÉCLAT") };
    Knob haze { "BRUME" }, hazeTime { theme::fr ("DURÉE") }, blur { "FLOU" };
    ValueField output { "OUT" };

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
