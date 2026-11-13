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

    // Cut et boost : les deux moitiés d'un même interrupteur. DEPTH suit le mode : cut depth ou boost depth.
    for (auto* b : { &cutButton, &boostButton })
        addAndMakeVisible (*b);

    modeAttachment = std::make_unique<ParameterAttachment> (*state.getParameter ("gain mode"), [this] (float value)
    {
        const bool boost = value >= 0.5f;
        cutButton.setToggleState (! boost, dontSendNotification);
        boostButton.setToggleState (boost, dontSendNotification);
        depth.attach (processor.state, boost ? "boost depth" : "cut depth");
        depth.setAccent (boost ? violet : ember);
    });
    cutButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (0.0f); };
    boostButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (1.0f); };
    addAndMakeVisible (depth);
    depth.setEnds ("0", "10");
    modeAttachment->sendInitialUpdate();

    const std::pair<Knob*, const char*> knobs[] = {
        { &sensitivity, "sensitivity" }, { &decay, "decay" }, { &sharpness, "sharpness" }, { &decayLfHf, "decay lf/hf" },
        { &link, "stereo link" }, { &balance, "stereo balance" }, { &mix, "mix" }, { &trim, "trim" },
    };

    for (const auto& [knob, id] : knobs)
    {
        knob->attach (state, id);
        addAndMakeVisible (*knob);
    }

    for (auto* k : { &decayLfHf, &balance, &trim })
        k->slider.getProperties().set ("bipolar", true);

    auto choice = [this, &state] (ComboBox& box, const String& id, const String& heading)
    {
        box.addSectionHeading (heading);
        if (auto* parameter = dynamic_cast<AudioParameterChoice*> (state.getParameter (id)))
            for (int i = 0; i < parameter->choices.size(); ++i)
                box.addItem (parameter->choices[i].toUpperCase(), i + 1);
        comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (state, id, box));
        addAndMakeVisible (box);
    };
    choice (stereoModeBox, "stereo mode", "STEREO MODE");
    choice (resolutionBox, "resolution", "RESOLUTION");
    choice (oversampleBox, "oversample", "OVERSAMPLE");
    choice (windowBox, "window length", "WINDOW");
    choice (phaseBox, "phase mode", "PHASE");

    // Les presets.
    for (auto* b : { &previousButton, &nextButton, &saveButton, &openButton })
    {
        b->getProperties().set ("plain", true);
        addAndMakeVisible (*b);
    }
    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { beginSaving(); };
    openButton.onClick = [this] { openPresetFile(); };

    presetBox.onChange = [this]
    {
        const auto index = presetBox.getSelectedId() - 1;
        if (isPositiveAndBelow (index, (int) presetList.size()))
            processor.loadPreset (presetList[(size_t) index]);
    };
    addAndMakeVisible (presetBox);

    // Le nom d'un preset à enregistrer se tape là où le nom s'affiche, sur le menu lui-même.
    presetNameEditor.getProperties().set ("box", true);
    presetNameEditor.setJustificationType (Justification::centredLeft);
    presetNameEditor.setBorderSize ({ 0, 13, 0, 30 });
    presetNameEditor.setEditable (true);
    presetNameEditor.onTextChange = [this]
    {
        if (processor.saveUserPreset (presetNameEditor.getText()).existsAsFile())
            refreshPresets();
    };
    presetNameEditor.onEditorHide = [this] { presetNameEditor.setVisible (false); };
    addChildComponent (presetNameEditor);
    refreshPresets();

    for (const auto& [button, id] : { std::pair { &deltaButton, "delta" }, { &bypassButton, "bypass" } })
    {
        button->setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, id, *button));
        addAndMakeVisible (*button);
    }

    describe ({ &previousButton }, fr ("Preset précédent."));
    describe ({ &nextButton }, fr ("Preset suivant."));
    describe ({ &presetBox }, fr ("Les presets : ceux d'usine par catégorie, puis les vôtres (Documents/Onirique/BrumeBox/Presets, rangés par sous-dossier)."));
    describe ({ &saveButton }, fr ("Enregistre les réglages comme preset personnel : tapez son nom à la place du preset affiché, puis Entrée. Échap annule."));
    describe ({ &openButton }, fr ("Ouvre un fichier .preset où qu'il soit. Les fichiers .preset de spiff s'ouvrent aussi : les paramètres portent les mêmes noms."));
    describe ({ &deltaButton }, fr ("Delta : la différence entre le son traité et l'original, pour entendre à quoi BrumeBox réagit. MIX mélange alors l'original et le delta."));
    describe ({ &bypassButton }, fr ("Bypass doux, pour comparer sans à-coup. Le calcul continue : pour l'économiser, désactivez le plugin dans l'hôte."));
    describe ({ &cutButton }, fr ("Cut : baisse les transitoires, seulement aux fréquences qui les composent."));
    describe ({ &boostButton }, fr ("Boost : pousse les transitoires, seulement aux fréquences qui les composent."));
    describe ({ &depth }, fr ("Depth : la quantité de traitement, cut et boost ayant chacun la sienne. BrumeBox ne dépend pas du niveau d'entrée : il agit selon ce réglage où qu'il soit dans la chaîne."));
    describe ({ &sensitivity }, fr ("Sensitivity : plus haut, plus de transitoires détectées ; plus bas, seulement les plus fortes. Pour l'entendre : DELTA, beaucoup de DEPTH, puis ce réglage."));
    describe ({ &decay }, fr ("Decay : le temps qu'une coupe ou un boost met à revenir. À 0, seul le tout début de la transitoire est touché ; au-dessus, la queue aussi. Sous 5 : le plus naturel."));
    describe ({ &sharpness }, fr ("Sharpness : la largeur (le q) de chaque coupe ou boost. Bas : plus naturel sur les percussions ; haut : pour les sons harmoniques et larges, comme le piano."));
    describe ({ &decayLfHf }, fr ("Decay LF/HF : sous 5, les graves reviennent plus lentement que les aigus ; au-dessus, l'inverse ; à 5, pareil partout."));
    describe ({ &stereoModeBox }, fr ("Stereo mode : traiter la gauche et la droite, ou le mid et le side."));
    describe ({ &link }, fr ("Stereo link : à 100 %, la détection se fait sur les deux canaux ensemble et la même réduction s'applique aux deux ; à 0 %, deux instances mono."));
    describe ({ &balance }, fr ("Balance : la part de traitement de chaque canal. 100/100 : les deux ; 100/0 : seulement la gauche (ou le mid) ; 0/100 : seulement la droite (ou le side)."));
    describe ({ &mix }, fr ("Mix : le son d'origine mélangé au son traité. À 100 %, seulement le traité."));
    describe ({ &trim }, fr ("Trim : le gain du son traité, pour revenir au niveau de l'original."));
    describe ({ &resolutionBox }, fr ("Resolution : la précision dans le temps. High détecte plus exactement le moment de la transitoire. Gourmand."));
    describe ({ &oversampleBox }, fr ("Oversample : la réduction calculée plus finement en fréquence, surtout dans les graves. Gourmand."));
    describe ({ &windowBox }, fr ("Window : le compromis temps / fréquence. Short : presque pas de résonance, idéal pour les clics, moins bon dans le grave. Long : plus doux, pour les sources riches. Medium : entre les deux."));
    describe ({ &phaseBox }, fr ("Phase : Linear, plus précis pour les coupes chirurgicales, peut pré-sonner dans le grave ; Minimum, sans pré-écho, souvent plus naturel pour les boosts."));

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
    // Le nom du preset chargé, même s'il vient de l'hôte ou d'un fichier.
    if (! presetNameEditor.isVisible() && presetBox.getText() != processor.getPresetName())
        presetBox.setText (processor.getPresetName(), dontSendNotification);

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
void BrumeBoxEditor::refreshPresets()
{
    presetList = processor.listPresets();
    presetBox.clear (dontSendNotification);

    String category;
    for (size_t i = 0; i < presetList.size(); ++i)
    {
        if (presetList[i].category != category)
        {
            category = presetList[i].category;
            presetBox.addSectionHeading (category);
        }
        presetBox.addItem (presetList[i].name, (int) i + 1);
    }

    presetBox.setText (processor.getPresetName(), dontSendNotification);
}

