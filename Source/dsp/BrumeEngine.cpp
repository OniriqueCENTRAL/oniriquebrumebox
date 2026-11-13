// Onirique BrumeBox — moteur spectral
#include "BrumeEngine.h"

#include <algorithm>
#include <cmath>

namespace bb
{

namespace
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 6.28318530717959f;
    constexpr float kLn1000 = 6.90775527898f;   // T60 : -60 dB

    float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    // Les intervalles de chaque accord, en demi-tons au-dessus de la fondamentale.
    // Ils sont répétés à l'octave jusqu'à remplir les modes.
    const std::vector<int>& chordIntervals (Chord c)
    {
        static const std::vector<int> tables[kNumChords] = {
            { 0 },                 // octaves
            { 0, 7 },              // quintes
            { 0, 3, 7, 10, 14 },   // mineur 9
            { 0, 4, 7, 11 },       // majeur 7
            { 0, 2, 7 },           // sus2
            { 0, 4, 6, 7, 11 },    // lydien
        };
        return tables[std::clamp ((int) c, 0, kNumChords - 1)];
    }
}

float bandDb (const Band& b, float hz) noexcept
{
    if (! b.on || b.gainDb == 0.0f)
        return 0.0f;

    const float octaves = std::log2 (std::max (1.0f, hz) / std::max (1.0f, b.freq));
    const float q = std::clamp (b.q, 0.1f, 10.0f);

    switch (b.shape)
    {
        case BandShape::lowShelf:  return b.gainDb / (1.0f + std::exp (octaves * 4.0f * q));
        case BandShape::highShelf: return b.gainDb / (1.0f + std::exp (-octaves * 4.0f * q));
        case BandShape::bell:
        default:
        {
            const float width = 0.5f / q;   // en octaves
            return b.gainDb * std::exp (-0.5f * (octaves * octaves) / (width * width));
        }
    }
}

float sensitivityDb (const Settings& s, float hz) noexcept
{
    float db = 0.0f;
    for (const auto& b : s.bands)
        db += bandDb (b, hz);
    return db;
}

void BrumeEngine::prepare (double sr, int channels)
{
    sampleRate = sr;
    numCh = std::clamp (channels, 1, kMaxChannels);

    // Même durée de trame en secondes quelle que soit la fréquence d'échantillonnage.
    fftSize = sr >= 176400.0 ? 8192 : (sr >= 88200.0 ? 4096 : 2048);
    hop = fftSize / 4;
    mask = fftSize - 1;
    bins = fftSize / 2 + 1;
    hopSeconds = (float) (hop / sr);

    fft = std::make_unique<Fft> (fftSize);

    window.resize ((size_t) fftSize);
    double sumSq = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) fftSize);
        sumSq += (double) window[(size_t) i] * window[(size_t) i];
    }
    // Hann en analyse et en synthèse : la somme des carrés recouverts vaut sumSq / hop.
    olaNorm = (float) (hop / sumSq);

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        inRing[ch].assign ((size_t) fftSize, 0.0f);
        outTrans[ch].assign ((size_t) fftSize, 0.0f);
        outRest[ch].assign ((size_t) fftSize, 0.0f);
        spec[ch].assign ((size_t) bins, {});
        hazeMag[ch].assign ((size_t) bins, 0.0f);
        hazePhase[ch].assign ((size_t) bins, 0.0f);
    }
    work.assign ((size_t) fftSize, {});
    avgDb.assign ((size_t) bins, -120.0f);
    novelty.assign ((size_t) bins, 0.0f);
    envelope.assign ((size_t) bins, 0.0f);
    releaseCoef.assign ((size_t) bins, 0.0f);
    weightDb.assign ((size_t) bins, 0.0f);
    visIn.assign ((size_t) bins, -120.0f);

    modesDirty = true;
    setSettings (settings);
    reset();
}

