// Onirique Shaper — l'écran
#include "ShaperDisplay.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    constexpr float kCurveRange = 21.0f;    // la courbe : ±21 dB d'un bord à l'autre (les bandes vont à ±18)
    constexpr float kSpectrumFloor = -96.0f;

    const char* const kBandNames[shaper::kNumBands] = { "LOW CUT", "BAND 1", "BAND 2", "BAND 3", "HIGH CUT" };
    const char* const kShortNames[shaper::kNumBands] = { "LC", "1", "2", "3", "HC" };

    const Colour pale { 244, 236, 236 };   // le trait de l'entrée, celui d'ONIRIQUE EQ

    float hzAt (int i, int points) { return 20.0f * std::pow (1000.0f, (float) i / (float) (points - 1)); }

    /** Un trait qui luit : quatre traits de plus en plus larges et pâles dessous (ONIRIQUE EQ, ui::strokeGlow). */
    void strokeGlow (Graphics& g, const Path& p, Colour c, float width, float intensity)
    {
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (c.withAlpha (jlimit (0.0f, 1.0f, intensity * 0.17f / (float) i)));
            g.strokePath (p, PathStrokeType (width * (1.0f + 1.15f * (float) i), PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    /** La forme des panneaux d'ONIRIQUE EQ : un rectangle arrondi, et une bosse douce au-dessus de [hx0, hx1]. */
    Path humpShape (Rectangle<float> body, float hx0, float hx1, float hump, float rad)
    {
        const float top = body.getY(), ht = top - hump, ease = jmin (30.0f, (hx1 - hx0) * 0.3f);
        Path p;
        p.startNewSubPath (body.getX() + rad, top);
        p.lineTo (hx0, top);
        p.cubicTo (hx0 + ease * 0.55f, top, hx0 + ease * 0.4f, ht, hx0 + ease, ht);
        p.lineTo (hx1 - ease, ht);
        p.cubicTo (hx1 - ease * 0.4f, ht, hx1 - ease * 0.55f, top, hx1, top);
        p.lineTo (body.getRight() - rad, top);
        p.quadraticTo (body.getRight(), top, body.getRight(), top + rad);
        p.lineTo (body.getRight(), body.getBottom() - rad);
        p.quadraticTo (body.getRight(), body.getBottom(), body.getRight() - rad, body.getBottom());
        p.lineTo (body.getX() + rad, body.getBottom());
        p.quadraticTo (body.getX(), body.getBottom(), body.getX(), body.getBottom() - rad);
        p.lineTo (body.getX(), top + rad);
        p.quadraticTo (body.getX(), top, body.getX() + rad, top);
        p.closeSubPath();
        return p;
    }
}

//==============================================================================
/** Marche / arrêt de la bande, le bouton d'ONIRIQUE EQ : un pictogramme, une pastille braise quand elle travaille. */
class RingButton final : public Button
{
public:
    RingButton() : Button ("on") { setClickingTogglesState (true); }

    void paintButton (Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = getToggleState();
        if (on)
        {
            g.setColour (ember.withAlpha (0.3f));
            g.fillRoundedRectangle (r, 11.0f);
            g.setColour (ember.withAlpha (0.55f));
            g.fillRoundedRectangle (r.reduced (3.0f), 9.0f);
        }
        else if (over)
        {
            g.setColour (Colours::white.withAlpha (0.10f));
            g.fillRoundedRectangle (r.reduced (3.0f), 9.0f);
        }

        Path p;
        const auto c = r.getCentre();
        const float rad = 5.2f;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.75f, MathConstants<float>::twoPi - 0.75f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.25f);
        p.lineTo (c.x, c.y - rad * 0.15f);
        g.setColour (on || over ? Colours::white : Colour (0xffd8c6c8));
        g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }
};

/** Les petits boutons du coin du panneau : bande précédente, suivante (triangles couchés), fermer (une croix). */
class GlyphButton final : public Button
{
public:
    enum Glyph { previous, next, cross };
    explicit GlyphButton (Glyph which) : Button ("glyph"), glyph (which) {}

    void paintButton (Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (over ? bone : haze);
        if (glyph == cross)
        {
            const auto q = r.withSizeKeepingCentre (9.0f, 9.0f);
            Path p;
            p.startNewSubPath (q.getTopLeft()); p.lineTo (q.getBottomRight());
            p.startNewSubPath (q.getTopRight()); p.lineTo (q.getBottomLeft());
            g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
            return;
        }
        const auto a = r.withSizeKeepingCentre (6.0f, 10.0f);
        Path p;
        if (glyph == previous) p.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY());
        else                   p.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
        g.fillPath (p);
    }

private:
    Glyph glyph;
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
class ShaperDisplay::BandPanel final : public Component
{
public:
    BandPanel (ShaperDisplay& d) : display (d)
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

        previous.onClick = [this] { display.select ((band + shaper::kNumBands - 1) % shaper::kNumBands); };
        next.onClick = [this] { display.select ((band + 1) % shaper::kNumBands); };
        close.onClick = [this] { display.select (-1); };
        for (auto* b : { &previous, &next, &close })
            addAndMakeVisible (*b);
    }

    void show (int newBand)
    {
        band = newBand;
        auto& state = display.processor.state;
        const bool cut = isCut (band);

        powerAttachment.reset();
        modeAttachment.reset();
        mode.clear (dontSendNotification);
        if (auto* p = dynamic_cast<AudioParameterChoice*> (state.getParameter (ShaperProcessor::bandId (band, "mode"))))
            for (int i = 0; i < p->choices.size(); ++i)
                mode.addItem (p->choices[i].toUpperCase(), i + 1);

        powerAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (
            state, ShaperProcessor::bandId (band, "on"), power);
        modeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (
            state, ShaperProcessor::bandId (band, "mode"), mode);

        freq.attach (state, ShaperProcessor::bandId (band, "freq"));
        sens.detach();
        q.detach();
        if (! cut)
        {
            sens.attach (state, ShaperProcessor::bandId (band, "sens"));
            q.attach (state, ShaperProcessor::bandId (band, "q"));
        }
        sens.setVisible (! cut);
        q.setVisible (! cut);

        for (auto* k : { &freq, &sens, &q })
            k->setAccent (ember);
        sens.slider.getProperties().set ("bipolar", true);

        listen.setToggleState (display.processor.listenBand.load() == band, dontSendNotification);
        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        // Le verre d'ONIRIQUE EQ (celui de VOCALBOX) : ombre douce, fond sombre, voile blanc en biais, liseré rosé.
        DropShadow (Colours::black.withAlpha (0.55f), 26, { 0, 10 }).drawForPath (g, shape);
        g.setColour (Colour (0xff0f0b0d).withAlpha (0.92f));
        g.fillPath (shape);
        const auto sb = shape.getBounds();
        ColourGradient veil (Colours::white.withAlpha (0.075f), sb.getX() + sb.getWidth() * 0.32f, sb.getY(),
                             Colours::white.withAlpha (0.035f), sb.getRight() - sb.getWidth() * 0.32f, sb.getBottom(), false);
        veil.addColour (0.4, Colours::white.withAlpha (0.02f));
        g.setGradientFill (veil);
        g.fillPath (shape);
        g.setColour (Colour (0xffffebee).withAlpha (0.12f));
        g.strokePath (shape, PathStrokeType (1.0f));

        g.setColour (mist);
        g.setFont (capsFont (10.5f));
        g.drawText (kBandNames[band], power.getRight() + 8, power.getY(), 110, power.getHeight(), Justification::centredLeft, false);

        g.setFont (monoFont (12.0f).boldened());
        g.drawText (kShortNames[band], previous.getRight(), previous.getY(), next.getX() - previous.getRight(),
                    previous.getHeight(), Justification::centred, false);
    }

    void resized() override
    {
        // Comme sur ONIRIQUE EQ : gauche (marche, forme, écoute) | FREQ | SENS sous la bosse | Q | droite (bandes, fermer).
        const auto r = getLocalBounds().toFloat();
        const float hump = 16.0f, margin = 12.0f;
        const auto body = r.reduced (margin).withTrimmedTop (hump);
        const float sideW = jmax (150.0f, body.getWidth() * 0.24f), knobW = (body.getWidth() - 2.0f * sideW) / 3.2f, mainW = knobW * 1.2f;
        const float kx = body.getX() + sideW;
        const auto freqR = Rectangle<float> (kx, body.getY() + 4.0f, knobW, body.getHeight() - 8.0f);
        const auto sensR = Rectangle<float> (kx + knobW, r.getY() + margin, mainW, body.getBottom() - r.getY() - margin - 4.0f);
        const auto qR = Rectangle<float> (kx + knobW + mainW, freqR.getY(), knobW, freqR.getHeight());
        shape = humpShape (body, sensR.getX() - 22.0f, sensR.getRight() + 22.0f, hump, 16.0f);

        auto left = body.withWidth (sideW).reduced (12.0f, 10.0f).toNearestInt();
        power.setBounds (left.removeFromTop (30).removeFromLeft (30));
        left.removeFromTop (8);
        mode.setBounds (left.removeFromTop (28));
        left.removeFromTop (8);
        listen.setBounds (left.removeFromTop (26).withWidth (96));

        auto nav = body.withLeft (body.getRight() - sideW).reduced (12.0f, 10.0f).toNearestInt().removeFromTop (28);
        close.setBounds (nav.removeFromRight (28));
        nav.removeFromRight (12);
        next.setBounds (nav.removeFromRight (22));
        previous.setBounds (nav.removeFromRight (22 + 26).removeFromLeft (22));

        if (isCut (band))
        {
            freq.setBounds (sensR.toNearestInt());
        }
        else
        {
            freq.setBounds (freqR.toNearestInt());
            sens.setBounds (sensR.toNearestInt());
            q.setBounds (qR.toNearestInt());
        }
    }

    bool hitTest (int x, int y) override { return shape.contains ((float) x, (float) y); }

    void listenChanged (int listening) { listen.setToggleState (listening == band, dontSendNotification); }

    int band = 1;

private:
    ShaperDisplay& display;

    RingButton power;
    ListenButton listen;
    ComboBox mode;
    Knob freq { "FREQ" }, sens { "SENS" }, q { "Q" };
    GlyphButton previous { GlyphButton::previous }, next { GlyphButton::next }, close { GlyphButton::cross };
    Path shape;

    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
};

