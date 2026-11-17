// Onirique Shaper — moteur : processeur de transitoires spectral
#include "ShaperEngine.h"

#include <algorithm>
#include <cmath>

namespace shaper
{

namespace
{
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
    {
        avgDb[d].assign ((size_t) maxBins, -120.0f);
        novelty[d].assign ((size_t) maxBins, 0.0f);
    }
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
        std::fill (avgDb[d].begin(), avgDb[d].end(), -120.0f);
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

    // Decay : de 4 ms (0) à environ 1,4 s (10) ; decay lf/hf penche ce temps autour de 1 kHz.
    const float releaseS = 0.004f * std::pow (2.0f, settings.decay * 0.85f);
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

    // 2. Détection : chaque bin comparé à sa propre moyenne récente, en dB, pour chaque canal et pour les deux liés.
    const float follow = 1.0f - std::exp (-hopSeconds / 0.05f);
    const float threshold = 14.0f - 1.2f * std::clamp (s.sensitivity, 0.0f, 10.0f);
    const float shownOffset = 20.0f * std::log10 ((float) frameLen * 0.25f);
    const int detectors = stereo ? 3 : 1;

    for (int k = 0; k < bins; ++k)
    {
        float linked = 0.0f;
        for (int d = 0; d < detectors; ++d)
        {
            float m;
            if (d < numCh)
            {
                m = std::abs (spec[d][(size_t) k]);
                linked += m;
            }
            else
            {
                m = linked * 0.5f;
            }

            const float db = 20.0f * std::log10 (m + 1.0e-9f);
            auto& avg = avgDb[d][(size_t) k];
            novelty[d][(size_t) k] = db > -110.0f ? std::max (0.0f, db - avg + sensShift[(size_t) k]) : 0.0f;
            avg += (db - avg) * follow;
        }

        const float shown = 20.0f * std::log10 ((stereo ? linked * 0.5f : linked) + 1.0e-9f) - shownOffset;
        visIn[(size_t) k] += (shown - visIn[(size_t) k]) * (shown > visIn[(size_t) k] ? 0.6f : 0.15f);
    }

    const float link = stereo ? std::clamp (s.stereoLink / 100.0f, 0.0f, 1.0f) : 0.0f;
    auto found = [threshold] (float n) { return std::clamp ((n - threshold) / 6.0f, 0.0f, 1.0f); };

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
        if (++hopCount >= hop)
        {
            hopCount = 0;
            processFrame();
        }
    }
}

} // namespace shaper
