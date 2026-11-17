// Onirique Shaper — moteur : processeur de transitoires spectral
#include "ShaperEngine.h"

#include <algorithm>
#include <cmath>

namespace shaper
{

namespace
{
    // Calées sur spiff (même sons, mêmes réglages, tools/compare.cpp) : 0,9 dB d'écart moyen sur 7 réglages et 4 sons.
    constexpr float kThr0 = 18.5f, kThr1 = 1.45f;        // seuil de nouveauté (dB) : 18,5 - 1,45 × sensitivity
    constexpr float kSoft = 18.0f;                       // de « à peine trouvé » à « pleinement » : 18 dB de plus
    constexpr float kDecBase = 0.002f, kDecExp = 0.85f;  // decay : 2 ms à 0, environ 0,7 s à 10
    constexpr float kTwoPi = 6.28318530717959f;
    constexpr float kLn10Over20 = 0.115129254649702f;

    int baseFrame (Window w) noexcept
    {
        switch (w)
        {
            case Window::shortWindow: return 1024;
            case Window::longWindow:  return 4096;
            case Window::mediumWindow:
            default:                  return 2048;
        }
    }

    int scaleFor (double sr) noexcept { return sr >= 176400.0 ? 4 : (sr >= 88200.0 ? 2 : 1); }

    /** Réponse d'un coupe-bas (ou coupe-haut avec le rapport inversé), de 0 à 1. */
    float cutResponse (float ratio, int slopeIndex) noexcept
    {
        const float order = kCutSlopes[std::clamp (slopeIndex, 0, 3)] / 6.0f;
        return 1.0f / std::sqrt (1.0f + std::pow (std::max (1.0e-6f, ratio), 2.0f * order));
    }

    float octavesFrom (float hz, float centre) noexcept
    {
        return std::log2 (std::max (1.0f, hz) / std::max (1.0f, centre));
    }

    /** La forme d'une bande 1 à 3, de 0 à 1. */
    float bandShape (const Band& b, float hz) noexcept
    {
        const float oct = octavesFrom (hz, b.freq);
        const float q = std::clamp (b.q, 0.05f, 20.0f);

        switch ((BandMode) std::clamp (b.mode, 0, kNumBandModes - 1))
        {
            case BandMode::lowShelf:  return 1.0f / (1.0f + std::exp (oct * 4.0f * q));
            case BandMode::highShelf: return 1.0f / (1.0f + std::exp (-oct * 4.0f * q));
            case BandMode::tilt:      return 1.0f;
            case BandMode::bell:
            default:
            {
                const float width = 0.5f / q;
                return std::exp (-0.5f * (oct * oct) / (width * width));
            }
        }
    }
}

Settings defaultSettings()
{
    Settings s;
    s.bands[0] = { false, 20.0f, 0.0f, 0.7f, 1 };
    s.bands[1] = { true, 100.0f, 0.0f, 0.7f, (int) BandMode::bell };
    s.bands[2] = { true, 1000.0f, 0.0f, 0.7f, (int) BandMode::bell };
    s.bands[3] = { true, 10000.0f, 0.0f, 0.7f, (int) BandMode::bell };
    s.bands[4] = { false, 20000.0f, 0.0f, 0.7f, 1 };
    return s;
}

float bandDb (const Settings& s, int band, float hz) noexcept
{
    const auto& b = s.bands[std::clamp (band, 0, kNumBands - 1)];
    if (! b.on)
        return 0.0f;

    if (band == 0)
        return std::max (-60.0f, 20.0f * std::log10 (cutResponse (b.freq / std::max (1.0f, hz), b.mode)));
    if (band == kNumBands - 1)
        return std::max (-60.0f, 20.0f * std::log10 (cutResponse (hz / std::max (1.0f, b.freq), b.mode)));

    // L'inclinaison va de -sens/2 sous la fréquence à +sens/2 au-dessus.
    if ((BandMode) b.mode == BandMode::tilt)
        return b.sens * 0.5f * std::tanh (octavesFrom (hz, b.freq) * 2.0f * std::clamp (b.q, 0.05f, 20.0f));

    return b.sens * bandShape (b, hz);
}

float sensitivityDb (const Settings& s, float hz) noexcept
{
    float db = 0.0f;
    for (int b = 0; b < kNumBands; ++b)
        db += bandDb (s, b, hz);
    return db;
}

float bandCoverage (const Settings& s, int band, float hz) noexcept
{
    const auto& b = s.bands[std::clamp (band, 0, kNumBands - 1)];

    if (band == 0)
        return 1.0f - cutResponse (b.freq / std::max (1.0f, hz), b.mode);
    if (band == kNumBands - 1)
        return 1.0f - cutResponse (hz / std::max (1.0f, b.freq), b.mode);

    return bandShape (b, hz);
}

//==============================================================================
int ShaperEngine::latencyFor (double sr, Window w, int oversample) noexcept
{
    const int n = baseFrame (w) * scaleFor (sr);
    const int m = n << std::clamp (oversample, 0, 2);
    return (m + n) / 2;
}

void ShaperEngine::prepare (double sr, int channels)
{
    sampleRate = sr;
    numCh = std::clamp (channels, 1, kMaxChannels);
    srScale = scaleFor (sr);

    // Toutes les tailles de FFT possibles : de la fenêtre courte à la longue en 4x.
    ffts.clear();
    for (int size = 1024 * srScale; size <= 16384 * srScale; size *= 2)
        ffts.push_back (std::make_unique<Fft> (size));

    const int maxFft = 16384 * srScale;
    const int maxBins = maxFft / 2 + 1;
    ringSize = 2 * maxFft;
    ringMask = ringSize - 1;

    window.assign ((size_t) (4096 * srScale), 0.0f);
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        inRing[ch].assign ((size_t) ringSize, 0.0f);
        outRing[ch].assign ((size_t) ringSize, 0.0f);
        spec[ch].assign ((size_t) maxBins, {});
        target[ch].assign ((size_t) maxBins, 0.0f);
        envelope[ch].assign ((size_t) maxBins, 0.0f);
        gainDb[ch].assign ((size_t) maxBins, 0.0f);
    }
    for (int d = 0; d <= kMaxChannels; ++d)
        novelty[d].assign ((size_t) maxBins, 0.0f);