//==============================================================================
Colour ShaperDisplay::bandColour (int)
{
    return ember;   // comme sur ONIRIQUE EQ : toutes les bandes de la couleur de l'interface
}

ShaperDisplay::ShaperDisplay (ShaperProcessor& p) : processor (p)
{
    panel = std::make_unique<BandPanel> (*this);
    addChildComponent (*panel);

    settings = processor.readSettings();
    startTimerHz (30);
}

ShaperDisplay::~ShaperDisplay()
{
    processor.listenBand = -1;
}

void ShaperDisplay::select (int band)
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

void ShaperDisplay::setListening (int band)
{
    processor.listenBand = band;
    panel->listenChanged (band);
    repaint();
}

void ShaperDisplay::timerCallback()
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
            outputDb.assign ((size_t) bins, kSpectrumFloor);
        }

        const float* in = engine.getInputDb();
        const float* gain = engine.getGainDb();
        float most = 0.0f;

        for (int k = 0; k < bins; ++k)
        {
            const float db = jmax (kSpectrumFloor, in[k]);
            inputDb[(size_t) k] += (db - inputDb[(size_t) k]) * (db > inputDb[(size_t) k] ? 0.7f : 0.25f);
            gainDb[(size_t) k] += (gain[k] - gainDb[(size_t) k]) * (std::abs (gain[k]) > std::abs (gainDb[(size_t) k]) ? 0.8f : 0.3f);
            outputDb[(size_t) k] = jmax (kSpectrumFloor, inputDb[(size_t) k] + gainDb[(size_t) k]);   // ce qui sort
            if (std::abs (gainDb[(size_t) k]) > std::abs (most))
                most = gainDb[(size_t) k];
        }

        deepest += (most - deepest) * (std::abs (most) > std::abs (deepest) ? 1.0f : 0.15f);
    }

    repaint();
}

