//==============================================================================
//  brumebox_demo : la démo interactive de BrumeBox pour ONIRIQUE CENTRAL, faite sur Mac.
//
//      brumebox_demo <dossier de sortie> [images par réglage, 12 par défaut]
//
//  Le même travail qu'onirique-probe (tools/probe du dépôt ONIRIQUE CENTRAL), qui ne tourne que sous
//  Windows : chaque réglage est balayé de son minimum à son maximum, la zone de l'interface qui change est
//  celle de son bouton, et son aspect à chaque position est gardé. Seule la photo change : au lieu de
//  photographier une fenêtre, l'interface est dessinée hors écran, à sa taille de base.
//
//  Sorties : base.png (l'interface au repos) et demo.json (format onirique-demo-v1), à rattacher au plugin
//  par deploy/attach-demo.mjs. Aucun son n'est traité.
//==============================================================================
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{

/** Le temps passe (les animations avancent), et l'écran relit l'état comme le ferait son minuteur. */
void pump (onirique::BrumeBoxEditor& editor, int milliseconds)
{
    juce::Thread::sleep (milliseconds);
    editor.getDisplay().refreshNow();
}

/** L'interface, dessinée hors écran, en RVB comme les captures du probe. */
juce::Image capture (onirique::BrumeBoxEditor& editor)
{
    const auto argb = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
    juce::Image rgb (juce::Image::RGB, argb.getWidth(), argb.getHeight(), true);
    juce::Graphics (rgb).drawImageAt (argb, 0, 0);
    return rgb;
}

/** Vrai si le pixel diffère nettement entre les deux images. */
inline bool differs (const juce::uint8* a, const juce::uint8* b)
{
    return std::abs (a[0] - b[0]) > 14 || std::abs (a[1] - b[1]) > 14 || std::abs (a[2] - b[2]) > 14;
}

/** Marque dans `mask` les pixels qui changent entre deux images. */
void markChanges (const juce::Image& a, const juce::Image& b, std::vector<char>& mask)
{
    const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < da.height; ++y)
        for (int x = 0; x < da.width; ++x)
            if (differs (da.getPixelPointer (x, y), db.getPixelPointer (x, y)))
                mask[(size_t) y * (size_t) da.width + (size_t) x] = 1;
}

/** Élargit un masque d'un pixel dans toutes les directions, `radius` fois. */
void dilate (std::vector<char>& mask, int width, int height, int radius)
{
    for (int pass = 0; pass < radius; ++pass)
    {
        const auto source = mask;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (! source[(size_t) y * (size_t) width + (size_t) x])
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const int nx = x + dx, ny = y + dy;
                            if (nx >= 0 && ny >= 0 && nx < width && ny < height && source[(size_t) ny * (size_t) width + (size_t) nx])
                                mask[(size_t) y * (size_t) width + (size_t) x] = 1;
                        }
    }
}

/** Découpe un masque de pixels en îlots (le bouton, son afficheur de valeur, un écran lié...) et rend leurs rectangles. */
std::vector<juce::Rectangle<int>> islands (const std::vector<char>& changed, int width, int height)
{
    const int cell = 6; // deux zones séparées de moins de deux cellules sont réunies
    const int gw = (width + cell - 1) / cell, gh = (height + cell - 1) / cell;
    std::vector<char> grid ((size_t) gw * (size_t) gh, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (changed[(size_t) y * (size_t) width + (size_t) x])
                grid[(size_t) (y / cell) * (size_t) gw + (size_t) (x / cell)] = 1;

    auto joined = grid;
    dilate (joined, gw, gh, 1);

    std::vector<int> label ((size_t) gw * (size_t) gh, -1);
    std::vector<juce::Rectangle<int>> result;
    for (int sy = 0; sy < gh; ++sy)
        for (int sx = 0; sx < gw; ++sx)
        {
            const auto seed = (size_t) sy * (size_t) gw + (size_t) sx;
            if (! joined[seed] || label[seed] >= 0)
                continue;

            int left = gw, top = gh, right = -1, bottom = -1;
            std::vector<std::pair<int, int>> stack { { sx, sy } };
            label[seed] = (int) result.size();
            while (! stack.empty())
            {
                const auto [x, y] = stack.back();
                stack.pop_back();
                if (grid[(size_t) y * (size_t) gw + (size_t) x])
                {
                    left = std::min (left, x);  right = std::max (right, x);
                    top = std::min (top, y);    bottom = std::max (bottom, y);
                }
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= gw || ny >= gh)
                            continue;
                        const auto next = (size_t) ny * (size_t) gw + (size_t) nx;
                        if (joined[next] && label[next] < 0)
                        {
                            label[next] = (int) result.size();
                            stack.push_back ({ nx, ny });
                        }
                    }
            }

            if (right >= 0)
                result.push_back (juce::Rectangle<int> (left * cell, top * cell, (right - left + 1) * cell, (bottom - top + 1) * cell)
                                      .expanded (1).getIntersection ({ 0, 0, width, height }));
            else
                result.push_back ({});
        }

    result.erase (std::remove_if (result.begin(), result.end(), [] (const auto& r) { return r.getWidth() * r.getHeight() < 60; }), result.end());
    return result;
}