    // La détection fine : 256 échantillons à 48 kHz (5,3 ms), un pas de 64 (1,3 ms) ; des bandes d'un tiers d'octave
    // (une par bin dans le grave, où la trame courte n'en a pas plus).
    detN = 256 * srScale;
    detHop = 64 * srScale;
    detFft = std::make_unique<Fft> (detN);
    detWindow.assign ((size_t) detN, 0.0f);
    for (int i = 0; i < detN; ++i)
        detWindow[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) detN);
    detLo.clear();
    detHi.clear();
    detBandHz.clear();
    for (int lo = 1; lo <= detN / 2;)
    {
        const int hi = std::min (detN / 2, std::max (lo, (int) std::floor ((float) lo * 1.26f)));
        detLo.push_back (lo);
        detHi.push_back (hi);
        detBandHz.push_back (std::sqrt ((float) lo * (float) hi) * (float) (sr / detN));
        lo = hi + 1;
    }
    detBands = (int) detLo.size();
    for (int ch = 0; ch < kMaxChannels; ++ch)
        detWork[ch].assign ((size_t) detN, {});
    for (int d = 0; d <= kMaxChannels; ++d)
    {
        detLevel[d].assign ((size_t) (kDetHistory * detBands), -120.0f);
        detNovelty[d].assign ((size_t) (kDetEvents * detBands), 0.0f);
    }
    detTime.assign ((size_t) kDetEvents, -(int64_t) 1 << 40);
    detPower.assign ((size_t) detBands, 0.0f);
    bandNovelty.assign ((size_t) ((kMaxChannels + 1) * detBands), 0.0f);
    binBand.assign ((size_t) maxBins, 0.0f);
    work.assign ((size_t) maxFft, {});
    cep.assign ((size_t) maxFft, {});
    for (auto* v : { &releaseCoef, &amount, &sensShift, &coverage, &scratch, &visIn, &visGain })
        v->assign ((size_t) maxBins, 0.0f);
    prefix.assign ((size_t) maxBins + 1, 0.0f);
    halfWidth.assign ((size_t) maxBins, 1);

    configure();
}