//==============================================================================
Rectangle<float> ShaperDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f, 10.0f).withTrimmedRight (40.0f)
                           .withTrimmedTop (22.0f).withTrimmedBottom (14.0f);
}

float ShaperDisplay::xForHz (float hz) const
{
    const auto plot = plotArea();
    const auto t = std::log (jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return plot.getX() + t * plot.getWidth();
}

float ShaperDisplay::hzForX (float x) const
{
    const auto plot = plotArea();
    const auto t = jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float ShaperDisplay::yForCurveDb (float db) const
{
    const auto plot = plotArea();
    return plot.getCentreY() - db / kCurveRange * plot.getHeight() * 0.5f;
}

float ShaperDisplay::curveDbForY (float y) const
{
    const auto plot = plotArea();
    return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * kCurveRange;
}

float ShaperDisplay::yForSpectrumDb (float db) const
{
    const auto plot = plotArea();
    const auto t = jlimit (0.0f, 1.0f, (db - kSpectrumFloor) / -kSpectrumFloor);
    return plot.getBottom() - t * plot.getHeight();
}

Point<float> ShaperDisplay::nodePosition (int band) const
{
    const auto& b = settings.bands[band];
    return { xForHz (b.freq), isCut (band) ? yForCurveDb (0.0f) : yForCurveDb (b.sens) };
}

int ShaperDisplay::nodeAt (Point<float> p) const
{
    int best = -1;
    float bestDistance = 12.0f;

    for (int b = 0; b < shaper::kNumBands; ++b)
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

std::array<float, ShaperDisplay::kPoints> ShaperDisplay::sample (const std::vector<float>& values, bool tilt) const
{
    std::array<float, kPoints> out;
    out.fill (tilt ? kSpectrumFloor : 0.0f);
    const int last = (int) values.size() - 1;
    if (last < 1)
        return out;

    const float spread = std::pow (2.0f, 1.0f / 24.0f);   // ±1/24 d'octave : 1/12 en tout
    for (int i = 0; i < kPoints; ++i)
    {
        const float hz = hzAt (i, kPoints);
        const int k0 = jlimit (0, last, (int) std::ceil (hz / spread / (float) binHz));
        const int k1 = jlimit (0, last, (int) std::floor (hz * spread / (float) binHz));
        if (k1 <= k0)
        {
            const auto pos = jlimit (0.0f, (float) last - 1.0f, hz / (float) binHz);
            const auto k = (int) pos;
            out[(size_t) i] = values[(size_t) k] + (values[(size_t) k + 1] - values[(size_t) k]) * (pos - (float) k);
        }
        else
        {
            float sum = 0.0f;
            for (int k = k0; k <= k1; ++k)
                sum += values[(size_t) k];
            out[(size_t) i] = sum / (float) (k1 - k0 + 1);
        }
        // Une pente de +4,5 dB par octave, pour que le spectre se lise comme on l'entend.
        if (tilt)
            out[(size_t) i] += 4.5f * std::log2 (hz / 1000.0f);
    }
    return out;
}

Path ShaperDisplay::smoothPath (const std::array<float, kPoints>& ys, bool closeToBottom, float bottom) const
{
    std::array<Point<float>, kPoints> pt;
    for (int i = 0; i < kPoints; ++i)
        pt[(size_t) i] = { xForHz (hzAt (i, kPoints)), ys[(size_t) i] };

    Path p;
    p.preallocateSpace (kPoints * 7 + 8);
    if (closeToBottom) { p.startNewSubPath (pt[0].x, bottom); p.lineTo (pt[0]); }
    else p.startNewSubPath (pt[0]);
    for (int i = 0; i < kPoints - 1; ++i)
    {
        const auto& p0 = pt[(size_t) jmax (0, i - 1)];
        const auto& p1 = pt[(size_t) i];
        const auto& p2 = pt[(size_t) i + 1];
        const auto& p3 = pt[(size_t) jmin (kPoints - 1, i + 2)];
        p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }
    if (closeToBottom) { p.lineTo (pt[(size_t) kPoints - 1].x, bottom); p.closeSubPath(); }
    return p;
}

//==============================================================================
void ShaperDisplay::paint (Graphics& g)
{
    // posé sur le fond, sans cadre ; le dessin est celui de l'analyseur d'ONIRIQUE EQ
    const auto plot = plotArea();
    paintGrid (g, plot);
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        paintSpectrum (g, plot);
        paintCurve (g, plot);
    }
    paintReadouts (g, plot);
}

void ShaperDisplay::paintGrid (Graphics& g, Rectangle<float> plot)
{
    const Colour grid { (uint8) 255, (uint8) 214, (uint8) 218, 0.10f };

    // Les décades nettes, leurs divisions fines ; 20, 50, 100, 200, 500... écrits dessous.
    g.setFont (monoFont (10.0f));
    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int k = 1; k <= 9; ++k)
        {
            const float hz = decade * (float) k;
            if (hz < kMinHz || hz > kMaxHz)
                continue;
            const float x = xForHz (hz);
            const bool major = k == 1;
            g.setColour (grid.withMultipliedAlpha (major ? 0.9f : 0.32f));
            g.fillRect (Rectangle<float> (x, plot.getY() - (major ? 6.0f : 0.0f), 1.0f, plot.getHeight() + (major ? 6.0f : 0.0f)));
            if (major || k == 2 || k == 5)
            {
                g.setColour (haze.withAlpha (major ? 0.9f : 0.65f));
                g.drawText (hz >= 1000.0f ? String ((int) (hz / 1000.0f)) + "k" : String ((int) hz),
                            Rectangle<float> (x + 3.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), Justification::centredLeft, false);
            }
        }

    // La sensibilité tous les 6 dB, écrite à droite ; le 0 plus marqué.
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yForCurveDb ((float) db);
        g.setColour (db == 0 ? grid.withMultipliedAlpha (1.8f) : grid.withMultipliedAlpha (0.5f));
        g.fillRect (Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        g.setColour (haze.withAlpha (0.6f));
        g.drawText ((db > 0 ? "+" : "") + String (db) + " dB", Rectangle<float> (plot.getRight() + 4.0f, y - 6.0f, 40.0f, 12.0f),
                    Justification::centredLeft, false);
    }
}

void ShaperDisplay::paintSpectrum (Graphics& g, Rectangle<float> plot)
{
    if (inputDb.empty())
        return;

    std::array<float, kPoints> yIn, yOut;
    const auto in = sample (inputDb), out = sample (outputDb);
    for (int i = 0; i < kPoints; ++i)
    {
        yIn[(size_t) i] = yForSpectrumDb (in[(size_t) i]);
        yOut[(size_t) i] = yForSpectrumDb (out[(size_t) i]);
    }

    // La sortie en surface de la couleur de l'interface, l'entrée en trait pâle : l'écart entre les deux,
    // c'est ce que Shaper enlève (ou ajoute) en ce moment.
    g.setGradientFill (ColourGradient (ember.withAlpha (0.55f), 0.0f, plot.getY(), ember.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
    g.fillPath (smoothPath (yOut, true, plot.getBottom()));
    g.setColour (pale.withAlpha (0.35f));
    g.strokePath (smoothPath (yIn, false, 0.0f), PathStrokeType (1.6f, PathStrokeType::curved));

    // Ce qui est coupé, pendu au haut de l'écran (ce qui est poussé, monté du bas), comme sur spiff :
    // 0 au bord, 30 dB à mi-hauteur. On voit d'un coup d'œil où Shaper agit, et de combien.
    const auto gain = sample (gainDb, false);
    const float edge = settings.boost ? plot.getBottom() : plot.getY();
    std::array<float, kPoints> yGain;
    for (int i = 0; i < kPoints; ++i)
        yGain[(size_t) i] = edge + (settings.boost ? -1.0f : 1.0f) * jmin (30.0f, std::abs (gain[(size_t) i])) / 30.0f * plot.getHeight() * 0.5f;
    Path change = smoothPath (yGain, false, 0.0f);
    Path area (change);
    area.lineTo (plot.getRight(), edge);
    area.lineTo (plot.getX(), edge);
    area.closeSubPath();
    const auto lit = onirique::look::emberPink;
    g.setGradientFill (ColourGradient (lit.withAlpha (0.55f), 0.0f, edge, lit.withAlpha (0.12f), 0.0f, plot.getCentreY(), false));
    g.fillPath (area);
    g.setColour (lit);
    g.strokePath (change, PathStrokeType (1.4f, PathStrokeType::curved));
}

void ShaperDisplay::paintCurve (Graphics& g, Rectangle<float> plot)
{
    // La bande écoutée : sa zone éclairée.
    if (const int listening = processor.listenBand.load(); listening >= 0)
    {
        Path zone;
        zone.startNewSubPath (plot.getX(), plot.getBottom());
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            zone.lineTo (x, plot.getBottom() - shaper::bandCoverage (settings, listening, hzForX (x)) * plot.getHeight());
        zone.lineTo (plot.getRight(), plot.getBottom());
        zone.closeSubPath();
        g.setColour (bone.withAlpha (0.06f));
        g.fillPath (zone);
    }

    // La courbe de sensibilité, comme celle d'ONIRIQUE EQ : remplie jusqu'au 0, un trait braise qui luit.
    std::array<float, kPoints> ys;
    for (int i = 0; i < kPoints; ++i)
        ys[(size_t) i] = yForCurveDb (jmax (-kCurveRange - 2.0f, shaper::sensitivityDb (settings, hzAt (i, kPoints))));
    const auto curve = smoothPath (ys, false, 0.0f);
    Path fill (curve);
    fill.lineTo (plot.getRight(), yForCurveDb (0.0f));
    fill.lineTo (plot.getX(), yForCurveDb (0.0f));
    fill.closeSubPath();
    g.setColour (ember.withAlpha (0.12f));
    g.fillPath (fill);
    strokeGlow (g, curve, ember, 2.5f, 1.0f);
    g.setColour (ember);
    g.strokePath (curve, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));

    // La forme de la bande choisie, en voile pâle.
    if (selected >= 0 && settings.bands[selected].on)
    {
        for (int i = 0; i < kPoints; ++i)
            ys[(size_t) i] = yForCurveDb (jmax (-kCurveRange, shaper::bandDb (settings, selected, hzAt (i, kPoints))));
        Path own = smoothPath (ys, false, 0.0f);
        own.lineTo (plot.getRight(), yForCurveDb (0.0f));
        own.lineTo (plot.getX(), yForCurveDb (0.0f));
        own.closeSubPath();
        g.setColour (bone.withAlpha (0.10f));
        g.fillPath (own);
    }
}

void ShaperDisplay::paintReadouts (Graphics& g, Rectangle<float> plot)
{
    // Les points, comme sur ONIRIQUE EQ : un anneau braise qui luit, numéroté ; celui choisi, plein.
    for (int b = 0; b < shaper::kNumBands; ++b)
    {
        const auto c = nodePosition (b);
        const bool on = settings.bands[b].on;
        const float glow = (b == hovered || b == dragging) ? 1.0f : 0.0f;
        const float r = 7.0f + 2.0f * glow;
        const auto dot = Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
        Path ring;
        ring.addEllipse (dot);
        if (on)
        {
            strokeGlow (g, ring, ember, 1.4f, 0.4f + 0.6f * glow);
            g.setColour (b == selected ? ember : night);
            g.fillPath (ring);
            g.setColour (b == selected ? bone : ember);
            g.strokePath (ring, PathStrokeType (1.8f));
            g.setColour (b == selected ? night : bone);
        }
        else
        {
            g.setColour (night.withAlpha (0.7f));
            g.fillPath (ring);
            g.setColour (haze.withAlpha (0.35f + 0.4f * glow));
            g.strokePath (ring, PathStrokeType (1.2f));
            g.setColour (haze.withAlpha (0.5f + 0.4f * glow));
        }
        g.setFont (monoFont (isCut (b) ? 7.0f : 8.0f).boldened());
        g.drawText (kShortNames[b], dot, Justification::centred, false);
    }

    // La légende, en haut à gauche.
    {
        float x = plot.getX();
        const float y = plot.getY() - 20.0f;
        g.setFont (monoFont (10.0f));
        for (auto [colour, text] : { std::pair { ember, "SENSITIVITY" }, { onirique::look::emberPink, settings.boost ? "BOOST" : "CUT" },
                                         { ember.withAlpha (0.55f), "OUTPUT" }, { pale.withAlpha (0.6f), "INPUT" } })
        {
            g.setColour (colour);
            g.fillRoundedRectangle (Rectangle<float> (x, y + 4.5f, 14.0f, 3.0f), 1.5f);
            const float width = GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 4.0f;
            g.setColour (haze);
            g.drawText (text, Rectangle<float> (x + 20.0f, y, width, 12.0f), Justification::centredLeft, false);
            x += 20.0f + width + 16.0f;
        }
    }

    // En haut à droite : la plus forte réduction du moment, dans la pastille d'ONIRIQUE EQ.
    const auto pill = Rectangle<float> (140.0f, 24.0f).withPosition (plot.getRight() + 40.0f - 140.0f, plot.getY() - 24.0f);
    g.setColour (Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (pill, 12.0f);
    g.setColour (Colour ((uint8) 255, (uint8) 214, (uint8) 218, 0.10f));
    g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.2f);
    g.setColour (ember);
    g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ pill.getX() + 13.0f, pill.getCentreY() }));
    g.setColour (mist);
    g.setFont (monoFont (11.0f).boldened());
    g.drawText (String (settings.boost ? "BOOST " : "CUT ") + (deepest > 0.05f ? "+" : "") + String (deepest, 1) + " dB",
                pill.withTrimmedLeft (22.0f).withTrimmedRight (8.0f), Justification::centred, false);
}

