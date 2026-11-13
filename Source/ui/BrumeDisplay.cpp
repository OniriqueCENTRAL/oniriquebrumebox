// Onirique BrumeBox — l'écran
#include "BrumeDisplay.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    constexpr float kCurveRange = 21.0f;    // la courbe : ±21 dB d'un bord à l'autre (les bandes vont à ±18)
    constexpr float kChangeRange = 35.0f;   // la réduction : ±35 dB
    constexpr float kSpectrumFloor = -96.0f;

    const Colour violet { 0xff9b8cff };

    const char* const kBandNames[bb::kNumBands] = { "LOW CUT", "BAND 1", "BAND 2", "BAND 3", "HIGH CUT" };
}

//==============================================================================
/** Le bouton d'une bande : un anneau de sa couleur, plein quand elle travaille. */
class RingButton final : public Button
{
public:
    RingButton() : Button ("on") { setClickingTogglesState (true); }

    void paintButton (Graphics& g, bool over, bool) override
    {
        const auto p = getLocalBounds().toFloat().reduced (3.0f);
        const auto colour = Colour ((uint32) (int64) getProperties().getWithDefault ("accent", (int64) 0xffe23444));

        g.setColour (getToggleState() ? colour : Colour (0x40ffd2d6));
        g.drawEllipse (p, over ? 2.0f : 1.6f);
        if (getToggleState())
        {
            g.setColour (colour.withAlpha (0.4f));
            g.fillEllipse (p.reduced (5.0f));
        }
    }
};

/** L'écoute d'une bande : un casque, allumé quand on l'écoute. */
class ListenButton final : public Button
{
public:
    ListenButton() : Button ("listen") { setClickingTogglesState (true); }

    void paintButton (Graphics& g, bool over, bool) override
    {
        const auto on = getToggleState();
        const auto colour = on ? ember : (over ? mist : haze);
        auto r = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (on ? ember.withAlpha (0.18f) : Colour (0x59000000));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? ember.withAlpha (0.8f) : Colour (0x33ffd2d6));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        // Le casque : un arceau et deux écouteurs.
        const auto icon = r.removeFromLeft (r.getHeight()).reduced (6.0f);
        Path arc;
        arc.addCentredArc (icon.getCentreX(), icon.getCentreY() + 1.0f, icon.getWidth() * 0.45f, icon.getHeight() * 0.45f,
                           0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
        g.setColour (colour);
        g.strokePath (arc, PathStrokeType (1.5f));
        g.fillRoundedRectangle (icon.getX(), icon.getCentreY() + 1.0f, 3.0f, 5.0f, 1.0f);
        g.fillRoundedRectangle (icon.getRight() - 3.0f, icon.getCentreY() + 1.0f, 3.0f, 5.0f, 1.0f);

        g.setFont (capsFont (9.5f));
        g.drawText ("LISTEN", r, Justification::centredLeft, false);
    }
};

/** Le panneau de la bande choisie : posé sur l'écran, comme une île. */
class BrumeDisplay::BandPanel final : public Component
{
public:
    BandPanel (BrumeDisplay& d) : display (d)
    {
        addAndMakeVisible (power);
        addAndMakeVisible (listen);
        listen.onClick = [this] { display.setListening (listen.getToggleState() ? band : -1); };

        addAndMakeVisible (mode);

        for (auto* k : { &freq, &sens, &q })
        {
            k->setCompact (true);
            addAndMakeVisible (*k);
        }

        previous.onClick = [this] { display.select ((band + bb::kNumBands - 1) % bb::kNumBands); };
        next.onClick = [this] { display.select ((band + 1) % bb::kNumBands); };
        close.onClick = [this] { display.select (-1); };
        for (auto* b : { &previous, &next, &close })
        {
            b->getProperties().set ("plain", true);
            addAndMakeVisible (*b);
        }
    }