/** Une image en JPEG : pour les images entières de l'interface, qu'un PNG rendrait trop lourdes. */
juce::String toJpegDataUrl (const juce::Image& image)
{
    juce::MemoryOutputStream jpeg;
    juce::JPEGImageFormat format;
    format.setQuality (0.84f);
    format.writeImageToStream (image, jpeg);
    return "data:image/jpeg;base64," + juce::Base64::toBase64 (jpeg.getData(), jpeg.getDataSize());
}

juce::String toDataUrl (const juce::Image& image)
{
    juce::MemoryOutputStream png;
    juce::PNGImageFormat().writeImageToStream (image, png);
    return "data:image/png;base64," + juce::Base64::toBase64 (png.getData(), png.getDataSize());
}

void restoreDefaults (juce::AudioProcessor& plugin, onirique::BrumeBoxEditor& editor)
{
    for (auto* parameter : plugin.getParameters())
        if (std::abs (parameter->getValue() - parameter->getDefaultValue()) > 1.0e-6f)
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
    editor.getDisplay().refreshNow();
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage : brumebox_demo <dossier de sortie> [images par reglage]" << std::endl;
        return 2;
    }

    const juce::File outDir (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])));
    const int framesPerKnob = juce::jlimit (4, 32, argc > 2 ? std::atoi (argv[2]) : 12);
    const int settleMs = 20;

    juce::ScopedJuceInitialiser_GUI gui;

    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    plugin->setPlayConfigDetails (2, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);

    std::unique_ptr<onirique::BrumeBoxEditor> editor (dynamic_cast<onirique::BrumeBoxEditor*> (plugin->createEditorIfNeeded()));
    if (editor == nullptr)
    {
        std::cout << "ERREUR pas d'interface" << std::endl;
        return 1;
    }
    editor->setSize (1300, 740);
    editor->setOpaque (true);

    // L'interface au repos, et ce qui bouge tout seul (vumètres, animations) pour ne pas le confondre avec un bouton.
    restoreDefaults (*plugin, *editor);
    pump (*editor, 400);
    const juce::Image base = capture (*editor);
    if (! base.isValid())
    {
        std::cout << "ERREUR capture de l'interface impossible" << std::endl;
        return 1;
    }

    const int width = base.getWidth(), height = base.getHeight();
    std::vector<char> noisy ((size_t) width * (size_t) height, 0);
    for (int i = 0; i < 30; ++i)
    {
        pump (*editor, 200);
        markChanges (base, capture (*editor), noisy);
    }
    // Ce qui bouge vraiment tout seul, au pixel près : la vérification finale ne tient compte que de cela.
    std::vector<char> jitter = noisy;
    dilate (jitter, width, height, 1);
    dilate (noisy, width, height, 3);

    // Un écran animé (réticule qui tourne, étincelles) ne passe pas partout en six secondes : chaque îlot
    // animé est donc rempli en entier, sauf s'il couvrait une grande part de l'interface.
    for (const auto& zone : islands (noisy, width, height))
        if ((double) zone.getWidth() * (double) zone.getHeight() < 0.3 * (double) width * (double) height)
            for (int y = zone.getY(); y < zone.getBottom(); ++y)
                for (int x = zone.getX(); x < zone.getRight(); ++x)
                    noisy[(size_t) y * (size_t) width + (size_t) x] = 1;

    {
        juce::Image::BitmapData data (base, juce::Image::BitmapData::readOnly);
        bool blank = true;
        const auto* first = data.getPixelPointer (0, 0);
        for (int y = 0; y < height && blank; y += 7)
            for (int x = 0; x < width && blank; x += 7)
                blank = ! differs (first, data.getPixelPointer (x, y));
        if (blank)
        {
            std::cout << "ERREUR l'interface capturee est vide" << std::endl;
            return 1;
        }
    }

    juce::Array<juce::var> controls;
    juce::StringArray hidden, tooWide, whole;
    const auto& parameters = plugin->getParameters();
    const double screenArea = (double) width * (double) height;

    for (int index = 0; index < parameters.size(); ++index)
    {
        auto* parameter = parameters[index];
        const juce::String name = parameter->getName (64);
        std::cout << "PROGRESSION " << (index + 1) << "/" << parameters.size() << " " << name << std::endl;

        if (! parameter->isAutomatable() || parameter == plugin->getBypassParameter())
            continue;

        const bool discrete = parameter->isDiscrete() && parameter->getNumSteps() >= 2 && parameter->getNumSteps() <= 24;
        const int count = discrete ? parameter->getNumSteps() : framesPerKnob;
        auto valueAt = [count] (int k) { return (float) k / (float) (count - 1); };

        auto shoot = [&] (int k)
        {
            parameter->setValueNotifyingHost (valueAt (k));
            pump (*editor, settleMs);
            return capture (*editor);
        };

        // Trois positions d'abord : si rien ne bouge, ce réglage n'a pas de bouton à l'écran.
        std::vector<juce::Image> frames ((size_t) count);
        std::vector<char> changed ((size_t) width * (size_t) height, 0);
        for (int k : { 0, count - 1, count / 2 })
            if (! frames[(size_t) k].isValid())
            {
                frames[(size_t) k] = shoot (k);
                markChanges (base, frames[(size_t) k], changed);
            }

        auto anyChange = [&]
        {
            for (size_t i = 0; i < changed.size(); ++i)
                if (changed[i] && ! noisy[i])
                    return true;
            return false;
        };

        const bool visible = anyChange();
        if (visible)
            for (int k = 0; k < count; ++k)
                if (! frames[(size_t) k].isValid())
                {
                    frames[(size_t) k] = shoot (k);
                    markChanges (base, frames[(size_t) k], changed);
                }

        juce::StringArray texts;
        for (int k = 0; k < count; ++k)
        {
            // L'unité, sauf si la valeur l'écrit déjà (« 82 % », « 2.82 kHz »).
            const juce::String value = parameter->getText (valueAt (k), 64).trim(), unit = parameter->getLabel().trim();
            texts.add (unit.isEmpty() || value.endsWithIgnoreCase (unit) ? value : value + " " + unit);
        }

        // Retour au repos. Ce qui diffère encore de l'image de départ bouge tout seul : on l'ajoute aux zones animées.
        restoreDefaults (*plugin, *editor);
        pump (*editor, settleMs + 60);
        {
            std::vector<char> drift ((size_t) width * (size_t) height, 0);
            markChanges (base, capture (*editor), drift);
            dilate (drift, width, height, 3);
            for (size_t i = 0; i < drift.size(); ++i)
            {
                noisy[i] = (char) (noisy[i] | drift[i]);
                jitter[i] = (char) (jitter[i] | drift[i]);
            }
        }

        // Les zones du réglage : ce qui a changé, hors des zones animées (vumètres, personnage, réticule).
        const std::vector<char> rawChanged = changed;
        for (size_t i = 0; i < changed.size(); ++i)
            if (noisy[i])
                changed[i] = 0;
        auto areas = visible ? islands (changed, width, height) : std::vector<juce::Rectangle<int>>();
        if (areas.empty())
        {
            hidden.add (name);
            continue;
        }

        const int defaultIndex = juce::jlimit (0, count - 1, juce::roundToInt (parameter->getDefaultValue() * (float) (count - 1)));

        // Vérification : pour chaque position, l'interface que le site recomposera (l'image au repos, et les zones
        // de ce réglage prises dans l'image de cette position) doit être la vraie image, à ce qui bouge tout seul
        // près. Rend le nombre de pixels faux de la pire position.
        auto wrongPixels = [&] (const std::vector<juce::Rectangle<int>>& zones)
        {
            std::vector<char> inside ((size_t) width * (size_t) height, 0);
            for (const auto& area : zones)
                for (int y = area.getY(); y < area.getBottom(); ++y)
                    for (int x = area.getX(); x < area.getRight(); ++x)
                        inside[(size_t) y * (size_t) width + (size_t) x] = 1;
            const juce::Image::BitmapData db (base, juce::Image::BitmapData::readOnly);
            int worst = 0;
            for (int k = 0; k < count; ++k)
            {
                const juce::Image::BitmapData dk (frames[(size_t) k], juce::Image::BitmapData::readOnly);
                int wrong = 0;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const auto i = (size_t) y * (size_t) width + (size_t) x;
                        if (! inside[i] && ! jitter[i] && differs (db.getPixelPointer (x, y), dk.getPixelPointer (x, y)))
                            ++wrong;
                    }
                worst = std::max (worst, wrong);
            }
            return worst;
        };
        const int tolerance = std::max (120, (int) (0.0005 * screenArea));
        int worst = wrongPixels (areas);
        if (worst > tolerance)
        {
            // Le réglage change aussi dans une zone animée (un afficheur qui suit le bouton) : ses zones la prennent
            // avec lui, et on revérifie.
            std::vector<char> wider = rawChanged;
            for (size_t i = 0; i < wider.size(); ++i)
                if (jitter[i])
                    wider[i] = 0;
            const auto widerAreas = islands (wider, width, height);
            const int widerWorst = widerAreas.empty() ? worst : wrongPixels (widerAreas);
            if (widerWorst <= tolerance) { areas = widerAreas; worst = widerWorst; }
        }
        // Quelques pixels de plus autour de chaque zone : le bord d'une courbe qui se déplace (son trait, son
        // anticrénelage, une ligne animée posée dessus) ne reste pas visible depuis l'image au repos.
        // Et tant que ce qui change vraiment (même dans une zone animée) est à moins de 8 pixels d'un bord, ce bord
        // recule, de 16 pixels au plus.
        auto touches = [&] (int x0, int y0, int x1, int y1)
        {
            for (int y = std::max (0, y0); y < std::min (height, y1); ++y)
                for (int x = std::max (0, x0); x < std::min (width, x1); ++x)
                    if (rawChanged[(size_t) y * (size_t) width + (size_t) x])
                        return true;
            return false;
        };
        for (auto& area : areas)
        {
            for (int step = 0; step < 16; ++step)
            {
                bool grown = false;
                if (area.getY() > 0 && touches (area.getX(), area.getY() - 8, area.getRight(), area.getY())) { area.setTop (area.getY() - 1); grown = true; }
                if (area.getBottom() < height && touches (area.getX(), area.getBottom(), area.getRight(), area.getBottom() + 8)) { area.setBottom (area.getBottom() + 1); grown = true; }
                if (area.getX() > 0 && touches (area.getX() - 8, area.getY(), area.getX(), area.getBottom())) { area.setLeft (area.getX() - 1); grown = true; }
                if (area.getRight() < width && touches (area.getRight(), area.getY(), area.getRight() + 8, area.getBottom())) { area.setRight (area.getRight() + 1); grown = true; }
                if (! grown) break;
            }
            area = area.expanded (6).getIntersection ({ 0, 0, width, height });
        }
        const bool faithful = worst <= tolerance;
        if (! faithful)
            std::cout << "VERIFICATION " << name << " : " << worst << " pixels faux hors de ses zones, garde en images entieres" << std::endl;

        // Un réglage qui ne se rejoue pas fidèlement par morceaux, ou qui redessine une grande partie de l'interface
        // (couleur, mode, page) : ses positions sont gardées en images entières, et le site le propose sous
        // l'interface, comme un sélecteur. Six au plus, et pas plus de douze positions chacun (six pour un continu).
        double total = 0.0;
        for (const auto& area : areas)
            total += (double) area.getWidth() * (double) area.getHeight();
        const bool global = ! faithful || total > 0.45 * screenArea || areas.size() > 12;
        if (global)
        {
            if (whole.size() >= 6 || (discrete && count > 12))
            {
                tooWide.add (name);
                continue;
            }
            std::vector<int> picks;
            if (discrete)
                for (int k = 0; k < count; ++k) picks.push_back (k);
            else
                for (int k = 0; k < 6; ++k) picks.push_back (juce::roundToInt ((double) k * (count - 1) / 5.0));

            juce::Image strip (juce::Image::RGB, width, height * (int) picks.size(), false);
            juce::StringArray pickedTexts;
            int start = 0;
            {
                juce::Graphics g (strip);
                for (size_t i = 0; i < picks.size(); ++i)
                {
                    g.drawImageAt (frames[(size_t) picks[i]], 0, (int) i * height);
                    pickedTexts.add (texts[picks[i]]);
                    if (std::abs (picks[i] - defaultIndex) < std::abs (picks[(size_t) start] - defaultIndex))
                        start = (int) i;
                }
            }
            auto* part = new juce::DynamicObject();
            part->setProperty ("x", 0.0);
            part->setProperty ("y", 0.0);
            part->setProperty ("w", 1.0);
            part->setProperty ("h", 1.0);
            part->setProperty ("sprite", toJpegDataUrl (strip));
            juce::Array<juce::var> parts;
            parts.add (juce::var (part));

            auto* control = new juce::DynamicObject();
            control->setProperty ("name", name);
            control->setProperty ("frames", (int) picks.size());
            control->setProperty ("discrete", true);
            control->setProperty ("global", true);
            control->setProperty ("start", start);
            control->setProperty ("texts", juce::var (pickedTexts));
            control->setProperty ("parts", juce::var (parts));
            controls.add (juce::var (control));
            whole.add (name);
            continue;
        }

        juce::Array<juce::var> parts;
        for (const auto& area : areas)
        {
            juce::Image strip (juce::Image::RGB, area.getWidth(), area.getHeight() * count, false);
            {
                juce::Graphics g (strip);
                for (int k = 0; k < count; ++k)
                    g.drawImageAt (frames[(size_t) k].getClippedImage (area), 0, k * area.getHeight());
            }

            auto* part = new juce::DynamicObject();
            part->setProperty ("x", (double) area.getX() / width);
            part->setProperty ("y", (double) area.getY() / height);
            part->setProperty ("w", (double) area.getWidth() / width);
            part->setProperty ("h", (double) area.getHeight() / height);
            part->setProperty ("sprite", toDataUrl (strip));
            parts.add (juce::var (part));
        }

        auto* control = new juce::DynamicObject();
        control->setProperty ("name", name);
        control->setProperty ("frames", count);
        control->setProperty ("discrete", discrete);
        control->setProperty ("start", defaultIndex);
        control->setProperty ("texts", juce::var (texts));
        control->setProperty ("parts", juce::var (parts));
        controls.add (juce::var (control));
    }

    outDir.createDirectory();

    juce::FileOutputStream basePng (outDir.getChildFile ("base.png"));
    basePng.setPosition (0);
    basePng.truncate();
    juce::PNGImageFormat().writeImageToStream (base, basePng);
    basePng.flush();

    auto* demo = new juce::DynamicObject();
    demo->setProperty ("format", "onirique-demo-v1");
    demo->setProperty ("plugin", JucePlugin_Name);
    demo->setProperty ("pluginVersion", JucePlugin_VersionString);
    demo->setProperty ("width", width);
    demo->setProperty ("height", height);
    demo->setProperty ("base", toDataUrl (base));
    demo->setProperty ("controls", juce::var (controls));
    outDir.getChildFile ("demo.json").replaceWithText (juce::JSON::toString (juce::var (demo), true));

    std::cout << "RESULTAT " << controls.size() << " boutons enregistres, " << hidden.size() << " reglages sans bouton visible, "
              << whole.size() << " en images entieres, " << tooWide.size() << " ecartes, image " << width << "x" << height << std::endl;
    if (! hidden.isEmpty())  std::cout << "  sans bouton : " << hidden.joinIntoString (", ") << std::endl;
    if (! whole.isEmpty())   std::cout << "  images entieres : " << whole.joinIntoString (", ") << std::endl;
    if (! tooWide.isEmpty()) std::cout << "  ecartes : " << tooWide.joinIntoString (", ") << std::endl;

    editor.reset();
    plugin.reset();
    return 0;
}