void BrumeBoxEditor::stepPreset (int direction)
{
    if (presetList.empty())
        return;

    // À partir du preset affiché ; s'il n'est pas dans la liste, depuis le début.
    int current = -1;
    for (size_t i = 0; i < presetList.size(); ++i)
        if (presetList[i].name == processor.getPresetName())
            current = (int) i;

    const auto count = (int) presetList.size();
    const auto next = current < 0 ? (direction > 0 ? 0 : count - 1) : (current + direction + count) % count;
    processor.loadPreset (presetList[(size_t) next]);
    presetBox.setText (processor.getPresetName(), dontSendNotification);
}

void BrumeBoxEditor::beginSaving()
{
    presetNameEditor.setBounds (presetBox.getBounds());
    presetNameEditor.setText (processor.getPresetName(), dontSendNotification);
    presetNameEditor.setVisible (true);
    presetNameEditor.toFront (true);
    presetNameEditor.showEditor();
    if (auto* editor = presetNameEditor.getCurrentTextEditor())
        editor->selectAll();
}

void BrumeBoxEditor::openPresetFile()
{
    chooser = std::make_unique<FileChooser> (fr ("Ouvrir un preset"), BrumeBoxProcessor::userPresetFolder(), "*.preset;*.xml");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile() && ! processor.loadPresetFile (file))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "BRUMEBOX",
                                              fr ("Ce fichier ne contient pas de réglages lisibles."));
        presetBox.setText (processor.getPresetName(), dontSendNotification);
    });
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

    // Le cramoisi de la gamme, avec un voile violet vers la droite.
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

    paintSection (g, "DETECTION", sensitivity.getY() - 10, first.getX() + 18, first.getRight() - 18);
    paintSection (g, "STEREO", stereoModeBox.getY() - 14, second.getX() + 18, second.getRight() - 18);
    paintSection (g, "OUTPUT", mix.getY() - 12, second.getX() + 18, second.getRight() - 18);
    paintSection (g, "ADVANCED", resolutionBox.getY() - 26, second.getX() + 18, second.getRight() - 18);

    // Le nom de chaque menu de l'avancé, au-dessus de lui.
    g.setColour (theme::haze);
    g.setFont (capsFont (8.5f));
    for (auto [box, name] : { std::pair { &resolutionBox, "RESOLUTION" }, { &oversampleBox, "OVERSAMPLE" },
                              { &windowBox, "WINDOW" }, { &phaseBox, "PHASE" } })
        g.drawText (name, box->getX() + 4, box->getY() - 13, box->getWidth(), 12, Justification::centredLeft, false);
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
    row.removeFromRight (18);

    openButton.setBounds (row.removeFromRight (64));
    row.removeFromRight (4);
    saveButton.setBounds (row.removeFromRight (60));
    row.removeFromRight (8);

    // Le nom prend ce qui reste entre le titre et les boutons.
    row.removeFromLeft (titleRight() + 62);
    previousButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (4);
    nextButton.setBounds (row.removeFromLeft (30));
    row.removeFromLeft (6);
    presetBox.setBounds (row);
    presetNameEditor.setBounds (row);

    // Première colonne : cut / boost, depth en grand, puis la détection.
    auto first = bounds.removeFromLeft (kColumnWidth).reduced (14, 16);

    auto mode = first.removeFromTop (30);
    cutButton.setBounds (mode.removeFromLeft (mode.getWidth() / 2).withTrimmedRight (3));
    boostButton.setBounds (mode.withTrimmedLeft (3));
    first.removeFromTop (10);

    const auto small = jlimit (100, 124, (first.getHeight() - 200) / 2);

    auto lower = first.removeFromBottom (small);
    sharpness.setBounds (lower.removeFromLeft (lower.getWidth() / 2));
    decayLfHf.setBounds (lower);
    first.removeFromBottom (8);

    auto upper = first.removeFromBottom (small);
    sensitivity.setBounds (upper.removeFromLeft (upper.getWidth() / 2));
    decay.setBounds (upper);
    first.removeFromBottom (22);

    depth.setBounds (first.withSizeKeepingCentre (jmin (first.getWidth(), 164), jmin (first.getHeight(), 186)));

    // Deuxième colonne : stéréo, sortie, avancé.
    auto second = bounds.removeFromLeft (kSecondWidth).reduced (14, 16);
    second.removeFromTop (14);

    stereoModeBox.setBounds (second.removeFromTop (28));
    second.removeFromTop (8);
    const auto rowHeight = jlimit (96, 118, (second.getHeight() - 150) / 2);
    auto stereoRow = second.removeFromTop (rowHeight);
    link.setBounds (stereoRow.removeFromLeft (stereoRow.getWidth() / 2));
    balance.setBounds (stereoRow);

    // L'avancé en bas : deux rangées de deux menus, chacun avec son nom au-dessus.
    auto advanced = second.removeFromBottom (90);
    auto advTop = advanced.removeFromTop (28 + 14).withTrimmedTop (14);
    auto advBottom = advanced.withTrimmedTop (20).removeFromTop (28);
    resolutionBox.setBounds (advTop.removeFromLeft (advTop.getWidth() / 2).withTrimmedRight (4));
    oversampleBox.setBounds (advTop.withTrimmedLeft (4));
    windowBox.setBounds (advBottom.removeFromLeft (advBottom.getWidth() / 2).withTrimmedRight (4));
    phaseBox.setBounds (advBottom.withTrimmedLeft (4));
    second.removeFromBottom (24);

    auto outputRow = second.removeFromBottom (rowHeight);
    mix.setBounds (outputRow.removeFromLeft (outputRow.getWidth() / 2));
    trim.setBounds (outputRow);

    display.setBounds (bounds.reduced (14, 12));
}

} // namespace onirique