void ShaperEngine::configure() noexcept
{
    activeWindow = settings.window;
    activeOversample = std::clamp (settings.oversample, 0, 2);
    activeHighRes = settings.highResolution;

    frameLen = baseFrame (activeWindow) * srScale;
    fftSize = frameLen << activeOversample;
    bins = fftSize / 2 + 1;
    pad = (fftSize - frameLen) / 2;
    hop = frameLen / (activeHighRes ? 8 : 4);
    hopSeconds = (float) (hop / sampleRate);
    latencySamples = (fftSize + frameLen) / 2;

    fft = nullptr;
    for (auto& f : ffts)
        if (f->size() == fftSize)
            fft = f.get();

    double sum = 0.0, sumSq = 0.0;
    for (int i = 0; i < frameLen; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) frameLen);
        sum += window[(size_t) i];
        sumSq += (double) window[(size_t) i] * window[(size_t) i];
    }

    // Sans oversample, une fenêtre de synthèse adoucit les bords de trame. Avec, le filtre a la place
    // de résonner dans le remplissage, et la trame se recolle telle quelle.
    synthesisWindow = activeOversample == 0;
    olaNorm = (float) (hop / (synthesisWindow ? sumSq : sum));

    // Chaque bin de la grande FFT, placé entre deux bandes de la détection (en octaves).
    const float binHz = (float) (sampleRate / fftSize);
    for (int k = 0; k < bins; ++k)
    {
        const float hz = std::max (1.0f, (float) k * binHz);
        int b = 0;
        while (b + 1 < detBands && detBandHz[(size_t) b + 1] <= hz)
            ++b;
        float pos = (float) b;
        if (b + 1 < detBands && hz > detBandHz[(size_t) b])
            pos += std::log2 (hz / detBandHz[(size_t) b]) / std::log2 (detBandHz[(size_t) b + 1] / detBandHz[(size_t) b]);
        binBand[(size_t) k] = pos;
    }

    weightsDirty = true;
    reset();
}

void ShaperEngine::reset() noexcept
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        std::fill (inRing[ch].begin(), inRing[ch].end(), 0.0f);
        std::fill (outRing[ch].begin(), outRing[ch].end(), 0.0f);
        std::fill (envelope[ch].begin(), envelope[ch].end(), 0.0f);
        std::fill (gainDb[ch].begin(), gainDb[ch].end(), 0.0f);
    }
    for (int d = 0; d <= kMaxChannels; ++d)
    {
        std::fill (detLevel[d].begin(), detLevel[d].end(), -120.0f);
        std::fill (detNovelty[d].begin(), detNovelty[d].end(), 0.0f);
    }
    std::fill (detTime.begin(), detTime.end(), -(int64_t) 1 << 40);
    samplesIn = 0;
    detEnd = detN;
    detCount = 0;
    std::fill (visIn.begin(), visIn.end(), -120.0f);
    std::fill (visGain.begin(), visGain.end(), 0.0f);
    writePos = 0;
    hopCount = 0;
}

void ShaperEngine::setSettings (const Settings& s) noexcept
{
    const auto& k = weightKey;
    bool changed = s.sharpness != k.sharpness || s.decay != k.decay || s.decayLfHf != k.decayLfHf
                   || s.listenBand != k.listenBand;
    for (int b = 0; b < kNumBands && ! changed; ++b)
        changed = s.bands[b].on != k.bands[b].on || s.bands[b].freq != k.bands[b].freq || s.bands[b].sens != k.bands[b].sens
                  || s.bands[b].q != k.bands[b].q || s.bands[b].mode != k.bands[b].mode;
    if (changed)
        weightsDirty = true;

    settings = s;
}

void ShaperEngine::updateWeights() noexcept
{
    weightsDirty = false;
    weightKey = settings;

    const float binHz = (float) (sampleRate / fftSize);

    // Decay : de 2 ms (0) à environ 0,7 s (10) ; decay lf/hf penche ce temps autour de 1 kHz.
    const float releaseS = kDecBase * std::pow (2.0f, settings.decay * kDecExp);
    const float lean = (std::clamp (settings.decayLfHf, 0.0f, 10.0f) - 5.0f) / 5.0f;

    // Sharpness : chaque coupe ou boost s'étale sur 2 octaves (0) à un trentième d'octave (10).
    const float widthOct = std::pow (2.0f, 1.0f - std::clamp (settings.sharpness, 0.0f, 10.0f) * 0.6f);
    const float spread = std::pow (2.0f, widthOct * 0.5f) - 1.0f;
    const int minHalf = std::max (1, (1 << activeOversample) / 2);

    for (int k = 0; k < bins; ++k)
    {
        const float hz = std::max (10.0f, (float) k * binHz);
        const float octaves = std::log2 (hz / 1000.0f);
        releaseCoef[(size_t) k] = std::exp (-hopSeconds / std::max (0.001f, releaseS * std::pow (2.0f, lean * octaves)));

        // Les bandes 1 à 3 : au-dessus de 0, plus de transitoires trouvées, et la profondeur atteinte plus tôt ;
        // en dessous, moins, jusqu'à rien à -18 dB. Les coupe-bas et coupe-haut font ignorer ce qu'ils coupent.
        float weight = 0.0f;
        for (int b = 1; b < kNumBands - 1; ++b)
            weight += bandDb (settings, b, hz);

        float pass = 1.0f;
        for (int b : { 0, kNumBands - 1 })
            if (settings.bands[b].on)
                pass *= std::pow (10.0f, bandDb (settings, b, hz) / 20.0f);

        sensShift[(size_t) k] = std::max (0.0f, weight) * 0.5f;
        amount[(size_t) k] = pass * std::clamp (1.0f + weight / 18.0f, 0.0f, 2.0f);
        coverage[(size_t) k] = settings.listenBand >= 0 ? bandCoverage (settings, settings.listenBand, hz) : 1.0f;
        halfWidth[(size_t) k] = std::clamp ((int) ((float) k * spread), minHalf, std::max (minHalf, bins / 4));
    }
}