    void show (int newBand)
    {
        band = newBand;
        auto& state = display.processor.state;
        const bool cut = isCut (band);

        powerAttachment.reset();
        modeAttachment.reset();
        mode.clear (dontSendNotification);
        if (auto* p = dynamic_cast<AudioParameterChoice*> (state.getParameter (BrumeBoxProcessor::bandId (band, "mode"))))
            for (int i = 0; i < p->choices.size(); ++i)
                mode.addItem (p->choices[i].toUpperCase(), i + 1);

        powerAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
            state, BrumeBoxProcessor::bandId (band, "on"), power);
        modeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
            state, BrumeBoxProcessor::bandId (band, "mode"), mode);

        freq.attach (state, BrumeBoxProcessor::bandId (band, "freq"));
        sens.detach();
        q.detach();
        if (! cut)
        {
            sens.attach (state, BrumeBoxProcessor::bandId (band, "sens"));
            q.attach (state, BrumeBoxProcessor::bandId (band, "q"));
        }
        sens.setVisible (! cut);
        q.setVisible (! cut);

        const auto colour = bandColour (band);
        power.getProperties().set ("accent", (int64) colour.getARGB());
        for (auto* k : { &freq, &sens, &q })
            k->setAccent (colour);
        sens.slider.getProperties().set ("bipolar", true);

        listen.setToggleState (display.processor.listenBand.load() == band, dontSendNotification);
        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        const auto colour = bandColour (band);
        const auto area = getLocalBounds().toFloat().reduced (1.0f);

        // Le verre : sombre, teinté de la couleur de la bande, bordé d'un fil lumineux.
        g.setGradientFill (ColourGradient (Colour (0xf2241016).interpolatedWith (colour, 0.10f), area.getX(), area.getY(),
                                           Colour (0xf20d0709), area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle (area, 16.0f);
        g.setColour (colour.withAlpha (0.65f));
        g.drawRoundedRectangle (area, 16.0f, 1.4f);

        g.setColour (mist);
        g.setFont (capsFont (10.5f));
        g.drawText (kBandNames[band], power.getRight() + 8, power.getY(), 110, power.getHeight(), Justification::centredLeft, false);

        g.setFont (monoFont (11.0f).boldened());
        static const char* const shortNames[bb::kNumBands] = { "LC", "1", "2", "3", "HC" };
        g.drawText (shortNames[band], previous.getRight(), previous.getY(), next.getX() - previous.getRight(),
                    previous.getHeight(), Justification::centred, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 12);

        auto left = area.removeFromLeft (160);
        auto top = left.removeFromTop (30);
        power.setBounds (top.removeFromLeft (30));
        left.removeFromTop (6);
        mode.setBounds (left.removeFromTop (28));
        left.removeFromTop (8);
        listen.setBounds (left.removeFromTop (26).withWidth (96));

        auto right = area.removeFromRight (104);
        auto nav = right.removeFromTop (28);
        close.setBounds (nav.removeFromRight (28));
        nav.removeFromRight (6);
        previous.setBounds (nav.removeFromLeft (24));
        next.setBounds (nav.removeFromRight (24));

        area.reduce (6, 0);
        if (isCut (band))
        {
            freq.setBounds (area.withSizeKeepingCentre (area.getWidth() / 3, area.getHeight()));
        }
        else
        {
            const auto w = area.getWidth() / 3;
            freq.setBounds (area.removeFromLeft (w));
            sens.setBounds (area.removeFromLeft (w));
            q.setBounds (area);
        }
    }

    void listenChanged (int listening) { listen.setToggleState (listening == band, dontSendNotification); }

    int band = 1;

private:
    BrumeDisplay& display;

    RingButton power;
    ListenButton listen;
    ComboBox mode;
    Knob freq { "FREQ" }, sens { "SENS" }, q { "Q" };
    TextButton previous { "<" }, next { ">" }, close { "X" };

    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
};

//==============================================================================
Colour BrumeDisplay::bandColour (int band)
{
    static const Colour colours[bb::kNumBands] = { Colour (0xffb9a7ab), Colour (0xffff7a59), Colour (0xffffc857),
                                                   Colour (0xff9b8cff), Colour (0xff5cd6e8) };
    return colours[jlimit (0, bb::kNumBands - 1, band)];
}

BrumeDisplay::BrumeDisplay (BrumeBoxProcessor& p) : processor (p)
{
    panel = std::make_unique<BandPanel> (*this);
    addChildComponent (*panel);

    settings = processor.readSettings();
    startTimerHz (30);
}

BrumeDisplay::~BrumeDisplay()
{
    processor.listenBand = -1;
}

