// Onirique BrumeBox — l'interface
#include "PluginEditor.h"
#include "ui/OniriqueWindowSize.h"

#include <BinaryData.h>

namespace onirique
{

using namespace theme;

namespace
{
    OniriqueLookAndFeel& sharedLookAndFeel()
    {
        static OniriqueLookAndFeel laf;
        return laf;
    }

    constexpr int kHeaderHeight = 68;
    constexpr int kTitleLeft = 72;
    constexpr int kColumnWidth = 208;
    constexpr int kSecondWidth = 246;
    constexpr int kLogoWidth = 44;
    constexpr int kLogoMargin = 8;

    const Colour violet { 0xff9b8cff };

    Font titleFont (bool heavy) { return displayFont (23.0f, heavy).withExtraKerningFactor (0.09f); }

    /** L'étoile à quatre branches du sigil. */
    Path star (Point<float> centre, float radius)
    {
        Path p;
        const auto waist = radius * 0.16f;
        p.startNewSubPath (centre.x, centre.y - radius);
        p.lineTo (centre.x + waist, centre.y - waist);
        p.lineTo (centre.x + radius, centre.y);
        p.lineTo (centre.x + waist, centre.y + waist);
        p.lineTo (centre.x, centre.y + radius);
        p.lineTo (centre.x - waist, centre.y + waist);
        p.lineTo (centre.x - radius, centre.y);
        p.lineTo (centre.x - waist, centre.y - waist);
        p.closeSubPath();
        return p;
    }

    Image makeLogo()
    {
        const auto sigil = ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);
        if (! sigil.isValid())
            return {};

        const auto width = 2 * kLogoWidth;
        const auto height = width * sigil.getHeight() / sigil.getWidth();
        const auto margin = 2 * kLogoMargin;

        Image padded (Image::ARGB, width + 2 * margin, height + 2 * margin, true);
        {
            Graphics g (padded);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (sigil, Rectangle<int> (margin, margin, width, height).toFloat());
        }

        Image result (Image::ARGB, padded.getWidth(), padded.getHeight(), true);
        Graphics g (result);
        GlowEffect glow;
        glow.setGlowProperties (7.0f, ember.withAlpha (0.8f));
        glow.applyEffect (padded, g, 1.0f, 1.0f);
        return result;
    }
}