void BrumeEngine::reset() noexcept
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        std::fill (inRing[ch].begin(), inRing[ch].end(), 0.0f);
        std::fill (outTrans[ch].begin(), outTrans[ch].end(), 0.0f);
        std::fill (outRest[ch].begin(), outRest[ch].end(), 0.0f);
        std::fill (hazeMag[ch].begin(), hazeMag[ch].end(), 0.0f);
        std::fill (hazePhase[ch].begin(), hazePhase[ch].end(), 0.0f);
    }
    std::fill (avgDb.begin(), avgDb.end(), -120.0f);
    std::fill (envelope.begin(), envelope.end(), 0.0f);

    for (auto& m : modes)
        for (int ch = 0; ch < kMaxChannels; ++ch)
            m.x1[ch] = m.x2[ch] = m.y1[ch] = m.y2[ch] = 0.0f;

    writePos = 0;
    hopCount = 0;
}

void BrumeEngine::setSettings (const Settings& s) noexcept
{
    if (s.root != modeKey.root || s.chord != modeKey.chord
        || s.decayS != modeKey.decayS || s.brightness != modeKey.brightness)
        modesDirty = true;

    settings = s;

    // Retour de chaque bin : le tilt allonge les aigus (> 0) ou les graves (< 0),
    // d'un facteur 2 par tranche de deux octaves au plus, autour de 1 kHz.
    if (! releaseCoef.empty())
    {
        const float releaseS = std::max (0.001f, s.releaseMs * 0.001f);
        const float binHz = (float) (sampleRate / fftSize);
        for (int k = 0; k < bins; ++k)
        {
            const float hz = std::max (20.0f, (float) k * binHz);
            const float octaves = std::log2 (hz / 1000.0f);
            const float t = releaseS * std::pow (2.0f, 0.5f * s.tilt * octaves);
            releaseCoef[(size_t) k] = std::exp (-hopSeconds / std::max (0.001f, t));
            weightDb[(size_t) k] = sensitivityDb (s, hz);
        }
    }
}

void BrumeEngine::updateModes() noexcept
{
    modesDirty = false;
    modeKey = settings;

    const auto& intervals = chordIntervals (settings.chord);
    const float baseMidi = 48.0f + (float) std::clamp (settings.root, 0, 11);   // do3 .. si3
    const float f0 = 440.0f * std::pow (2.0f, (baseMidi - 69.0f) / 12.0f);
    const float nyquistGuard = (float) sampleRate * 0.45f;

    numModes = 0;
    for (int octave = 0; numModes < kMaxModes && octave < 6; ++octave)
    {
        for (int iv : intervals)
        {
            if (numModes >= kMaxModes)
                break;

            const float hz = f0 * std::pow (2.0f, (float) (iv + 12 * octave) / 12.0f);
            if (hz > std::min (8000.0f, nyquistGuard))
                break;

            const float ratio = hz / f0;
            // Sombre : les modes aigus s'éteignent vite et sont plus bas. Brillant : l'inverse.
            const float t60 = std::max (0.05f, settings.decayS * std::pow (ratio, (settings.brightness - 1.0f) * 0.6f));
            const float level = std::pow (ratio, (settings.brightness - 0.6f) * 0.8f);

            const float r = std::exp (-kLn1000 / (t60 * (float) sampleRate));
            const float theta = kTwoPi * hz / (float) sampleRate;
            const float thetaR = theta * std::pow (2.0f, 4.0f / 1200.0f);   // +4 cents à droite

            modeHz[numModes] = hz;
            auto& m = modes[numModes++];
            m.a1 = 2.0f * r * std::cos (theta);
            m.a1R = 2.0f * r * std::cos (thetaR);
            m.a2 = -r * r;
            // Normalisé en énergie : un clic donne à peu près la même quantité de son
            // quelle que soit la durée de résonance.
            m.gain = level * 2.0f * std::sin (theta) * std::sqrt (1.0f - r);
        }
    }

    const float norm = 1.0f / std::sqrt ((float) std::max (1, numModes));
    for (int i = 0; i < numModes; ++i)
        modes[i].gain *= norm;
}

float BrumeEngine::random() noexcept
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float) rng * (2.0f / 4294967296.0f) - 1.0f;   // -1..1
}

