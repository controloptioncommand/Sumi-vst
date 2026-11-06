#include "SpectralEngine.h"

namespace sumi
{
SpectralEngine::SpectralEngine()
{
    // всё выделяется один раз под самый большой FFT — смена Resolution в аудиопотоке ничего не выделяет
    for (int o = kMinOrder; o <= kMaxOrder; ++o) ffts[o] = std::make_unique<juce::dsp::FFT> (o);
    window.assign ((size_t) kMaxSize, 0.0f);
    for (auto& v : inRing) v.assign ((size_t) kMaxSize, 0.0f);
    for (auto& v : outAcc) v.assign ((size_t) kMaxSize, 0.0f);
    buf.assign ((size_t) kMaxSize * 2, 0.0f);
    const size_t bins = (size_t) kMaxSize / 2 + 1;
    phase.assign (bins, 0.0f);
    stereoOffset.assign (bins, 0.0f);
    amp.assign (bins, 0.0f);
    binY0.assign (bins, 0.0f);
    binY1.assign (bins, 0.0f);
    power.assign (bins, 0.0f);
    prefix.assign (bins + 1, 0.0f);
    setOrder (12);
}

void SpectralEngine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    binsForRate = 0;
    reset();
}

void SpectralEngine::setOrder (int o)
{
    order = juce::jlimit (kMinOrder, kMaxOrder, o);
    N = 1 << order;
    hop = N / 4;
    for (int j = 0; j < N; ++j)
        window[(size_t) j] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) j / (float) N);   // периодический Hann
    binsForN = 0;
    reset();
}

void SpectralEngine::reset()
{
    for (auto& v : inRing) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : outAcc) std::fill (v.begin(), v.end(), 0.0f);
    inPos = outPos = hopCount = 0;
    state = 0x9E3779B9u;
    for (size_t k = 0; k < phase.size(); ++k)
    {
        phase[k] = rndPhase();
        stereoOffset[k] = rndPhase();
    }
}

void SpectralEngine::updateBins (const FreqMap& m)
{
    if (binsForN == N && binsForRate == sr && binMap.scale == m.scale && binMap.lo == m.lo && binMap.hi == m.hi) return;
    binMap = m;
    binsForN = N;
    binsForRate = sr;
    const double df = sr / N;
    kLo = N / 2;
    kHi = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        double f0 = (k - 0.5) * df, f1 = (k + 0.5) * df;
        if (f1 <= m.lo || f0 >= m.hi) continue;
        f0 = juce::jmax (f0, m.lo);
        f1 = juce::jmin (f1, m.hi);
        // строка 0 картинки — верх (высокие частоты)
        binY0[(size_t) k] = (float) (1.0 - m.pos (f1));
        binY1[(size_t) k] = (float) (1.0 - m.pos (f0));
        kLo = juce::jmin (kLo, k);
        kHi = juce::jmax (kHi, k);
    }
}

void SpectralEngine::addPicture (float* spec, int channel, float gain, const EngineParams& p) const
{
    const int B = kHi - kLo + 1;
    if (B <= 0) return;
    // калибровка: вся полоса белая → около −14 dBFS RMS; случайная фаза теряет ~4 дБ на перекрытии — компенсируем
    const float norm = 0.2f / std::sqrt ((float) B * 0.5f);
    const float comp = 1.633f - 0.633f * p.tone;
    const float A0 = norm * comp * gain * 0.75f * (float) N * 0.5f;
    const float off = channel == 1 ? p.width : 0.0f;
    for (int k = kLo; k <= kHi; ++k)
    {
        const float a = amp[(size_t) k];
        if (a <= 0.0f) continue;
        const float ph = phase[(size_t) k] + off * stereoOffset[(size_t) k];
        spec[2 * k] += A0 * a * std::cos (ph);
        spec[2 * k + 1] += A0 * a * std::sin (ph);
    }
}