BrumeBoxEditor::BrumeBoxEditor (BrumeBoxProcessor& p)
    : AudioProcessorEditor (&p), processor (p), display (p)
{
    setLookAndFeel (&sharedLookAndFeel());
    logo = makeLogo();

    auto& state = processor.state;

    addAndMakeVisible (display);

    // Cut et boost : les deux moitiés d'un même interrupteur.
    for (auto* b : { &cutButton, &boostButton })
        addAndMakeVisible (*b);

    modeAttachment = std::make_unique<ParameterAttachment> (*state.getParameter ("boost"), [this] (float value)
    {
        cutButton.setToggleState (value < 0.5f, dontSendNotification);
        boostButton.setToggleState (value >= 0.5f, dontSendNotification);
        depth.setAccent (value >= 0.5f ? violet : ember);
    });
    cutButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (0.0f); };
    boostButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (1.0f); };
    modeAttachment->sendInitialUpdate();

    const std::pair<Knob*, const char*> knobs[] = {
        { &depth, "depth" }, { &sensitivity, "sensitivity" }, { &release, "release" }, { &tilt, "tilt" }, { &mix, "mix" },
        { &reso, "reso" }, { &decay, "decay" }, { &brightness, "brightness" },
        { &haze, "haze" }, { &hazeTime, "hazeTime" }, { &blur, "blur" },
    };

    for (const auto& [knob, id] : knobs)
    {
        knob->attach (state, id);
        addAndMakeVisible (*knob);
    }

    depth.setEnds ("0", "10");
    reso.setEnds ("0", "1");
    tilt.slider.getProperties().set ("bipolar", true);

    // Ce qui naît des attaques, en violet : les résonateurs et la brume.
    for (auto* k : { &reso, &decay, &brightness, &haze, &hazeTime, &blur })
        k->setAccent (violet);

    auto choice = [this, &state] (ComboBox& box, const String& id, const String& heading)
    {
        box.addSectionHeading (heading);
        if (auto* parameter = dynamic_cast<AudioParameterChoice*> (state.getParameter (id)))
            for (int i = 0; i < parameter->choices.size(); ++i)
                box.addItem (parameter->choices[i].toUpperCase(), i + 1);
        comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (state, id, box));
        addAndMakeVisible (box);
    };
    choice (rootBox, "root", "NOTE");
    choice (chordBox, "chord", "ACCORD");

    for (const auto& [button, id] : { std::pair { &deltaButton, "delta" }, { &bypassButton, "bypass" } })
    {
        button->setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, id, *button));
        addAndMakeVisible (*button);
    }

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, "out", output));
    addAndMakeVisible (output);

    describe ({ &deltaButton }, fr ("Delta : n'écouter que ce qui change (ce qui est coupé ou ajouté)."));
    describe ({ &bypassButton }, fr ("Bypass. La latence est gardée : la piste ne se décale pas."));
    describe ({ &cutButton }, fr ("Cut : les attaques détectées sont baissées, jusqu'à -24 dB."));
    describe ({ &boostButton }, fr ("Boost : les attaques détectées sont poussées, jusqu'à +12 dB."));
    describe ({ &depth }, fr ("Depth : combien les attaques sont coupées ou poussées. À 0, elles passent telles quelles, et ne font que frapper les résonateurs."));
    describe ({ &sensitivity }, fr ("Sensibilité : à quel point une fréquence doit sauter au-dessus de sa moyenne récente pour compter comme attaque. Plus haut, plus de choses sont prises. Ne dépend pas du niveau."));
    describe ({ &release }, fr ("Retour : le temps qu'une fréquence met à revenir après une attaque. Court, seul le claquement est touché ; long, le début de la note aussi."));
    describe ({ &tilt }, fr ("Tilt : les aigus (+) ou les graves (-) reviennent plus lentement que le reste."));
    describe ({ &mix }, fr ("Mix : la part du son traité par rapport au son d'origine."));
    describe ({ &rootBox }, fr ("Note : la fondamentale de l'accord sur lequel les résonateurs sont accordés."));
    describe ({ &chordBox }, fr ("Accord : les notes des résonateurs, répétées sur plusieurs octaves."));
    describe ({ &reso }, fr ("Réso : combien les attaques font sonner les résonateurs. Une batterie se met à chanter l'accord."));
    describe ({ &decay }, fr ("Durée : le temps que les résonateurs mettent à s'éteindre."));
    describe ({ &brightness }, fr ("Éclat : sombre, les notes aiguës de l'accord sont courtes et douces ; brillant, elles sont longues et présentes."));
    describe ({ &haze }, fr ("Brume : une traîne spectrale de tout le son, qui reste quand il s'arrête."));
    describe ({ &hazeTime }, fr ("Durée : le temps que la brume met à s'effacer."));
    describe ({ &blur }, fr ("Flou : la phase de la brume, et un peu du son, part au hasard. Le son se dissout."));
    describe ({ &output }, fr ("Niveau de sortie. Glissez vers le haut ou le bas."));

    hoverTip.setInterceptsMouseClicks (false, false);
    hoverTip.setAlwaysOnTop (true);
    addChildComponent (hoverTip);

    setResizable (true, true);
    setResizeLimits (1220, 640, 2000, 1250);
    const auto opening = window::openingSize ({}, "ONIRIQUE BRUMEBOX", { 1300, 740 }, { 1220, 640 }, { 2000, 1250 });
    setSize (opening.x, opening.y);

    licenseOverlay = std::make_unique<license::Overlay> ("ONIRIQUE BRUMEBOX", processor.license);
    addAndMakeVisible (*licenseOverlay);
    resized();

    startTimerHz (30);
}