void BrumeEngine::processFrame() noexcept
{
    const Settings& s = settings;

    // 1. Spectres, la trame la plus ancienne en premier.
    for (int ch = 0; ch < numCh; ++ch)
    {
        for (int i = 0; i < fftSize; ++i)
            work[(size_t) i] = { inRing[ch][(size_t) ((writePos + i) & mask)] * window[(size_t) i], 0.0f };
        fft->forward (work.data());
        std::copy (work.begin(), work.begin() + bins, spec[ch].begin());
    }

    // 2. Détection, sur la moyenne des canaux : chaque bin comparé à sa propre
    //    moyenne récente, en dB. Ce qui monte d'un coup est une attaque.
    const float follow = 1.0f - std::exp (-hopSeconds / 0.05f);
    for (int k = 0; k < bins; ++k)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            m += std::abs (spec[ch][(size_t) k]);
        m /= (float) numCh;

        const float db = 20.0f * std::log10 (m + 1.0e-9f);
        // Une bande au-dessus de 0 abaisse le seuil d'autant de dB (voir plus bas pour en dessous).
        novelty[(size_t) k] = db > -100.0f ? std::max (0.0f, db - avgDb[(size_t) k] + std::max (0.0f, weightDb[(size_t) k])) : 0.0f;
        avgDb[(size_t) k] += (db - avgDb[(size_t) k]) * follow;

        // Pour l'écran : le spectre d'entrée, en dB par rapport à une sinusoïde pleine échelle.
        const float shown = db - 20.0f * std::log10 ((float) fftSize * 0.25f);
        visIn[(size_t) k] += (shown - visIn[(size_t) k]) * (shown > visIn[(size_t) k] ? 0.6f : 0.15f);
    }

    const float threshold = 14.0f - 1.2f * std::clamp (s.sensitivity, 0.0f, 10.0f);  // 14 dB .. 2 dB
    float shareSum = 0.0f;
    for (int k = 0; k < bins; ++k)
    {
        // Lissé sur ±2 bins : une attaque est rarement large d'un seul bin.
        float sum = 0.0f, weight = 0.0f;
        for (int d = -2; d <= 2; ++d)
        {
            const int j = k + d;
            if (j < 0 || j >= bins)
                continue;
            const float w = 3.0f - (float) std::abs (d);
            sum += novelty[(size_t) j] * w;
            weight += w;
        }
        const float n = sum / weight;
        // Une bande en dessous de 0 retire de la part traitée : plus rien à -12 dB.
        const float keep = std::clamp (1.0f + std::min (0.0f, weightDb[(size_t) k]) / 12.0f, 0.0f, 1.0f);
        const float t = keep * std::clamp ((n - threshold) / 6.0f, 0.0f, 1.0f);
        envelope[(size_t) k] = std::max (t, envelope[(size_t) k] * releaseCoef[(size_t) k]);
        shareSum += envelope[(size_t) k];
    }
    transientShare = shareSum / (float) bins;
    ++frameCount;

    // 3. Séparation, brume, et retour au temps.
    const float hazeDecay = std::exp (-kLn1000 * hopSeconds / std::max (0.1f, s.hazeS));
    const float hazeGain = 2.0f * std::clamp (s.haze, 0.0f, 1.0f);
    const float blur = std::clamp (s.blur, 0.0f, 1.0f);
    const float dissolve = blur * std::clamp (s.haze, 0.0f, 1.0f);
    const float phaseStep = kTwoPi * (float) hop / (float) fftSize;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& X = spec[ch];
        auto& hm = hazeMag[ch];
        auto& hp = hazePhase[ch];

        // Les attaques.
        for (int k = 0; k < bins; ++k)
            work[(size_t) k] = X[(size_t) k] * envelope[(size_t) k];
        for (int k = 1; k < bins - 1; ++k)
            work[(size_t) (fftSize - k)] = std::conj (work[(size_t) k]);
        work[0].imag (0.0f);
        work[(size_t) (bins - 1)].imag (0.0f);
        fft->inverse (work.data());
        for (int i = 0; i < fftSize; ++i)
            outTrans[ch][(size_t) ((writePos + i) & mask)] += work[(size_t) i].real() * window[(size_t) i] * olaNorm;

        // Le reste, et la brume qu'il laisse derrière lui.
        for (int k = 0; k < bins; ++k)
        {
            const auto x = X[(size_t) k];
            const float mag = std::abs (x);
            const float decayed = hm[(size_t) k] * hazeDecay;

            if (mag >= decayed)
            {
                hm[(size_t) k] = mag;
                hp[(size_t) k] = std::arg (x);
            }
            else
            {
                hm[(size_t) k] = decayed;
                hp[(size_t) k] += phaseStep * (float) k + blur * kPi * random();
            }

            // La brume ne garde que ce qui dépasse le son présent : une traîne, pas un doublon.
            const float tail = std::max (0.0f, hm[(size_t) k] - mag);
            const auto hazeBin = std::polar (tail * hazeGain, hp[(size_t) k]);

            auto rest = x * (1.0f - envelope[(size_t) k]);
            if (dissolve > 0.0f)
            {
                const auto scattered = std::polar (std::abs (rest), std::arg (rest) + kPi * random());
                rest = rest * (1.0f - dissolve) + scattered * dissolve;
            }
            work[(size_t) k] = rest + hazeBin;
        }
        for (int k = 1; k < bins - 1; ++k)
            work[(size_t) (fftSize - k)] = std::conj (work[(size_t) k]);
        work[0].imag (0.0f);
        work[(size_t) (bins - 1)].imag (0.0f);
        fft->inverse (work.data());
        for (int i = 0; i < fftSize; ++i)
            outRest[ch][(size_t) ((writePos + i) & mask)] += work[(size_t) i].real() * window[(size_t) i] * olaNorm;
    }
}