void ShaperEngine::smoothAcross (const float* in, float* out) noexcept
{
    prefix[0] = 0.0f;
    for (int k = 0; k < bins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + in[k];

    for (int k = 0; k < bins; ++k)
    {
        const int hw = halfWidth[(size_t) k];
        const int lo = std::max (0, k - hw), hi = std::min (bins - 1, k + hw);
        out[k] = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (float) (hi - lo + 1);
    }
}

void ShaperEngine::minimumPhase (const float* gain, std::complex<float>* out) noexcept
{
    // Cepstre réel du logarithme du gain, replié sur les temps positifs : la même amplitude, sans pré-écho.
    for (int k = 0; k < bins; ++k)
        cep[(size_t) k] = { gain[k] * kLn10Over20, 0.0f };
    for (int k = 1; k < bins - 1; ++k)
        cep[(size_t) (fftSize - k)] = cep[(size_t) k];
    fft->inverse (cep.data());

    const int half = fftSize / 2;
    for (int n = 1; n < half; ++n)
        cep[(size_t) n] = { 2.0f * cep[(size_t) n].real(), 0.0f };
    cep[0] = { cep[0].real(), 0.0f };
    cep[(size_t) half] = { cep[(size_t) half].real(), 0.0f };
    for (int n = half + 1; n < fftSize; ++n)
        cep[(size_t) n] = {};
    fft->forward (cep.data());

    for (int k = 0; k < bins; ++k)
        out[k] = std::exp (cep[(size_t) k]);
}

void ShaperEngine::detect() noexcept
{
    const bool stereo = numCh == 2;
    const bool midSide = stereo && settings.stereoMode == StereoMode::midSide;
    const int detectors = stereo ? 3 : 1;
    constexpr int nearest = 4, farthest = 22;   // la référence : les trames de 5 à 30 ms avant

    while (detEnd <= samplesIn)
    {
        // Les échantillons [detEnd - detN, detEnd), retrouvés dans l'anneau d'entrée.
        const int back = (int) (samplesIn - detEnd) + detN;
        for (int i = 0; i < detN; ++i)
        {
            const int idx = (writePos - back + i) & ringMask;
            float a = inRing[0][(size_t) idx], b = stereo ? inRing[1][(size_t) idx] : 0.0f;
            if (midSide)
            {
                const float m = (a + b) * 0.5f;
                b = (a - b) * 0.5f;
                a = m;
            }
            detWork[0][(size_t) i] = { a * detWindow[(size_t) i], 0.0f };
            if (stereo)
                detWork[1][(size_t) i] = { b * detWindow[(size_t) i], 0.0f };
        }

        const int slot = detCount % kDetHistory, event = detCount % kDetEvents;
        detTime[(size_t) event] = detEnd - detN / 2;
        std::fill (detPower.begin(), detPower.end(), 0.0f);

        for (int d = 0; d < detectors; ++d)
        {
            if (d < numCh)
                detFft->forward (detWork[d].data());

            for (int b = 0; b < detBands; ++b)
            {
                float p = 0.0f;
                if (d < numCh)
                {
                    for (int k = detLo[(size_t) b]; k <= detHi[(size_t) b]; ++k)
                        p += std::norm (detWork[d][(size_t) k]);
                    detPower[(size_t) b] += p * 0.5f;
                }
                else
                {
                    p = detPower[(size_t) b];   // les deux canaux ensemble
                }

                const float level = 10.0f * std::log10 (p + 1.0e-12f);
                float reference = -120.0f;
                for (int ago = nearest; ago <= farthest; ++ago)
                    reference = std::max (reference, detLevel[d][(size_t) (((detCount - ago + kDetHistory * 4) % kDetHistory) * detBands + b)]);
                detLevel[d][(size_t) (slot * detBands + b)] = level;
                detNovelty[d][(size_t) (event * detBands + b)] = level > -100.0f ? std::max (0.0f, level - reference) : 0.0f;
            }
        }

        ++detCount;
        detEnd += detHop;
    }
}

void ShaperEngine::processFrame() noexcept
{
    const Settings& s = settings;
    const bool stereo = numCh == 2;
    const bool midSide = stereo && s.stereoMode == StereoMode::midSide;

    // 1. Spectres : la trame, fenêtrée, au milieu de la FFT (remplie de zéros autour avec l'oversample).
    for (int ch = 0; ch < numCh; ++ch)
    {
        std::fill (work.begin(), work.begin() + fftSize, std::complex<float> {});
        for (int i = 0; i < frameLen; ++i)
            work[(size_t) (pad + i)] = { inRing[ch][(size_t) ((writePos - frameLen + i) & ringMask)] * window[(size_t) i], 0.0f };
        fft->forward (work.data());
        std::copy (work.begin(), work.begin() + bins, spec[ch].begin());
    }

    if (midSide)
        for (int k = 0; k < bins; ++k)
        {
            const auto l = spec[0][(size_t) k], r = spec[1][(size_t) k];
            spec[0][(size_t) k] = (l + r) * 0.5f;
            spec[1][(size_t) k] = (l - r) * 0.5f;
        }

    // 2. Détection : ce qui vient de monter vite, bande par bande (detect), pris autour du centre de cette trame,
    //    puis réparti sur les bins. Le reste (seuil, largeur, retour, profondeur) ne change pas.
    detect();
    const float threshold = kThr0 - kThr1 * std::clamp (s.sensitivity, 0.0f, 10.0f);
    const float shownOffset = 20.0f * std::log10 ((float) frameLen * 0.25f);
    const int detectors = stereo ? 3 : 1;
    {
        const int64_t centre = samplesIn - frameLen / 2;
        std::fill (bandNovelty.begin(), bandNovelty.end(), 0.0f);
        for (int e = 0; e < kDetEvents; ++e)
            if (std::abs (detTime[(size_t) e] - centre) <= hop)
                for (int d = 0; d < detectors; ++d)
                    for (int b = 0; b < detBands; ++b)
                        bandNovelty[(size_t) (d * detBands + b)] = std::max (bandNovelty[(size_t) (d * detBands + b)],
                                                                             detNovelty[d][(size_t) (e * detBands + b)]);
    }

    for (int k = 0; k < bins; ++k)
    {
        const float pos = binBand[(size_t) k];
        const int b0 = std::min ((int) pos, detBands - 1), b1 = std::min (b0 + 1, detBands - 1);
        const float f = pos - (float) b0;
        for (int d = 0; d < detectors; ++d)
        {
            const float n = bandNovelty[(size_t) (d * detBands + b0)] * (1.0f - f) + bandNovelty[(size_t) (d * detBands + b1)] * f;
            novelty[d][(size_t) k] = n > 0.0f ? n + sensShift[(size_t) k] : 0.0f;
        }

        float linked = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            linked += std::abs (spec[ch][(size_t) k]);
        const float shown = 20.0f * std::log10 ((stereo ? linked * 0.5f : linked) + 1.0e-9f) - shownOffset;
        visIn[(size_t) k] += (shown - visIn[(size_t) k]) * (shown > visIn[(size_t) k] ? 0.6f : 0.15f);
    }

    const float link = stereo ? std::clamp (s.stereoLink / 100.0f, 0.0f, 1.0f) : 0.0f;
    auto found = [threshold] (float n) { return std::clamp ((n - threshold) / kSoft, 0.0f, 1.0f); };

    // Balance : 0 = 100/0, 0,5 = 100/100, 1 = 0/100.
    const float balance = std::clamp (s.balance, 0.0f, 1.0f);
    const float channelAmount[2] = { std::min (1.0f, 2.0f * (1.0f - balance)), std::min (1.0f, 2.0f * balance) };
    const float range = s.rangeDb();

    float share = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        for (int k = 0; k < bins; ++k)
            scratch[(size_t) k] = (1.0f - link) * found (novelty[ch][(size_t) k])
                                  + link * found (novelty[stereo ? 2 : 0][(size_t) k]);

        smoothAcross (scratch.data(), target[ch].data());

        auto& env = envelope[ch];
        const float chAmount = stereo ? channelAmount[ch] : 1.0f;
        for (int k = 0; k < bins; ++k)
        {
            const float t = target[ch][(size_t) k] * amount[(size_t) k];
            env[(size_t) k] = std::max (t, env[(size_t) k] * releaseCoef[(size_t) k]);
            gainDb[ch][(size_t) k] = range * std::min (env[(size_t) k], 1.0f) * chAmount;   // depth fixe le maximum
            share += std::min (env[(size_t) k], 1.0f);
        }
    }
    transientShare = share / (float) (bins * numCh);

    for (int k = 0; k < bins; ++k)
    {
        float g = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            g += gainDb[ch][(size_t) k];
        visGain[(size_t) k] = g / (float) numCh;
    }
    ++frameCount;

    // 3. Le filtre de chaque canal, linéaire (gain réel) ou à phase minimale.
    const bool listening = s.listenBand >= 0;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& X = spec[ch];
        if (s.phase == Phase::minimum)
        {
            minimumPhase (gainDb[ch].data(), work.data());
            for (int k = 0; k < bins; ++k)
                X[(size_t) k] *= listening ? (work[(size_t) k] - 1.0f) * coverage[(size_t) k] : work[(size_t) k];
        }
        else
        {
            for (int k = 0; k < bins; ++k)
            {
                const float g = std::pow (10.0f, gainDb[ch][(size_t) k] / 20.0f);
                X[(size_t) k] *= listening ? (g - 1.0f) * coverage[(size_t) k] : g;
            }
        }
    }

    if (midSide)
        for (int k = 0; k < bins; ++k)
        {
            const auto m = spec[0][(size_t) k], sd = spec[1][(size_t) k];
            spec[0][(size_t) k] = m + sd;
            spec[1][(size_t) k] = m - sd;
        }

    // 4. Retour au temps, et recollage des trames. La trame commence à writePos - frameLen - pad ;
    //    retardée de latencySamples = frameLen + pad, elle se recolle à partir de writePos.
    for (int ch = 0; ch < numCh; ++ch)
    {
        for (int k = 0; k < bins; ++k)
            work[(size_t) k] = spec[ch][(size_t) k];
        for (int k = 1; k < bins - 1; ++k)
            work[(size_t) (fftSize - k)] = std::conj (work[(size_t) k]);
        work[0].imag (0.0f);
        work[(size_t) (bins - 1)].imag (0.0f);
        fft->inverse (work.data());

        auto& out = outRing[ch];
        if (synthesisWindow)
        {
            for (int i = 0; i < frameLen; ++i)
                out[(size_t) ((writePos + i) & ringMask)] += work[(size_t) i].real() * window[(size_t) i] * olaNorm;
        }
        else
        {
            for (int i = 0; i < fftSize; ++i)
                out[(size_t) ((writePos + i) & ringMask)] += work[(size_t) i].real() * olaNorm;
        }
    }
}