BrumeBoxEditor::~BrumeBoxEditor()
{
    window::rememberOnThisComputer ("ONIRIQUE BRUMEBOX", getWidth(), getHeight());
    setLookAndFeel (nullptr);
}

//==============================================================================
void BrumeBoxEditor::timerCallback()
{
    repaint (getLocalBounds().removeFromTop (kHeaderHeight));
    updateHoverTip();
}

void BrumeBoxEditor::describe (std::initializer_list<Component*> components, const String& text)
{
    for (auto* c : components)
        described.push_back ({ c, text });
}

String BrumeBoxEditor::descriptionAt (Point<int> at) const
{
    for (const auto& d : described)
        if (d.component->isVisible() && d.component->getBoundsInParent().contains (at))
            return d.text;

    if (display.getBounds().contains (at))
        return display.descriptionAt (at - display.getPosition());

    return {};
}

void BrumeBoxEditor::pinDescription (Point<int> at)
{
    pinned = true;
    hoverTip.text = descriptionAt (at);
    hoverTip.setBounds (getLookAndFeel().getTooltipBounds (hoverTip.text, at, getLocalBounds().reduced (6)));
    hoverTip.setVisible (hoverTip.text.isNotEmpty());
    hoverTip.toFront (false);
}

void BrumeBoxEditor::updateHoverTip()
{
    if (pinned)
        return;

    const auto now = Time::getMillisecondCounter();
    const auto at = getMouseXYRelative();

    auto* under = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    const auto here = under != nullptr && (under == this || isParentOf (under))
                      && ! ModifierKeys::currentModifiers.isAnyMouseButtonDown();
    const auto over = here ? descriptionAt (at) : String();

    // Quitter le contrôle retire la description, et tout recommence.
    if (over != restingOn)
    {
        restingOn = over;
        restingAt = at;
        restingSince = now;
        hoverTip.setVisible (false);
        return;
    }

    if (restingOn.isEmpty() || hoverTip.isVisible())
        return;

    // Deux secondes immobile sur le même contrôle.
    if (at.getDistanceFrom (restingAt) > 3)
    {
        restingAt = at;
        restingSince = now;
        return;
    }

    if (now - restingSince < 2000)
        return;

    hoverTip.text = restingOn;
    hoverTip.setBounds (getLookAndFeel().getTooltipBounds (restingOn, at, getLocalBounds().reduced (6)));
    hoverTip.setVisible (true);
    hoverTip.toFront (false);
    hoverTip.repaint();
}

//==============================================================================
int BrumeBoxEditor::titleRight() const
{
    return kTitleLeft
           + roundToInt (GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE")
                         + GlyphArrangement::getStringWidth (titleFont (false), "BRUMEBOX"))
           + 12;
}