void BrumeEngine::process (float* const* io, int channels, int numSamples) noexcept
{
    if (modesDirty)
        updateModes();

    const int chs = std::min (channels, numCh);
    const float punch = dbToGain (std::clamp (settings.punchDb(), -60.0f, 24.0f));
    const bool delta = settings.delta, bypass = settings.bypass;
    const float resoGain = 4.0f * std::clamp (settings.reso, 0.0f, 1.0f);
    const float mix = std::clamp (settings.mix, 0.0f, 1.0f);
    const float out = dbToGain (settings.outDb);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto idx = (size_t) writePos;

        for (int ch = 0; ch < chs; ++ch)
        {
            const float x = io[ch][n];
            const float dry = inRing[ch][idx];          // l'entrée d'il y a fftSize échantillons
            const float t = outTrans[ch][idx];
            const float r = outRest[ch][idx];
            outTrans[ch][idx] = 0.0f;
            outRest[ch][idx] = 0.0f;
            inRing[ch][idx] = x;

            // Les attaques frappent les résonateurs (passe-bande à deux pôles).
            float reso = 0.0f;
            for (int i = 0; i < numModes; ++i)
            {
                auto& m = modes[i];
                const float a1 = ch == 1 ? m.a1R : m.a1;
                const float y = m.gain * (t - m.x2[ch]) + a1 * m.y1[ch] + m.a2 * m.y2[ch];
                m.x2[ch] = m.x1[ch];
                m.x1[ch] = t;
                m.y2[ch] = m.y1[ch];
                m.y1[ch] = y;
                reso += y;
            }

            const float wet = r + t * punch + std::tanh (reso * resoGain);
            const float processed = (dry * (1.0f - mix) + wet * mix) * out;
            io[ch][n] = bypass ? dry : (delta ? processed - dry : processed);
        }

        // Un canal en trop dans le bloc passe sans traitement.
        writePos = (writePos + 1) & mask;
        if (++hopCount == hop)
        {
            hopCount = 0;
            processFrame();
        }
    }

    // Évite les nombres dénormalisés dans les queues de résonance.
    for (int i = 0; i < numModes; ++i)
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            if (std::abs (modes[i].y1[ch]) < 1.0e-15f) modes[i].y1[ch] = 0.0f;
            if (std::abs (modes[i].y2[ch]) < 1.0e-15f) modes[i].y2[ch] = 0.0f;
        }
}

} // namespace bb