void BrumeDisplay::select (int band)
{
    selected = band;

    // Changer de bande arrête l'écoute de l'autre.
    if (processor.listenBand.load() >= 0 && processor.listenBand.load() != band)
        setListening (-1);

    if (band >= 0)
    {
        panel->show (band);
        panel->setVisible (true);
    }
    else
    {
        panel->setVisible (false);
    }

    repaint();
}

void BrumeDisplay::setListening (int band)
{
    processor.listenBand = band;
    panel->listenChanged (band);
    repaint();
}

void BrumeDisplay::timerCallback()
{
    settings = processor.readSettings();

    const auto& engine = processor.getEngine();
    const int bins = engine.getDisplayBins();

    if (bins > 0 && engine.getFrameCount() != lastFrame)
    {
        lastFrame = engine.getFrameCount();

        // La fenêtre ou l'oversample a changé : de nouveaux bins.
        if ((int) inputDb.size() != bins || binHz != engine.getDisplayBinHz())
        {
            binHz = engine.getDisplayBinHz();
            inputDb.assign ((size_t) bins, kSpectrumFloor);
            gainDb.assign ((size_t) bins, 0.0f);
        }

        const float* in = engine.getInputDb();
        const float* gain = engine.getGainDb();
        float most = 0.0f;

        for (int k = 0; k < bins; ++k)
        {
            const float db = jmax (kSpectrumFloor, in[k]);
            inputDb[(size_t) k] += (db - inputDb[(size_t) k]) * (db > inputDb[(size_t) k] ? 0.7f : 0.25f);
            gainDb[(size_t) k] += (gain[k] - gainDb[(size_t) k]) * (std::abs (gain[k]) > std::abs (gainDb[(size_t) k]) ? 0.8f : 0.3f);
            if (std::abs (gainDb[(size_t) k]) > std::abs (most))
                most = gainDb[(size_t) k];
        }

        deepest += (most - deepest) * (std::abs (most) > std::abs (deepest) ? 1.0f : 0.15f);
    }

    repaint();
}

//==============================================================================
Rectangle<float> BrumeDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft (38.0f).withTrimmedRight (38.0f)
                           .withTrimmedTop (12.0f).withTrimmedBottom (26.0f);
}

float BrumeDisplay::xForHz (float hz) const
{
    const auto plot = plotArea();
    const auto t = std::log (jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return plot.getX() + t * plot.getWidth();
}

float BrumeDisplay::hzForX (float x) const
{
    const auto plot = plotArea();
    const auto t = jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float BrumeDisplay::yForCurveDb (float db) const
{
    const auto plot = plotArea();
    return plot.getCentreY() - db / kCurveRange * plot.getHeight() * 0.5f;
}

float BrumeDisplay::curveDbForY (float y) const
{
    const auto plot = plotArea();
    return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * kCurveRange;
}

float BrumeDisplay::yForChangeDb (float db) const
{
    const auto plot = plotArea();
    return plot.getCentreY() - db / kChangeRange * plot.getHeight() * 0.5f;
}

float BrumeDisplay::yForSpectrumDb (float db) const
{
    const auto plot = plotArea();
    const auto t = jlimit (0.0f, 1.0f, (db - kSpectrumFloor) / -kSpectrumFloor);
    return plot.getBottom() - t * plot.getHeight();
}

Point<float> BrumeDisplay::nodePosition (int band) const
{
    const auto& b = settings.bands[band];
    return { xForHz (b.freq), isCut (band) ? yForCurveDb (0.0f) : yForCurveDb (b.sens) };
}

int BrumeDisplay::nodeAt (Point<float> p) const
{
    int best = -1;
    float bestDistance = 12.0f;

    for (int b = 0; b < bb::kNumBands; ++b)
    {
        const auto d = nodePosition (b).getDistanceFrom (p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = b;
        }
    }
    return best;
}

float BrumeDisplay::columnValue (const std::vector<float>& values, float x, bool lowest) const
{
    const auto lo = hzForX (x - 1.0f), hi = hzForX (x + 1.0f);
    const int last = (int) values.size() - 1;
    const int k0 = jlimit (0, last, (int) std::floor (lo / binHz));
    const int k1 = jlimit (k0, last, (int) std::ceil (hi / binHz));

    if (k1 - k0 <= 1)
    {
        const auto pos = jlimit (0.0f, (float) last - 1.0f, (lo + hi) * 0.5f / (float) binHz);
        const auto k = (int) pos;
        return values[(size_t) k] + (values[(size_t) k + 1] - values[(size_t) k]) * (pos - (float) k);
    }

    float v = values[(size_t) k0];
    for (int k = k0; k <= k1; ++k)
        v = lowest ? jmin (v, values[(size_t) k]) : jmax (v, values[(size_t) k]);
    return v;
}

//==============================================================================
void BrumeDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (ColourGradient (Colour (0xff120a0d), 0.0f, bounds.getY(), Colour (0xff1d0a0e), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 14.0f);
    g.setColour (line);
    g.drawRoundedRectangle (bounds, 14.0f, 1.0f);

    const auto plot = plotArea();
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());

        paintSpectrum (g, plot);
        paintGrid (g, plot);
        paintChange (g, plot);
        paintCurve (g, plot);
    }

    paintReadouts (g, plot);
}