void SpectralEngine::doFrame (int t, int numChannels, const EngineParams& p, const Timing& tm)
{
    for (int c = 0; c < numChannels; ++c)
    {
        auto* acc = outAcc[c].data();
        std::memmove (acc, acc + hop, sizeof (float) * (size_t) (N - hop));
        std::fill (acc + (N - hop), acc + N, 0.0f);
    }
    outPos = 0;

    // кадр звучит в окне [t, t + N) выхода, его центр после компенсации задержки хостом — момент t − N/2
    const double u = tm.phaseAt ((double) t - N * 0.5);
    const Picture* pic = picture.get();
    const bool on = u >= 0.0 && pic != nullptr && ! pic->empty();
    if (on)
    {
        updateBins (p.map);
        const double W = pic->w, H = pic->h;
        const double xc = u * W;
        const double half = juce::jmax (0.5, 0.5 * hop * tm.beatsPerSample / juce::jmax (1e-9, tm.lengthBeats) * W);
        for (int k = kLo; k <= kHi; ++k)
        {
            const float v = pic->boxMean (xc - half, xc + half, binY0[(size_t) k] * H, binY1[(size_t) k] * H);
            amp[(size_t) k] = p.look.amp (v);
        }
        ++framesWithPicture;
    }

    // фаза: ровное продвижение даёт непрерывные тона на центрах бинов, случайная добавка — шум
    {
        const float adv = juce::MathConstants<float>::twoPi * (float) hop / (float) N;
        const float noise = 1.0f - p.tone;
        for (int k = 1; k < N / 2; ++k)
        {
            float ph = phase[(size_t) k] + adv * (float) k + (noise > 0.0f ? noise * rndPhase() : 0.0f);
            ph -= juce::MathConstants<float>::twoPi * std::floor (ph / juce::MathConstants<float>::twoPi);
            phase[(size_t) k] = ph;
        }
    }

    auto& fft = *ffts[order];
    const bool imprint = p.mode == 1;
    float* d = buf.data();
    for (int c = 0; c < numChannels; ++c)
    {
        float olaGain = 1.0f / 1.5f;   // Hann × Hann при перекрытии 75% в сумме дают 1.5
        if (! imprint)
        {
            if (! on) continue;   // Paint без картинки — тишина
            std::fill (d, d + 2 * N, 0.0f);
            addPicture (d, c, p.level, p);
        }
        else
        {
            const auto* ring = inRing[c].data();
            for (int j = 0; j < N; ++j) d[j] = ring[(inPos + j) & (N - 1)] * window[(size_t) j];
            std::fill (d + N, d + 2 * N, 0.0f);
            fft.performRealOnlyForwardTransform (d, true);
            if (on)
            {
                if (p.blend == 0)
                    addPicture (d, c, p.level, p);
                else
                {
                    // Flatten: спектр входа выравнивается к среднему, чтобы картинка проступала и там, где звука мало
                    const int half = N / 2;
                    bool flat = p.flatten > 0.001f && kHi >= kLo;
                    float ref = 0.0f;
                    if (flat)
                    {
                        prefix[0] = 0.0f;
                        for (int k = 0; k <= half; ++k)
                        {
                            power[(size_t) k] = d[2 * k] * d[2 * k] + d[2 * k + 1] * d[2 * k + 1];
                            prefix[(size_t) k + 1] = prefix[(size_t) k] + power[(size_t) k];
                        }
                        ref = std::sqrt (juce::jmax (0.0f, prefix[(size_t) kHi + 1] - prefix[(size_t) kLo]) / (float) (kHi - kLo + 1));
                        flat = ref > 1e-6f;
                    }
                    // за пределами Low–High картинки нет — там тишина, иначе вырезанное тонет в остальном спектре
                    std::fill (d, d + 2 * kLo, 0.0f);
                    std::fill (d + 2 * (kHi + 1), d + 2 * (half + 1), 0.0f);
                    for (int k = kLo; k <= kHi; ++k)
                    {
                        float g = amp[(size_t) k];
                        if (flat && g > 0.0f)
                        {
                            const int hw = juce::jmax (2, (int) ((float) k * 0.08f));
                            const int a = juce::jmax (0, k - hw), b = juce::jmin (half, k + hw);
                            const float env = std::sqrt (juce::jmax (0.0f, prefix[(size_t) b + 1] - prefix[(size_t) a]) / (float) (b - a + 1));
                            g *= std::pow (juce::jlimit (0.03f, 31.6f, ref / (env + 1e-12f)), p.flatten);
                        }
                        d[2 * k] *= g;
                        d[2 * k + 1] *= g;
                    }
                    olaGain *= p.level;
                }
            }
        }
        fft.performRealOnlyInverseTransform (d);
        auto* acc = outAcc[c].data();
        for (int j = 0; j < N; ++j) acc[j] += d[j] * window[(size_t) j] * olaGain;
    }
}

void SpectralEngine::process (float* const* io, int numChannels, int numSamples, const EngineParams& p, const Timing& tm)
{
    numChannels = juce::jmin (numChannels, kMaxChannels);
    const bool imprint = p.mode == 1;
    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            float* x = io[c] + i;
            const float dry = inRing[c][(size_t) inPos];   // вход N сэмплов назад — выровнен с обработанным
            inRing[c][(size_t) inPos] = *x;
            const float wet = outAcc[c][(size_t) outPos];
            *x = imprint ? dry + (wet - dry) * p.mix : wet;
        }
        inPos = (inPos + 1) & (N - 1);
        ++outPos;
        if (++hopCount >= hop)
        {
            hopCount = 0;
            doFrame (i + 1, numChannels, p, tm);
        }
    }
}
} // namespace sumi
