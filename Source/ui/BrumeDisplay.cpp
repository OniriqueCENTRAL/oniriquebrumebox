// Onirique BrumeBox — l'écran
#include "BrumeDisplay.h"
#include "OniriqueKnob.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    constexpr float kCurveRange = 15.0f;    // la courbe : ±15 dB d'un bord à l'autre
    constexpr float kChangeRange = 30.0f;   // ce qui est fait aux attaques : ±30 dB
    constexpr float kSpectrumFloor = -96.0f;

    const Colour violet { 0xff9b8cff };

    const char* const kBandNames[bb::kNumBands] = { "BANDE 1", "BANDE 2", "BANDE 3", "BANDE 4", "BANDE 5" };
    const char* const kNoteNames[12] = { "DO", "DO#", "RÉ", "RÉ#", "MI", "FA", "FA#", "SOL", "SOL#", "LA", "LA#", "SI" };
    const char* const kChordNames[bb::kNumChords] = { "OCTAVES", "QUINTES", "MINEUR 9", "MAJEUR 7", "SUS2", "LYDIEN" };

    Path star (Point<float> c, float r) { return ui::knobStar (c, r); }
}

//==============================================================================
/** Le bouton d'une bande : un anneau de sa couleur, plein quand elle travaille. */
class RingButton final : public Button
{
public:
    RingButton() : Button ("power") { setClickingTogglesState (true); }

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

/** Le panneau de la bande choisie : posé sur l'écran, comme une île. */
class BrumeDisplay::BandPanel final : public Component
{
public:
    BandPanel (BrumeDisplay& d) : display (d)
    {
        addAndMakeVisible (power);

        shape.addItemList ({ "CLOCHE", fr ("ÉTAGÈRE GRAVE"), fr ("ÉTAGÈRE AIGUË") }, 1);
        addAndMakeVisible (shape);

        // Comme l'île de ResoBox : le nom sous le potard, la valeur à sa place quand on le touche.
        for (auto* k : { &freq, &gain, &q })
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

        powerAttachment.reset();
        shapeAttachment.reset();
        powerAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
            state, BrumeBoxProcessor::bandId (band, "on"), power);
        shapeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
            state, BrumeBoxProcessor::bandId (band, "shape"), shape);

        freq.attach (state, BrumeBoxProcessor::bandId (band, "freq"));
        gain.attach (state, BrumeBoxProcessor::bandId (band, "gain"));
        q.attach (state, BrumeBoxProcessor::bandId (band, "q"));

        const auto colour = bandColour (band);
        power.getProperties().set ("accent", (int64) colour.getARGB());
        power.repaint();
        for (auto* k : { &freq, &gain, &q })
            k->setAccent (colour);
        gain.slider.getProperties().set ("bipolar", true);

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

        g.setColour (haze);
        g.setFont (capsFont (9.5f));
        g.drawText (kBandNames[band], shape.getBounds().translated (0, 30), Justification::centredLeft, false);

        g.setColour (mist);
        g.setFont (monoFont (11.0f).boldened());
        g.drawText (String (band + 1), previous.getRight(), previous.getY(), next.getX() - previous.getRight(),
                    previous.getHeight(), Justification::centred, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 12);

        auto left = area.removeFromLeft (150);
        power.setBounds (left.removeFromTop (30).removeFromLeft (30));
        left.removeFromTop (8);
        shape.setBounds (left.removeFromTop (28));

        auto right = area.removeFromRight (104);
        auto nav = right.removeFromTop (28);
        close.setBounds (nav.removeFromRight (28));
        nav.removeFromRight (6);
        previous.setBounds (nav.removeFromLeft (24));
        next.setBounds (nav.removeFromRight (24));

        area.reduce (6, 0);
        const auto w = area.getWidth() / 3;
        freq.setBounds (area.removeFromLeft (w));
        gain.setBounds (area.removeFromLeft (w));
        q.setBounds (area);
    }

    int band = 0;

private:
    BrumeDisplay& display;

    RingButton power;
    ComboBox shape;
    Knob freq { "FREQ" }, gain { "SENS" }, q { "Q" };
    TextButton previous { "<" }, next { ">" }, close { "X" };

    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment;
};