void ShaperEngine::process (float* const* io, int channels, int numSamples) noexcept
{
    if (settings.window != activeWindow || std::clamp (settings.oversample, 0, 2) != activeOversample
        || settings.highResolution != activeHighRes)
        configure();

    if (weightsDirty)
        updateWeights();

    const int chs = std::min (channels, numCh);
    const auto& s = settings;
    const float mix = std::clamp (s.mix / 100.0f, 0.0f, 1.0f);
    const float trim = std::pow (10.0f, s.trimDb / 20.0f);
    const bool listening = s.listenBand >= 0;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto idx = (size_t) writePos;
        const auto dryIdx = (size_t) ((writePos - latencySamples) & ringMask);

        for (int ch = 0; ch < chs; ++ch)
        {
            const float x = io[ch][n];
            const float dry = inRing[ch][dryIdx];
            const float wet = outRing[ch][idx];
            outRing[ch][idx] = 0.0f;
            inRing[ch][idx] = x;

            float y;
            if (listening)
                y = wet;                                    // le delta de la bande écoutée
            else if (s.bypass)
                y = dry;
            else if (s.delta)
                y = dry * (1.0f - mix) + (wet * trim - dry) * mix;
            else
                y = dry * (1.0f - mix) + wet * trim * mix;

            io[ch][n] = y;
        }

        writePos = (writePos + 1) & ringMask;
        ++samplesIn;
        if (++hopCount >= hop)
        {
            hopCount = 0;
            processFrame();
        }
    }
}

} // namespace shaper