void BrumeBoxEditor::paintHeader (Graphics& g, Rectangle<int> header)
{
    const auto area = header.toFloat();
    const auto seconds = (Time::getMillisecondCounterHiRes() - openedAt) * 0.001;

    // Le cramoisi de la gamme, avec un voile violet vers la droite : la brume.
    ColourGradient ground (Colour (0xff2c060b), 0.0f, 0.0f, Colour (0xff24103a), area.getRight(), 0.0f, false);
    ground.addColour (0.30, Colour (0xff7a0e1c));
    ground.addColour (0.65, Colour (0xff4a0c2a));
    g.setGradientFill (ground);
    g.fillRect (area);

    g.setGradientFill (ColourGradient (Colours::transparentBlack, 0.0f, area.getY(), Colour (0x73000000), 0.0f, area.getBottom(), false));
    g.fillRect (area);

    if (logo.isValid())
    {
        const auto width = (float) logo.getWidth() * 0.5f;
        const auto height = (float) logo.getHeight() * 0.5f;
        g.drawImage (logo, { 16.0f - (float) kLogoMargin, area.getCentreY() - height * 0.5f, width, height });
    }

    const auto nameWidth = GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE");
    g.setColour (Colours::white);
    g.setFont (titleFont (true));
    g.drawText ("ONIRIQUE", area.withTrimmedLeft ((float) kTitleLeft).withWidth (nameWidth + 4.0f), Justification::centredLeft, false);
    g.setColour (Colour (0xffd9c8ff));
    g.setFont (titleFont (false));
    g.drawText ("BRUMEBOX", area.withTrimmedLeft ((float) kTitleLeft + nameWidth + 10.0f).withWidth (260.0f), Justification::centredLeft, false);

    // Des étoiles à côté, chacune respirant à son rythme.
    struct Spark { float x, y, radius, rate, offset; };
    static const Spark sparks[] = { { 10.0f, 17.0f, 5.5f, 0.9f, 0.0f }, { 27.0f, 11.0f, 3.0f, 1.3f, 1.7f },
                                    { 38.0f, 24.0f, 4.0f, 0.7f, 3.1f }, { 18.0f, 42.0f, 4.5f, 1.1f, 4.4f },
                                    { 34.0f, 48.0f, 2.5f, 1.6f, 2.3f }, { 46.0f, 38.0f, 2.0f, 1.0f, 5.2f } };
    const auto sparkLeft = (float) titleRight();
    for (const auto& s : sparks)
    {
        const auto glow = 0.5f + 0.5f * (float) std::sin (seconds * s.rate * 2.0 + s.offset);
        g.setColour (Colours::white.withAlpha (0.25f + 0.65f * glow));
        g.fillPath (star ({ sparkLeft + s.x, area.getY() + s.y }, s.radius * (0.8f + 0.2f * glow)));
    }

    // Le pied de l'en-tête : un filet, et une traînée de lumière qui le parcourt.
    const auto footY = area.getBottom() - 2.0f;
    g.setColour (Colour (0x30ffd2d6));
    g.fillRect (0.0f, footY, area.getWidth(), 1.0f);

    const auto length = jmax (160.0f, area.getWidth() * 0.22f);
    const auto cycle = 3.6;
    const auto travelled = (float) (std::fmod (seconds, cycle) / cycle);
    const auto head = -40.0f + travelled * (area.getWidth() + length + 80.0f);
    const auto tail = head - length;

    ColourGradient halo (Colours::transparentBlack, tail, 0.0f, Colours::transparentBlack, head + 14.0f, 0.0f, false);
    halo.addColour (0.82, violet.withAlpha (0.5f));
    g.setGradientFill (halo);
    g.fillRect (tail, footY - 3.0f, length + 14.0f, 7.0f);

    ColourGradient streak (Colours::transparentWhite, tail, 0.0f, Colours::transparentWhite, head + 6.0f, 0.0f, false);
    streak.addColour (0.55, emberLit.withAlpha (0.9f));
    streak.addColour (0.93, Colours::white);
    g.setGradientFill (streak);
    g.fillRoundedRectangle (tail, footY - 0.5f, length + 6.0f, 2.0f, 1.0f);
}

void BrumeBoxEditor::paintSection (Graphics& g, const String& title, int y, int left, int right)
{
    g.setColour (theme::haze);
    g.setFont (capsFont (9.5f));
    const auto width = roundToInt (GlyphArrangement::getStringWidth (capsFont (9.5f), title)) + 14;
    g.drawText (title, left, y - 7, width, 14, Justification::centredLeft, false);
    g.setColour (line);
    g.fillRect ((float) (left + width), (float) y, (float) (right - left - width), 1.0f);
}