void BrumeDisplay::paintGrid (Graphics& g, Rectangle<float> plot)
{
    for (float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const bool major = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
        g.setColour (Colour (0xffffd2d6).withAlpha (major ? 0.10f : 0.05f));
        g.drawVerticalLine (roundToInt (xForHz (hz)), plot.getY(), plot.getBottom());
    }

    for (float db : { -18.0f, -12.0f, -6.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (Colour (0xffffd2d6).withAlpha (0.05f));
        g.drawHorizontalLine (roundToInt (yForCurveDb (db)), plot.getX(), plot.getRight());
    }

    g.setColour (Colour (0xffffd2d6).withAlpha (0.16f));
    g.drawHorizontalLine (roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
}

void BrumeDisplay::paintSpectrum (Graphics& g, Rectangle<float> plot)
{
    if (inputDb.empty())
        return;

    Path fill;
    fill.startNewSubPath (plot.getX(), plot.getBottom());
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
    {
        // Une pente de +4,5 dB par octave, pour que le spectre se lise comme on l'entend.
        const auto db = columnValue (inputDb, x, false) + 4.5f * std::log2 (hzForX (x) / 1000.0f);
        fill.lineTo (x, yForSpectrumDb (db));
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    g.setGradientFill (ColourGradient (Colour (0x55e3b5ba), 0.0f, plot.getY(), Colour (0x10e3b5ba), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (Colour (0x40ffd2d6));
    g.strokePath (fill, PathStrokeType (1.0f));
}

void BrumeDisplay::paintChange (Graphics& g, Rectangle<float> plot)
{
    if (gainDb.empty())
        return;

    const auto zero = plot.getCentreY();
    Path shape;
    shape.startNewSubPath (plot.getX(), zero);
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        shape.lineTo (x, yForChangeDb (columnValue (gainDb, x, ! settings.boost)));
    shape.lineTo (plot.getRight(), zero);
    shape.closeSubPath();

    // Coupé : une ombre qui descend de la ligne ; poussé : une lueur qui monte.
    const auto colour = settings.boost ? violet : ember;
    g.setGradientFill (ColourGradient (colour.withAlpha (0.15f), 0.0f, zero,
                                       colour.withAlpha (0.55f), 0.0f, settings.boost ? plot.getY() : plot.getBottom(), false));
    g.fillPath (shape);
    g.setColour (colour.withAlpha (0.9f));
    g.strokePath (shape, PathStrokeType (1.2f));
}

void BrumeDisplay::paintCurve (Graphics& g, Rectangle<float> plot)
{
    // La forme de la bande choisie, en couleur, sous la courbe.
    if (selected >= 0 && settings.bands[selected].on)
    {
        Path own;
        own.startNewSubPath (plot.getX(), plot.getCentreY());
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            own.lineTo (x, yForCurveDb (jmax (-kCurveRange, bb::bandDb (settings, selected, hzForX (x)))));
        own.lineTo (plot.getRight(), plot.getCentreY());
        own.closeSubPath();

        g.setColour (bandColour (selected).withAlpha (0.18f));
        g.fillPath (own);
    }

    // La bande écoutée : sa zone éclairée.
    if (const int listening = processor.listenBand.load(); listening >= 0)
    {
        Path zone;
        zone.startNewSubPath (plot.getX(), plot.getBottom());
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            zone.lineTo (x, plot.getBottom() - bb::bandCoverage (settings, listening, hzForX (x)) * plot.getHeight());
        zone.lineTo (plot.getRight(), plot.getBottom());
        zone.closeSubPath();
        g.setColour (bandColour (listening).withAlpha (0.10f));
        g.fillPath (zone);
    }

    Path curve;
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
    {
        const auto y = yForCurveDb (jmax (-kCurveRange - 2.0f, bb::sensitivityDb (settings, hzForX (x))));
        if (x == plot.getX())
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    g.setColour (Colours::white.withAlpha (0.25f));
    g.strokePath (curve, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::white);
    g.strokePath (curve, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    for (int b = 0; b < bb::kNumBands; ++b)
    {
        const auto p = nodePosition (b);
        const auto colour = bandColour (b);
        const bool on = settings.bands[b].on;
        const float r = (b == hovered || b == dragging) ? 8.0f : 7.0f;
        const auto disc = Rectangle<float> (2 * r, 2 * r).withCentre (p);

        if (b == selected)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (disc.expanded (5.0f));
        }

        if (on)
        {
            Path e;
            e.addEllipse (disc);
            DropShadow (colour.withAlpha (0.7f), 8, {}).drawForPath (g, e);
            g.setColour (colour);
            g.fillEllipse (disc);
            g.setColour (Colours::white.withAlpha (0.85f));
            g.drawEllipse (disc, 1.4f);
        }
        else
        {
            g.setColour (night);
            g.fillEllipse (disc);
            g.setColour (colour.withAlpha (0.6f));
            g.drawEllipse (disc, 1.4f);
        }
    }
}

void BrumeDisplay::paintReadouts (Graphics& g, Rectangle<float> plot)
{
    g.setFont (monoFont (10.0f));

    g.setColour (haze);
    for (auto [hz, text] : { std::pair { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                             { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
        g.drawText (text, Rectangle<float> (40.0f, 16.0f).withCentre ({ xForHz (hz), plot.getBottom() + 13.0f }),
                    Justification::centred, false);

    // À gauche la sensibilité, à droite la réduction.
    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (Colour (0xffe3b5ba));
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (32.0f, 14.0f).withCentre ({ plot.getX() - 19.0f, yForCurveDb (db) }),
                    Justification::centred, false);
    }
    for (float db : { -30.0f, -20.0f, -10.0f, 0.0f, 10.0f, 20.0f, 30.0f })
    {
        g.setColour ((settings.boost ? violet : ember).withAlpha (0.85f));
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (32.0f, 14.0f).withCentre ({ plot.getRight() + 19.0f, yForChangeDb (db) }),
                    Justification::centred, false);
    }

    auto pill = [&g] (Rectangle<float> r, const String& text, Colour led)
    {
        g.setColour (Colour (0xb3000000));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (Colour (0x33ffd2d6));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (led);
        g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX() + 13.0f, r.getCentreY() }));
        g.setColour (mist);
        g.setFont (monoFont (11.0f).boldened());
        g.drawText (text, r.withTrimmedLeft (22.0f).withTrimmedRight (8.0f), Justification::centred, false);
    };

    // En haut à gauche : la configuration avancée ; à droite : la plus forte réduction du moment.
    static const char* windows[] = { "SHORT", "MEDIUM", "LONG" };
    static const char* oversamples[] = { "1X", "2X", "4X" };
    pill ({ plot.getX() + 10.0f, plot.getY() + 8.0f, 230.0f, 24.0f },
          String (windows[(int) settings.window]) + " · " + oversamples[jlimit (0, 2, settings.oversample)] + " · "
              + (settings.highResolution ? "HIGH RES" : "NORMAL") + " · " + (settings.phase == bb::Phase::minimum ? "MIN" : "LIN"),
          haze);

    pill ({ plot.getRight() - 150.0f, plot.getY() + 8.0f, 140.0f, 24.0f },
          String (settings.boost ? "BOOST " : "CUT ") + (deepest > 0.05f ? "+" : "") + String (deepest, 1) + " dB",
          settings.boost ? violet : ember);
}

void BrumeDisplay::resized()
{
    const auto plot = plotArea();
    panel->setBounds (Rectangle<int> (640, 132).withCentre ({ roundToInt (plot.getCentreX()), roundToInt (plot.getBottom() - 88.0f) })
                          .constrainedWithin (getLocalBounds().reduced (12)));
}

//==============================================================================
void BrumeDisplay::setParam (const String& id, float value)
{
    if (auto* p = processor.state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void BrumeDisplay::gesture (int band, bool begin)
{
    for (auto* what : { "freq", "sens", "q" })
        if (auto* p = processor.state.getParameter (BrumeBoxProcessor::bandId (band, what)))
            begin ? p->beginChangeGesture() : p->endChangeGesture();
}

void BrumeDisplay::mouseMove (const MouseEvent& e)
{
    const auto now = nodeAt (e.position);
    if (now != hovered)
    {
        hovered = now;
        setMouseCursor (hovered >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        repaint();
    }
}

void BrumeDisplay::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

void BrumeDisplay::mouseDown (const MouseEvent& e)
{
    const auto node = nodeAt (e.position);

    if (node < 0)
    {
        if (plotArea().contains (e.position))
            select (-1);
        return;
    }

    select (node);
    dragging = node;
    dragStartY = e.position.y;
    dragStartQ = settings.bands[node].q;
    gesture (node, true);

    // Alt en glissant : on écoute la bande le temps du geste.
    if (e.mods.isAltDown())
    {
        altListening = true;
        setListening (node);
    }
}

void BrumeDisplay::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0)
        return;

    const auto plot = plotArea();

    // Cmd (Ctrl) en glissant : la largeur, vers le haut plus étroite.
    if (e.mods.isCommandDown() && ! isCut (dragging))
    {
        const auto q = jlimit (0.1f, 10.0f, dragStartQ * std::pow (2.0f, (dragStartY - e.position.y) / 60.0f));
        setParam (BrumeBoxProcessor::bandId (dragging, "q"), q);
    }
    else
    {
        setParam (BrumeBoxProcessor::bandId (dragging, "freq"), hzForX (jlimit (plot.getX(), plot.getRight(), e.position.x)));
        if (! isCut (dragging))
            setParam (BrumeBoxProcessor::bandId (dragging, "sens"),
                      jlimit (-18.0f, 18.0f, curveDbForY (jlimit (plot.getY(), plot.getBottom(), e.position.y))));
    }

    settings = processor.readSettings();
    repaint();
}

void BrumeDisplay::mouseUp (const MouseEvent&)
{
    if (dragging >= 0)
        gesture (dragging, false);
    dragging = -1;

    if (altListening)
    {
        altListening = false;
        setListening (-1);
    }
}

void BrumeDisplay::mouseDoubleClick (const MouseEvent& e)
{
    const auto node = nodeAt (e.position);
    if (node < 0)
        return;

    if (auto* p = processor.state.getParameter (BrumeBoxProcessor::bandId (node, "on")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void BrumeDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    auto node = nodeAt (e.position);
    if (node < 0)
        node = selected;
    if (node < 0 || isCut (node))
        return;

    const auto id = BrumeBoxProcessor::bandId (node, "q");
    if (auto* p = processor.state.getParameter (id))
    {
        const auto q = processor.state.getRawParameterValue (id)->load();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (jlimit (0.1f, 10.0f, q * std::pow (1.25f, wheel.deltaY * 4.0f))));
        p->endChangeGesture();
    }
}

String BrumeDisplay::descriptionAt (Point<int> at) const
{
    if (panel->isVisible() && panel->getBounds().contains (at))
        return fr ("La bande choisie : FREQ la place, SENS donne plus (au-dessus de 0) ou moins (en dessous) de traitement ici, jusqu'à rien à -18 dB, Q sa largeur. Le menu choisit sa forme (ou la pente des coupes). LISTEN fait entendre ce qui change dans sa zone. Le rond l'allume ou l'éteint.");

    if (nodeAt (at.toFloat()) >= 0)
        return fr ("Glissez pour la fréquence et la sensibilité ; Cmd (Ctrl) en glissant ou la molette pour la largeur ; Alt en glissant pour l'écouter ; double-clic pour l'éteindre ou la rallumer.");

    if (plotArea().contains (at.toFloat()))
        return fr ("En fond, le spectre qui entre. En rouge, ce qui est coupé en ce moment (en violet, ce qui est poussé), lu sur l'échelle de droite. La ligne blanche : la courbe de sensibilité, une pondération de la détection et non un EQ.");

    return {};
}

} // namespace onirique
