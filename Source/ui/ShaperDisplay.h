// Onirique Shaper — l'écran : spectre, réduction, et courbe de sensibilité
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** Tout ce que Shaper voit et fait, sur un seul axe des fréquences.

    Du bas vers le haut : le spectre qui entre ; ce qui est coupé (pend de la
    ligne du milieu) ou poussé (monte) en ce moment ; et la courbe de
    sensibilité avec ses cinq points : le coupe-bas, trois bandes, le
    coupe-haut.

    Un point se glisse (fréquence et sensibilité ; les coupes, en fréquence
    seulement), Cmd (Ctrl) en glissant ou la molette règle sa largeur, Alt en
    glissant fait écouter la bande, un double clic l'éteint ou la rallume. Un
    clic sur un point ouvre son panneau ; un clic dans le vide le referme. */
class ShaperDisplay final : public Component,
                           private Timer
{
public:
    explicit ShaperDisplay (ShaperProcessor&);
    ~ShaperDisplay() override;

    void paint (Graphics&) override;
    void resized() override;

    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

    /** Choisit la bande dont le panneau est ouvert, ou aucune avec -1. */
    void select (int band);
    int getSelected() const noexcept { return selected; }

    static Colour bandColour (int band);
    static bool isCut (int band) noexcept { return band == 0 || band == shaper::kNumBands - 1; }

    /** Relit le moteur tout de suite, comme le ferait le minuteur (pour les captures sans boucle de messages). */
    void refreshNow() { timerCallback(); }

    /** Ce qui se trouve sous un point, en mots, pour la description au survol. */
    String descriptionAt (Point<int>) const;

private:
    class BandPanel;

    void timerCallback() override;

    Rectangle<float> plotArea() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForCurveDb (float db) const;
    float curveDbForY (float y) const;
    float yForChangeDb (float db) const;
    float yForSpectrumDb (float db) const;

    Point<float> nodePosition (int band) const;
    int nodeAt (Point<float>) const;

    void setParam (const String& id, float value);
    void gesture (int band, bool begin);
    void setListening (int band);

    void paintGrid (Graphics&, Rectangle<float> plot);
    void paintSpectrum (Graphics&, Rectangle<float> plot);
    void paintChange (Graphics&, Rectangle<float> plot);
    void paintCurve (Graphics&, Rectangle<float> plot);
    void paintReadouts (Graphics&, Rectangle<float> plot);

    /** La valeur d'une colonne de pixels dans un tableau par bin : entre deux bins dans les graves,
        le plus fort (ou le plus bas) de la colonne dans les aigus. */
    float columnValue (const std::vector<float>&, float x, bool lowest) const;

    ShaperProcessor& processor;
    shaper::Settings settings;

    std::vector<float> inputDb, gainDb;
    double binHz = 23.4;
    uint32_t lastFrame = 0;
    float deepest = 0.0f;

    int selected = -1, hovered = -1, dragging = -1;
    bool altListening = false;
    float dragStartY = 0.0f, dragStartQ = 1.0f;

    std::unique_ptr<BandPanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperDisplay)
};

} // namespace onirique