//==============================================================================
Colour BrumeDisplay::bandColour (int band)
{
    static const Colour colours[bb::kNumBands] = { Colour (0xffff7a59), Colour (0xffffc857), Colour (0xff9b8cff),
                                                   Colour (0xff5cd6e8), Colour (0xff7be0a0) };
    return colours[jlimit (0, bb::kNumBands - 1, band)];
}

BrumeDisplay::BrumeDisplay (BrumeBoxProcessor& p) : processor (p)
{
    panel = std::make_unique<BandPanel> (*this);
    addChildComponent (*panel);

    settings = processor.readSettings();
    startTimerHz (30);
}

BrumeDisplay::~BrumeDisplay() = default;

void BrumeDisplay::select (int band)
{
    selected = band;

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

void BrumeDisplay::timerCallback()
{
    settings = processor.readSettings();

    const auto& engine = processor.getEngine();
    const int bins = engine.getDisplayBins();

    if (bins > 0 && engine.getFrameCount() != lastFrame)
    {
        lastFrame = engine.getFrameCount();
        binHz = engine.getDisplayBinHz();

        if ((int) inputDb.size() != bins)
        {
            inputDb.assign ((size_t) bins, kSpectrumFloor);
            envelope.assign ((size_t) bins, 0.0f);
        }

        const float* in = engine.getInputDb();
        const float* env = engine.getEnvelope();
        float sum = 0.0f;

        for (int k = 0; k < bins; ++k)
        {
            const float db = jmax (kSpectrumFloor, in[k]);
            inputDb[(size_t) k] += (db - inputDb[(size_t) k]) * (db > inputDb[(size_t) k] ? 0.7f : 0.25f);
            envelope[(size_t) k] += (env[k] - envelope[(size_t) k]) * (env[k] > envelope[(size_t) k] ? 0.8f : 0.3f);
            sum += envelope[(size_t) k];
        }

        activity += (sum / (float) bins - activity) * 0.3f;
    }

    numModes = engine.getNumModes();
    for (int i = 0; i < numModes; ++i)
        modeHz[(size_t) i] = engine.getModeHz (i);

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
    return { xForHz (b.freq), yForCurveDb (b.gainDb) };
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

//==============================================================================
void BrumeDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    // Le cadre : un puits sombre, à peine rougi vers le bas.
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
        paintChord (g, plot);
        paintCurve (g, plot);
    }

    paintReadouts (g, plot);
}

