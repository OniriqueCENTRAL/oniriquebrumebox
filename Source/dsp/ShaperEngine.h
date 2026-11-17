// Onirique Shaper — moteur : processeur de transitoires spectral (sans dépendance)
#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

#include "Fft.h"

namespace shaper
{

constexpr int kMaxChannels = 2;
constexpr int kNumBands = 5;          // 0 : coupe-bas, 1 à 3 : bandes, 4 : coupe-haut

/** Formes des bandes 1 à 3. */
enum class BandMode { lowShelf, bell, highShelf, tilt };
constexpr int kNumBandModes = 4;

/** Pentes des coupe-bas et coupe-haut (bandes 0 et 4), en dB par octave. */
constexpr float kCutSlopes[4] = { 6.0f, 12.0f, 24.0f, 48.0f };

enum class StereoMode { leftRight, midSide };
enum class Window { shortWindow, mediumWindow, longWindow };
enum class Phase { linear, minimum };

struct Band
{
    bool on = true;
    float freq = 1000.0f;
    float sens = 0.0f;      // -18..+18 dB : plus (ou moins) de traitement ici
    float q = 0.7f;
    int mode = 1;           // bandes 1 à 3 : BandMode ; bandes 0 et 4 : indice de kCutSlopes
};

struct Settings
{
    bool boost = false;         // cut ou boost
    float cutDepth = 5.0f;      // 0..10
    float boostDepth = 5.0f;    // 0..10
    float sensitivity = 5.0f;   // 0..10 : plus haut, plus de transitoires détectées
    float sharpness = 5.0f;     // 0..10 : largeur (q) de chaque coupe ou boost, de large à étroit
    float decay = 5.0f;         // 0..10 : temps de retour de chaque coupe ou boost
    float decayLfHf = 5.0f;     // 0..10 : 5 = égal ; < 5 graves plus longs ; > 5 aigus plus longs

    Band bands[kNumBands];

    StereoMode stereoMode = StereoMode::leftRight;
    float stereoLink = 100.0f;  // 0..100 %
    float balance = 0.5f;       // 0 = 100/0 (gauche ou mid seul), 0,5 = 100/100, 1 = 0/100

    bool highResolution = false;
    int oversample = 0;         // 0, 1, 2 : 1x, 2x, 4x
    Window window = Window::mediumWindow;
    Phase phase = Phase::linear;

    float mix = 100.0f;         // %
    float trimDb = 0.0f;        // gain du signal traité seulement
    bool delta = false;
    bool bypass = false;

    int listenBand = -1;        // écoute d'une bande (pas un paramètre : un état de l'interface)

    float depth() const noexcept { return boost ? boostDepth : cutDepth; }
    /** Le gain maximal appliqué, en dB : jusqu'à -30 dB en cut, +15 dB en boost. */
    float rangeDb() const noexcept { return boost ? boostDepth * 1.6f : -cutDepth * 2.0f; }   // calé sur spiff
};

Settings defaultSettings();

/** La courbe de sensibilité à une fréquence, en dB (bandes 1 à 3, coupe-bas et coupe-haut compris),
    telle que l'écran la dessine. */
float sensitivityDb (const Settings&, float hz) noexcept;

/** La forme d'une bande seule, en dB (pour l'écran). */
float bandDb (const Settings&, int band, float hz) noexcept;

/** La part d'une fréquence qu'une bande « couvre », de 0 à 1 : ce qu'on entend en l'écoutant. */
float bandCoverage (const Settings&, int band, float hz) noexcept;

/** Repère, fréquence par fréquence, ce qui monte d'un coup au-dessus de sa propre moyenne
    récente (indépendant du niveau), et le coupe ou le pousse, sans toucher au reste. */
class ShaperEngine
{
public:
    /** Alloue tout ce que les fenêtres, l'oversample et la résolution peuvent demander. */
    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setSettings (const Settings&) noexcept;

    /** La latence de la configuration en cours (elle suit la fenêtre et l'oversample). */
    int latency() const noexcept { return latencySamples; }
    static int latencyFor (double sampleRate, Window, int oversample) noexcept;

    void process (float* const* io, int numChannels, int numSamples) noexcept;

    // Tests
    float lastTransientShare() const noexcept { return transientShare; }

    // Écran : écrit une fois par trame sur le thread audio.
    int getDisplayBins() const noexcept         { return bins; }
    double getDisplayBinHz() const noexcept     { return sampleRate / (double) fftSize; }
    uint32_t getFrameCount() const noexcept     { return frameCount; }
    const float* getInputDb() const noexcept    { return visIn.data(); }
    const float* getGainDb() const noexcept     { return visGain.data(); }

private:
    void configure() noexcept;
    void processFrame() noexcept;
    void updateWeights() noexcept;
    void smoothAcross (const float* in, float* out) noexcept;
    void minimumPhase (const float* gain, std::complex<float>* out) noexcept;
    void detect() noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    int numCh = 2;
    int srScale = 1;

    // Configuration en cours.
    Window activeWindow = Window::mediumWindow;
    int activeOversample = -1;
    bool activeHighRes = false;
    int frameLen = 2048, fftSize = 2048, hop = 512, bins = 1025, pad = 0;
    int latencySamples = 2048;
    float hopSeconds = 0.01f, olaNorm = 1.0f;
    bool synthesisWindow = true;
    bool weightsDirty = true;

    std::vector<std::unique_ptr<Fft>> ffts;     // une par taille possible
    const Fft* fft = nullptr;

    int ringSize = 0, ringMask = 0, writePos = 0, hopCount = 0;
    std::vector<float> window;
    std::vector<float> inRing[kMaxChannels], outRing[kMaxChannels];
    std::vector<std::complex<float>> spec[kMaxChannels], work, cep;

    // Détection : une par canal, et une liée.
    std::vector<float> novelty[kMaxChannels + 1];

    // La détection fine, comme spiff : seul ce qui monte vite compte. Des trames courtes (5 ms, tous les 1,3 ms),
    // en tiers d'octave ; chaque bande est comparée à son propre niveau des 5 à 30 ms d'avant. Un clic y monte de
    // 30 dB d'un coup ; une voix tenue, un vibrato, une syllabe qui s'ouvre, de quelques dB seulement.
    static constexpr int kDetHistory = 32, kDetEvents = 96;
    int detN = 256, detHop = 64, detBands = 0, detCount = 0;
    int64_t samplesIn = 0, detEnd = 0;
    std::unique_ptr<Fft> detFft;
    std::vector<std::complex<float>> detWork[kMaxChannels];
    std::vector<float> detWindow, detBandHz, detPower, bandNovelty;
    std::vector<int> detLo, detHi;
    std::vector<float> detLevel[kMaxChannels + 1], detNovelty[kMaxChannels + 1];   // anneaux : trames × bandes
    std::vector<int64_t> detTime;
    std::vector<float> binBand;   // pour chaque bin de la grande FFT, sa place (fractionnaire) parmi les bandes
    std::vector<float> target[kMaxChannels], envelope[kMaxChannels], gainDb[kMaxChannels];
    std::vector<float> releaseCoef, amount, sensShift, coverage, scratch, prefix;
    std::vector<int> halfWidth;
    Settings weightKey;

    std::vector<float> visIn, visGain;
    uint32_t frameCount = 0;
    float transientShare = 0.0f;
};

} // namespace shaper
