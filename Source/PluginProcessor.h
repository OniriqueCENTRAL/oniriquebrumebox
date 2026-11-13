// Onirique BrumeBox — le plugin
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

#include "dsp/BrumeEngine.h"
#include "License/LicenseGuard.h"

namespace onirique
{
using namespace juce;

class BrumeBoxProcessor : public AudioProcessor
{
public:
    BrumeBoxProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;
    using AudioProcessor::processBlock;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Les réglages tels que les paramètres les donnent (aussi pour l'écran). */
    bb::Settings readSettings() const;

    const bb::BrumeEngine& getEngine() const noexcept { return engine; }

    /** Les identifiants des paramètres : les mêmes noms que spiff (« band2 freq », « decay lf/hf »...). */
    static String bandId (int band, const char* what) { return "band" + String (band) + " " + what; }

    /** La bande écoutée, ou -1. Un état de l'interface, pas un paramètre (comme dans spiff). */
    std::atomic<int> listenBand { -1 };

    //==============================================================================
    // Presets : ceux d'usine (Presets.h), et ceux de l'utilisateur, des fichiers .preset dans
    // Documents/Onirique/BrumeBox/Presets. Un fichier .preset ouvert à la main peut aussi venir
    // d'ailleurs : tout fichier XML dont les <PARAM id="..." value="..."/> portent nos identifiants.
    struct Preset
    {
        String category, name;
        int factoryIndex = -1;   // -1 : un fichier
        File file;
    };

    static File userPresetFolder();
    std::vector<Preset> listPresets() const;

    void loadFactoryPreset (int index);
    bool loadPresetFile (const File&);
    void loadPreset (const Preset&);

    /** Enregistre l'état en cours sous ce nom dans le dossier utilisateur. Rend le fichier, ou rien. */
    File saveUserPreset (const String& name);

    /** Le nom du preset chargé en dernier (gardé avec le projet). */
    String getPresetName() const { return state.state.getProperty ("presetName", String::fromUTF8 ("Par défaut")).toString(); }

    AudioProcessorValueTreeState state;

    // Licence ONIRIQUE : fiche « brumebox » sur CENTRAL, réservée aux fondateurs.
    license::Guard license { "brumebox" };

private:
    static AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Chaque paramètre à sa valeur dans `values` (identifiant → valeur réelle), sinon à sa valeur par défaut. */
    void applyValues (const std::map<String, float>& values, const String& presetName);

    bb::BrumeEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrumeBoxProcessor)
};

} // namespace onirique