void BrumeDisplay::paintGrid (Graphics& g, Rectangle<float> plot)
{
    g.setFont (monoFont (10.0f));

    for (float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const auto x = xForHz (hz);
        const bool major = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
        g.setColour (Colour (0xffffd2d6).withAlpha (major ? 0.10f : 0.05f));
        g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
    }

    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
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

    const int columns = roundToInt (plot.getWidth());
    for (int i = 0; i <= columns; i += 2)
    {
        const auto x = plot.getX() + (float) i;
        const auto lo = hzForX (x - 1.0f), hi = hzForX (x + 1.0f);
        const int k0 = jlimit (1, (int) inputDb.size() - 1, (int) std::floor (lo / binHz));
        const int k1 = jlimit (k0, (int) inputDb.size() - 1, (int) std::ceil (hi / binHz));

        // Dans les graves, entre deux bins ; dans les aigus, le plus fort de la colonne.
        float db = kSpectrumFloor;
        if (k1 - k0 <= 1)
        {
            const auto pos = jlimit (1.0f, (float) inputDb.size() - 2.0f, (lo + hi) * 0.5f / (float) binHz);
            const auto k = (int) pos;
            const auto f = pos - (float) k;
            db = inputDb[(size_t) k] * (1.0f - f) + inputDb[(size_t) k + 1] * f;
        }
        else
        {
            for (int k = k0; k <= k1; ++k)
                db = jmax (db, inputDb[(size_t) k]);
        }

        // Une pente de +4,5 dB par octave, pour que le spectre se lise comme on l'entend.
        db += 4.5f * std::log2 (jmax (20.0, (double) ((lo + hi) * 0.5f)) / 1000.0f);
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
    if (envelope.empty() || settings.depth <= 0.0f)
        return;

    const auto punch = settings.punchDb();
    const auto zero = plot.getCentreY();

    Path shape;
    shape.startNewSubPath (plot.getX(), zero);

    const int columns = roundToInt (plot.getWidth());
    for (int i = 0; i <= columns; i += 2)
    {
        const auto x = plot.getX() + (float) i;
        const auto lo = hzForX (x - 1.0f), hi = hzForX (x + 1.0f);
        const int k0 = jlimit (0, (int) envelope.size() - 1, (int) std::floor (lo / binHz));
        const int k1 = jlimit (k0, (int) envelope.size() - 1, (int) std::ceil (hi / binHz));

        float e = 0.0f;
        if (k1 - k0 <= 1)
        {
            const auto pos = jlimit (0.0f, (float) envelope.size() - 2.0f, (lo + hi) * 0.5f / (float) binHz);
            const auto k = (int) pos;
            e = envelope[(size_t) k] + (envelope[(size_t) k + 1] - envelope[(size_t) k]) * (pos - (float) k);
        }
        else
        {
            for (int k = k0; k <= k1; ++k)
                e = jmax (e, envelope[(size_t) k]);
        }

        shape.lineTo (x, yForChangeDb (e * punch));
    }

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

void BrumeDisplay::paintChord (Graphics& g, Rectangle<float> plot)
{
    if (settings.reso <= 0.001f)
        return;

    // Une étoile par note de l'accord, posée au bas de l'écran, qui brille quand les attaques la frappent.
    const auto glow = jlimit (0.0f, 1.0f, activity * 6.0f);

    for (int i = 0; i < numModes; ++i)
    {
        const auto x = xForHz (modeHz[(size_t) i]);
        const auto y = plot.getBottom() - 14.0f;
        const auto r = 3.0f + 2.0f * settings.reso + 2.5f * glow;

        g.setGradientFill (ColourGradient (violet.withAlpha (0.25f + 0.35f * glow), x, y, violet.withAlpha (0.0f), x, plot.getY() + plot.getHeight() * 0.35f, false));
        g.fillRect (x - 0.75f, plot.getY() + plot.getHeight() * 0.35f, 1.5f, y - (plot.getY() + plot.getHeight() * 0.35f));

        DropShadow (violet.withAlpha (0.8f), roundToInt (r * 1.6f), {}).drawForPath (g, star ({ x, y }, r));
        g.setColour (Colour (0xfff4f0ff));
        g.fillPath (star ({ x, y }, r));
    }
}

void BrumeDisplay::paintCurve (Graphics& g, Rectangle<float> plot)
{
    // La forme de la bande choisie, en couleur, sous la courbe.
    if (selected >= 0 && settings.bands[selected].on)
    {
        Path own;
        own.startNewSubPath (plot.getX(), plot.getCentreY());
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            own.lineTo (x, yForCurveDb (bb::bandDb (settings.bands[selected], hzForX (x))));
        own.lineTo (plot.getRight(), plot.getCentreY());
        own.closeSubPath();

        g.setColour (bandColour (selected).withAlpha (0.18f));
        g.fillPath (own);
    }

    Path curve;
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
    {
        const auto y = yForCurveDb (bb::sensitivityDb (settings, hzForX (x)));
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

        if (b == selected)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (Rectangle<float> (2 * (r + 5), 2 * (r + 5)).withCentre (p));
        }

        if (on)
        {
            DropShadow (colour.withAlpha (0.7f), 8, {}).drawForPath (g, [&] { Path e; e.addEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (p)); return e; }());
            g.setColour (colour);
            g.fillEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (p));
            g.setColour (Colours::white.withAlpha (0.85f));
            g.drawEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (p), 1.4f);
        }
        else
        {
            g.setColour (night);
            g.fillEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (p));
            g.setColour (colour.withAlpha (0.6f));
            g.drawEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (p), 1.4f);
        }
    }
}

