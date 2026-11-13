// Onirique BrumeBox — l'écran : spectre, attaques traitées, accord, et courbe de sensibilité
#pragma once

#include <array>

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** Tout ce que BrumeBox voit et fait, sur un seul axe des fréquences.

    Du bas vers le haut : le spectre qui entre ; l'accord des résonateurs, une
    étoile par note ; ce qui est fait aux attaques en ce moment (coupé : pend
    du haut ; poussé : monte du bas) ; et la courbe de sensibilité avec ses
    cinq points.

    Un point se glisse (fréquence et sensibilité), la molette règle sa
    largeur, un double clic l'éteint ou le rallume. Un clic sur un point ouvre
    son panneau ; un clic dans le vide le referme. */
class BrumeDisplay final : public Component,
                           private Timer
{
public:
    explicit BrumeDisplay (BrumeBoxProcessor&);
    ~BrumeDisplay() override;

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
    void beginGesture (int band);
    void endGesture();

    void paintGrid (Graphics&, Rectangle<float> plot);
    void paintSpectrum (Graphics&, Rectangle<float> plot);
    void paintChange (Graphics&, Rectangle<float> plot);
    void paintChord (Graphics&, Rectangle<float> plot);
    void paintCurve (Graphics&, Rectangle<float> plot);
    void paintReadouts (Graphics&, Rectangle<float> plot);

    BrumeBoxProcessor& processor;
    bb::Settings settings;

    // Ce que le moteur a vu, lissé pour l'écran.
    std::vector<float> inputDb, envelope;
    double binHz = 23.4;
    uint32_t lastFrame = 0;
    float activity = 0.0f;
    std::array<float, bb::kMaxModes> modeHz {};
    int numModes = 0;

    int selected = -1, hovered = -1, dragging = -1;

    std::unique_ptr<BandPanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeDisplay)
};

} // namespace onirique