void ShaperDisplay::resized()
{
    const auto plot = plotArea();
    const int width = jmin (700, roundToInt (plot.getWidth()) - 20), height = 180;
    panel->setBounds (Rectangle<int> (width, height).withCentre ({ roundToInt (plot.getCentreX()), roundToInt (plot.getBottom() - 6.0f - (float) height * 0.5f) })
                          .constrainedWithin (getLocalBounds()));
}

//==============================================================================
void ShaperDisplay::setParam (const String& id, float value)
{
    if (auto* p = processor.state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void ShaperDisplay::gesture (int band, bool begin)
{
    for (auto* what : { "freq", "sens", "q" })
        if (auto* p = processor.state.getParameter (ShaperProcessor::bandId (band, what)))
            begin ? p->beginChangeGesture() : p->endChangeGesture();
}

void ShaperDisplay::mouseMove (const MouseEvent& e)
{
    const auto now = nodeAt (e.position);
    if (now != hovered)
    {
        hovered = now;
        setMouseCursor (hovered >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        repaint();
    }
}

void ShaperDisplay::mouseExit (const MouseEvent&)
{
    hovered = -1;
    repaint();
}

void ShaperDisplay::mouseDown (const MouseEvent& e)
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

void ShaperDisplay::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0)
        return;

    const auto plot = plotArea();

    // Cmd (Ctrl) en glissant : la largeur, vers le haut plus étroite.
    if (e.mods.isCommandDown() && ! isCut (dragging))
    {
        const auto q = jlimit (0.1f, 10.0f, dragStartQ * std::pow (2.0f, (dragStartY - e.position.y) / 60.0f));
        setParam (ShaperProcessor::bandId (dragging, "q"), q);
    }
    else
    {
        setParam (ShaperProcessor::bandId (dragging, "freq"), hzForX (jlimit (plot.getX(), plot.getRight(), e.position.x)));
        if (! isCut (dragging))
            setParam (ShaperProcessor::bandId (dragging, "sens"),
                      jlimit (-18.0f, 18.0f, curveDbForY (jlimit (plot.getY(), plot.getBottom(), e.position.y))));
    }

    settings = processor.readSettings();
    repaint();
}

void ShaperDisplay::mouseUp (const MouseEvent&)
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

void ShaperDisplay::mouseDoubleClick (const MouseEvent& e)
{
    const auto node = nodeAt (e.position);
    if (node < 0)
        return;

    if (auto* p = processor.state.getParameter (ShaperProcessor::bandId (node, "on")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void ShaperDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    auto node = nodeAt (e.position);
    if (node < 0)
        node = selected;
    if (node < 0 || isCut (node))
        return;

    const auto id = ShaperProcessor::bandId (node, "q");
    if (auto* p = processor.state.getParameter (id))
    {
        const auto q = processor.state.getRawParameterValue (id)->load();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (jlimit (0.1f, 10.0f, q * std::pow (1.25f, wheel.deltaY * 4.0f))));
        p->endChangeGesture();
    }
}

String ShaperDisplay::descriptionAt (Point<int> at) const
{
    if (panel->isVisible() && panel->getBounds().contains (at))
        return fr ("La bande choisie : FREQ la place, SENS donne plus (au-dessus de 0) ou moins (en dessous) de traitement ici, jusqu'à rien à -18 dB, Q sa largeur. Le menu choisit sa forme (ou la pente des coupes). LISTEN fait entendre ce qui change dans sa zone. Le rond l'allume ou l'éteint.");

    if (nodeAt (at.toFloat()) >= 0)
        return fr ("Glissez pour la fréquence et la sensibilité ; Cmd (Ctrl) en glissant ou la molette pour la largeur ; Alt en glissant pour l'écouter ; double-clic pour l'éteindre ou la rallumer.");

    if (plotArea().contains (at.toFloat()))
        return fr ("En rose, pendu au haut de l'écran : ce que Shaper coupe en ce moment, fréquence par fréquence (en boost, ce qu'il pousse, monté du bas), jusqu'à 30 dB à mi-hauteur. Le trait pâle : le spectre qui entre ; la surface rouge : celui qui sort. La ligne rouge lumineuse : la courbe de sensibilité, une pondération de la détection et non un EQ.");

    return {};
}

} // namespace onirique