void BrumeDisplay::paintReadouts (Graphics& g, Rectangle<float> plot)
{
    g.setFont (monoFont (10.0f));

    // Fréquences, sous l'écran.
    g.setColour (haze);
    for (auto [hz, text] : { std::pair { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                             { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
        g.drawText (text, Rectangle<float> (40.0f, 16.0f).withCentre ({ xForHz (hz), plot.getBottom() + 13.0f }),
                    Justification::centred, false);

    // À gauche la sensibilité, à droite ce qui est fait aux attaques.
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        g.setColour (Colour (0xffe3b5ba));
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (32.0f, 14.0f).withCentre ({ plot.getX() - 19.0f, yForCurveDb (db) }),
                    Justification::centred, false);
        const auto change = db * 2.0f;
        g.setColour ((settings.boost ? violet : ember).withAlpha (0.85f));
        g.drawText ((change > 0 ? "+" : "") + String ((int) change), Rectangle<float> (32.0f, 14.0f).withCentre ({ plot.getRight() + 19.0f, yForChangeDb (change) }),
                    Justification::centred, false);
    }

    // En haut : l'accord, et la part du spectre traitée en ce moment.
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

    if (settings.reso > 0.001f)
        pill ({ plot.getX() + 10.0f, plot.getY() + 8.0f, 170.0f, 24.0f },
              fr (kNoteNames[jlimit (0, 11, settings.root)]) + " " + kChordNames[(int) settings.chord], violet);

    pill ({ plot.getRight() - 150.0f, plot.getY() + 8.0f, 140.0f, 24.0f },
          (settings.boost ? "BOOST " : "CUT ") + String (roundToInt (activity * 100.0f)) + " %",
          settings.boost ? violet : ember);
}

void BrumeDisplay::resized()
{
    const auto plot = plotArea();
    panel->setBounds (Rectangle<int> (620, 132).withCentre ({ roundToInt (plot.getCentreX()), roundToInt (plot.getBottom() - 104.0f) })
                          .constrainedWithin (getLocalBounds().reduced (12)));
}

//==============================================================================
void BrumeDisplay::setParam (const String& id, float value)
{
    if (auto* p = processor.state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void BrumeDisplay::beginGesture (int band)
{
    for (auto* what : { "freq", "gain" })
        if (auto* p = processor.state.getParameter (BrumeBoxProcessor::bandId (band, what)))
            p->beginChangeGesture();
}

void BrumeDisplay::endGesture()
{
    if (dragging < 0)
        return;

    for (auto* what : { "freq", "gain" })
        if (auto* p = processor.state.getParameter (BrumeBoxProcessor::bandId (dragging, what)))
            p->endChangeGesture();
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
    beginGesture (node);
}

void BrumeDisplay::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0)
        return;

    const auto plot = plotArea();
    const auto x = jlimit (plot.getX(), plot.getRight(), e.position.x);
    const auto y = jlimit (plot.getY(), plot.getBottom(), e.position.y);

    setParam (BrumeBoxProcessor::bandId (dragging, "freq"), hzForX (x));
    setParam (BrumeBoxProcessor::bandId (dragging, "gain"), jlimit (-12.0f, 12.0f, curveDbForY (y)));
    settings = processor.readSettings();
    repaint();
}

void BrumeDisplay::mouseUp (const MouseEvent&)
{
    endGesture();
    dragging = -1;
}

void BrumeDisplay::mouseDoubleClick (const MouseEvent& e)
{
    const auto node = nodeAt (e.position);
    if (node < 0)
        return;

    const auto id = BrumeBoxProcessor::bandId (node, "on");
    if (auto* p = processor.state.getParameter (id))
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
    if (node < 0)
        return;

    const auto id = BrumeBoxProcessor::bandId (node, "q");
    if (auto* p = processor.state.getParameter (id))
    {
        const auto q = processor.state.getRawParameterValue (id)->load();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (jlimit (0.2f, 6.0f, q * std::pow (1.25f, wheel.deltaY * 4.0f))));
        p->endChangeGesture();
    }
}

String BrumeDisplay::descriptionAt (Point<int> at) const
{
    if (panel->isVisible() && panel->getBounds().contains (at))
        return fr ("La bande choisie : FREQ la place, SENS rend la détection plus (au-dessus de 0) ou moins (en dessous) sensible ici — à -12 dB, plus rien n'y est traité —, Q sa largeur. Le rond l'allume ou l'éteint.");

    if (nodeAt (at.toFloat()) >= 0)
        return fr ("Glissez pour la fréquence et la sensibilité, molette pour la largeur, double-clic pour l'éteindre ou la rallumer.");

    if (plotArea().contains (at.toFloat()))
        return fr ("En fond, le spectre qui entre. En rouge, ce qui est coupé des attaques (en violet s'il est poussé). Les étoiles violettes : les notes de l'accord que les attaques font sonner. La ligne blanche : où la détection travaille plus ou moins.");

    return {};
}

} // namespace onirique
