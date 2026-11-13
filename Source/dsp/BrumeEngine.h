// Onirique BrumeBox — moteur spectral (sans dépendance)
#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

#include "Fft.h"

namespace bb
{

constexpr int kMaxChannels = 2;
constexpr int kMaxModes = 12;

/** Les accords sur lesquels les résonateurs sont accordés. */
enum class Chord { octaves, fifths, minor9, major7, sus2, lydian };
constexpr int kNumChords = 6;

/** Les bandes de sensibilité : où la détection travaille plus, ou moins. */
constexpr int kNumBands = 5;
enum class BandShape { bell, lowShelf, highShelf };
constexpr int kNumBandShapes = 3;

struct Band
{
    bool on = true;
    float freq = 1000.0f;
    float gainDb = 0.0f;    // -12..+12 : + rend la détection plus sensible ici
    float q = 1.0f;
    BandShape shape = BandShape::bell;
};

struct Settings
{
    // Détection des transitoires, fréquence par fréquence (le principe de spiff).
    float sensitivity = 5.0f;   // 0..10 : plus haut, plus de choses comptent comme attaque
    float releaseMs = 80.0f;    // retour de chaque bin après une attaque
    float tilt = 0.0f;          // -1..1 : graves ou aigus qui retombent plus lentement

    Band bands[kNumBands];

    // Ce qu'on fait des attaques détectées.
    bool boost = false;         // couper (cut) ou pousser (boost) les attaques
    float depth = 0.0f;         // 0..10 : jusqu'à -24 dB en cut, +12 dB en boost
    float reso = 0.5f;          // 0..1 : les attaques frappent une boîte de résonateurs accordés
    float decayS = 2.0f;        // durée de résonance (T60)
    int root = 9;               // 0 = do ... 9 = la
    Chord chord = Chord::minor9;
    float brightness = 0.5f;    // 0..1 : modes aigus courts et doux, ou longs et brillants

    // La brume : une traîne spectrale de tout le son.
    float haze = 0.3f;          // 0..1 : niveau de la brume
    float hazeS = 4.0f;         // durée de la brume (T60)
    float blur = 0.5f;          // 0..1 : la phase part au hasard, le son se dissout

    float mix = 1.0f;
    float outDb = 0.0f;
    bool delta = false;         // n'écouter que ce qui change
    bool bypass = false;        // le son d'origine, latence comprise

    /** Le gain appliqué aux attaques, en dB. */
    float punchDb() const noexcept { return boost ? depth * 1.2f : -depth * 2.4f; }
};

/** La hauteur d'une bande à une fréquence, en dB. */
float bandDb (const Band&, float hz) noexcept;

/** La courbe de sensibilité, toutes bandes allumées comprises, en dB. */
float sensitivityDb (const Settings&, float hz) noexcept;

/** Sépare chaque fréquence en « attaque » et « reste », comme spiff, puis :
    - les attaques frappent des résonateurs accordés sur un accord,
    - tout le son laisse derrière lui une brume spectrale qui s'efface lentement.

    La détection compare chaque bin à sa propre moyenne récente, en dB : elle
    ne dépend pas du niveau d'entrée. */
class BrumeEngine
{
public:
    /** Alloue. Rien n'alloue après. */
    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setSettings (const Settings&) noexcept;

    int latency() const noexcept { return fftSize; }

    /** En place. */
    void process (float* const* io, int numChannels, int numSamples) noexcept;

    // Pour les tests : part moyenne des bins vus comme attaque à la dernière trame.
    float lastTransientShare() const noexcept { return transientShare; }

    // Ce que l'interface dessine, écrit une fois par trame sur le thread audio.
    int getDisplayBins() const noexcept         { return bins; }
    double getDisplayBinHz() const noexcept     { return sampleRate / (double) fftSize; }
    uint32_t getFrameCount() const noexcept     { return frameCount; }
    const float* getInputDb() const noexcept    { return visIn.data(); }
    const float* getEnvelope() const noexcept   { return envelope.data(); }
    int getNumModes() const noexcept            { return numModes; }
    float getModeHz (int i) const noexcept      { return modeHz[i]; }

private:
    void processFrame() noexcept;
    void updateModes() noexcept;
    float random() noexcept;

    struct Mode
    {
        float a1 = 0, a2 = 0, gain = 0;
        float x1[kMaxChannels] {}, x2[kMaxChannels] {};
        float y1[kMaxChannels] {}, y2[kMaxChannels] {};
        float a1R = 0; // canal droit légèrement désaccordé, pour la largeur
    };

    Settings settings;
    double sampleRate = 48000.0;
    int numCh = 2;
    int fftSize = 2048, hop = 512, mask = 2047, bins = 1025;
    int writePos = 0, hopCount = 0;
    float hopSeconds = 0.01f;
    float olaNorm = 1.0f;

    std::unique_ptr<Fft> fft;
    std::vector<float> window;
    std::vector<float> inRing[kMaxChannels], outTrans[kMaxChannels], outRest[kMaxChannels];
    std::vector<std::complex<float>> spec[kMaxChannels], work;

    // Détection, liée entre les canaux.
    std::vector<float> avgDb, novelty, envelope, releaseCoef, weightDb, visIn;
    uint32_t frameCount = 0;

    // Brume.
    std::vector<float> hazeMag[kMaxChannels], hazePhase[kMaxChannels];

    // Résonateurs.
    Mode modes[kMaxModes];
    float modeHz[kMaxModes] {};
    int numModes = 0;
    bool modesDirty = true;
    Settings modeKey;

    float transientShare = 0.0f;
    uint32_t rng = 0x9e3779b9u;
};

} // namespace bb