void BrumeBoxEditor::paint (Graphics& g)
{
    g.fillAll (night);
    paintHeader (g, getLocalBounds().removeFromTop (kHeaderHeight));

    auto body = getLocalBounds().withTop (kHeaderHeight);
    const auto first = body.removeFromLeft (kColumnWidth);
    const auto second = body.removeFromLeft (kSecondWidth);

    g.setColour (deep);
    g.fillRect (first);

    // La deuxième colonne, d'un rouge qui tire vers le violet.
    g.setGradientFill (ColourGradient (Colour (0xff1a0a14), 0.0f, (float) second.getY(), Colour (0xff160a1c), 0.0f, (float) second.getBottom(), false));
    g.fillRect (second);

    g.setColour (line);
    g.drawVerticalLine (first.getRight(), (float) first.getY(), (float) first.getBottom());
    g.drawVerticalLine (second.getRight(), (float) second.getY(), (float) second.getBottom());

    // Entre trouver les attaques, et ce qu'on en fait.
    paintSection (g, fr ("DÉTECTION"), sensitivity.getY() - 10, first.getX() + 18, first.getRight() - 18);
    paintSection (g, fr ("RÉSONATEURS"), rootBox.getY() - 14, second.getX() + 18, second.getRight() - 18);
    paintSection (g, "BRUME", haze.getY() - 14, second.getX() + 18, second.getRight() - 18);
}

void BrumeBoxEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderHeight);

    auto row = header.withTrimmedBottom (6).withSizeKeepingCentre (header.getWidth(), 30).withTrimmedRight (14);
    bypassButton.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    deltaButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);

    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), 1.0f, row.removeFromRight (licenseOverlay->pillWidth()), Justification::centredRight);

    // Première colonne : cut / boost, depth en grand, puis la détection.
    auto first = bounds.removeFromLeft (kColumnWidth).reduced (14, 16);

    auto mode = first.removeFromTop (30);
    cutButton.setBounds (mode.removeFromLeft (mode.getWidth() / 2).withTrimmedRight (3));
    boostButton.setBounds (mode.withTrimmedLeft (3));
    first.removeFromTop (10);

    const auto small = jlimit (100, 124, (first.getHeight() - 200) / 2);

    auto lower = first.removeFromBottom (small);
    tilt.setBounds (lower.removeFromLeft (lower.getWidth() / 2));
    mix.setBounds (lower);
    first.removeFromBottom (8);

    auto upper = first.removeFromBottom (small);
    sensitivity.setBounds (upper.removeFromLeft (upper.getWidth() / 2));
    release.setBounds (upper);
    first.removeFromBottom (22);

    depth.setBounds (first.withSizeKeepingCentre (jmin (first.getWidth(), 164), jmin (first.getHeight(), 186)));

    // Deuxième colonne : les résonateurs, la brume, la sortie.
    auto second = bounds.removeFromLeft (kSecondWidth).reduced (12, 16);

    output.setBounds (second.removeFromBottom (32));
    second.removeFromBottom (18);
    second.removeFromTop (18);

    auto combos = second.removeFromTop (28);
    rootBox.setBounds (combos.removeFromLeft (78));
    combos.removeFromLeft (6);
    chordBox.setBounds (combos);
    second.removeFromTop (14);

    // Comme la première colonne : RÉSO en grand, puis ses deux réglages, puis la brume.
    const auto rowHeight = jlimit (100, 120, (second.getHeight() - 60) / 3);

    auto hazeRow = second.removeFromBottom (rowHeight);
    const auto third = hazeRow.getWidth() / 3;
    haze.setBounds (hazeRow.removeFromLeft (third));
    hazeTime.setBounds (hazeRow.removeFromLeft (third));
    blur.setBounds (hazeRow);
    second.removeFromBottom (30);

    auto resoRow = second.removeFromBottom (rowHeight);
    decay.setBounds (resoRow.removeFromLeft (resoRow.getWidth() / 2).withSizeKeepingCentre (third + 10, rowHeight));
    brightness.setBounds (resoRow.withSizeKeepingCentre (third + 10, rowHeight));
    second.removeFromBottom (6);

    reso.setBounds (second.withSizeKeepingCentre (jmin (second.getWidth(), 150), jmin (second.getHeight(), 170)));

    display.setBounds (bounds.reduced (14, 12));
}

} // namespace onirique
