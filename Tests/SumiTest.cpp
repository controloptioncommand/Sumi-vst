// Офлайн-тест Sumi: таблица сумм картинки, частотные шкалы, прозрачность STFT, картинка в спектре выхода
// (корреляция спектрограммы с исходной картинкой), привязка к тактам, состояние, пресеты, скорость,
// демо-WAV и спектрограммы, скриншоты интерфейса в обеих темах, About.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sumi;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "\n"; } } while (0)

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct TestPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true, has = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! has) return {};
        PositionInfo i;
        i.setPpqPosition (ppq);
        i.setBpm (bpm);
        i.setIsPlaying (playing);
        i.setTimeSignature (TimeSignature { 4, 4 });
        return i;
    }
};

void setParam (SumiProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

juce::AudioBuffer<float> run (SumiProcessor& p, TestPlayHead& ph, double seconds, const juce::AudioBuffer<float>* in = nullptr,
                              SumiEditor* ed = nullptr)
{
    const int total = (int) (seconds * kRate);
    juce::AudioBuffer<float> out (2, total), buf (2, kBlock);
    juce::MidiBuffer midi;
    int pos = 0, blocks = 0;
    while (pos < total)
    {
        const int n = juce::jmin (kBlock, total - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
        {
            if (in != nullptr && pos + n <= in->getNumSamples()) buf.copyFrom (c, 0, *in, c % in->getNumChannels(), pos, n);
            else buf.clear (c, 0, n);
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
        if (ph.playing) ph.ppq += n * ph.bpm / 60.0 / kRate;
        pos += n;
        if (ed != nullptr && ++blocks % 3 == 0) ed->pumpForTest();
    }
    return out;
}

juce::AudioBuffer<float> noise (double seconds, float amp, int seed = 1)
{
    juce::Random r (seed);
    juce::AudioBuffer<float> b (2, (int) (seconds * kRate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, amp * (r.nextFloat() * 2.0f - 1.0f));
    return b;
}

struct Stats { float peak = 0, rms = 0; bool nan = false; };
Stats stats (const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
{
    Stats s;
    if (to < 0) to = b.getNumSamples();
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i)
        {
            const float v = b.getSample (c, i);
            if (! std::isfinite (v)) { s.nan = true; continue; }
            s.peak = juce::jmax (s.peak, std::abs (v));
            sum += (double) v * v;
        }
    s.rms = (float) std::sqrt (sum / juce::jmax (1, b.getNumChannels() * (to - from)));
    return s;
}

void checkAudio (const juce::AudioBuffer<float>& b, const juce::String& what, float minRms = 0.003f)
{
    const auto s = stats (b);
    CHECK (! s.nan, what << ": NaN");
    CHECK (s.peak <= 1.0f, what << ": peak above 0 dBFS (" << s.peak << ")");
    CHECK (s.rms > minRms, what << ": silent (rms " << s.rms << ")");
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, int skip = 0)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (kRate).withNumChannels (2).withBitsPerSample (24)))
        w->writeFromAudioSampleBuffer (b, skip, b.getNumSamples() - skip);
    std::cout << "  wrote " << f.getFileName() << "\n";
}

juce::Image solid (int w, int h, juce::Colour c)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (c);
    return img;
}

// Спектрограмма выхода в сетке cols × rows поверх картинки: время — доля картинки, частота — по шкале картинки.
// Выход сдвинут на задержку: сэмпл j соответствует моменту (j − latency) на шкале.
std::vector<float> spectroGrid (const juce::AudioBuffer<float>& out, int latency, double t0, double dur, const FreqMap& map, int cols, int rows)
{
    constexpr int order = 12, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> work ((size_t) N * 2), grid ((size_t) (cols * rows), -140.0f);
    const double df = kRate / N;
    for (int cx = 0; cx < cols; ++cx)
    {
        const double t = t0 + dur * (cx + 0.5) / cols;
        const int centre = (int) (t * kRate) + latency;
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = centre - N / 2 + i;
            const float x = j >= 0 && j < out.getNumSamples() ? 0.5f * (out.getSample (0, j) + out.getSample (1, j)) : 0.0f;
            work[(size_t) i] = x * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / N));
        }
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        for (int cy = 0; cy < rows; ++cy)
        {
            // cy = 0 — верх картинки
            const double f1 = map.freq (1.0 - (double) cy / rows), f0 = map.freq (1.0 - (double) (cy + 1) / rows);
            double sum = 0;
            int cnt = 0;
            for (int k = juce::jmax (1, (int) std::floor (f0 / df)); k <= juce::jmin (N / 2, (int) std::ceil (f1 / df)); ++k) { sum += (double) work[(size_t) k] * work[(size_t) k]; ++cnt; }
            grid[(size_t) (cy * cols + cx)] = (float) (10.0 * std::log10 (sum / juce::jmax (1, cnt) + 1e-20));
        }
    }
    return grid;
}

std::vector<float> pictureGrid (const Picture& pic, const Look& look, int cols, int rows)
{
    std::vector<float> g ((size_t) (cols * rows));
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx)
        {
            const float v = pic.boxMean ((double) cx / cols * pic.w, (double) (cx + 1) / cols * pic.w, (double) cy / rows * pic.h, (double) (cy + 1) / rows * pic.h);
            g[(size_t) (cy * cols + cx)] = juce::jmax (0.0f, look.level (v));
        }
    return g;
}

float correlation (const std::vector<float>& a, std::vector<float> b, float rangeDb)
{
    // спектрограмма → 0…1 в пределах Range от максимума
    float mx = -1e9f;
    for (float v : b) mx = juce::jmax (mx, v);
    for (auto& v : b) v = juce::jlimit (0.0f, 1.0f, (v - (mx - rangeDb)) / rangeDb);
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= (double) a.size(); mb /= (double) b.size();
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return (float) (sab / std::sqrt (juce::jmax (1e-12, saa * sbb)));
}

// Картинка спектрограммы для README и руководства (тема Rubber, рампа несущей)
void writeSpectrogram (const juce::File& f, const juce::AudioBuffer<float>& out, int latency, double t0, double dur, const FreqMap& view, float rangeDb)
{
    constexpr int W = 512, H = 200;
    auto grid = spectroGrid (out, latency, t0, dur, view, W, H);
    float mx = -1e9f;
    for (float v : grid) mx = juce::jmax (mx, v);
    applyTheme (ThemeId::Rubber);
    juce::Image img (juce::Image::RGB, W * 2, H * 2, true);
    juce::Graphics g (img);
    g.fillAll (Colours::well);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const float t = (grid[(size_t) (y * W + x)] - (mx - rangeDb)) / rangeDb;
            if (t <= 0.0f) continue;
            g.setColour (rampBucketColour (Colours::carrierRamp, juce::jlimit (0, 9, (int) (t * 10.0f))));
            g.fillRect (x * 2, y * 2, 2, 2);
        }
    f.deleteFile();
    juce::FileOutputStream fos (f);
    juce::PNGImageFormat().writeImageToStream (img, fos);
    std::cout << "  wrote " << f.getFileName() << "\n";
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("render");
    outDir.createDirectory();

    // ---------------------------------------------------------------- картинка
    std::cout << "picture\n";
    {
        juce::Image img (juce::Image::ARGB, 4, 2, true);
        img.setPixelAt (0, 0, juce::Colours::white);
        img.setPixelAt (1, 0, juce::Colours::white);
        img.setPixelAt (3, 1, juce::Colours::white.withAlpha (0.5f));   // полупрозрачное — вполовину тише
        auto p = Picture::fromImage (img, "t");
        CHECK (p->w == 4 && p->h == 2, "size");
        CHECK (std::abs (p->boxMean (0, 2, 0, 1) - 1.0f) < 1e-4f, "box: white pair " << p->boxMean (0, 2, 0, 1));
        CHECK (std::abs (p->boxMean (0, 4, 0, 2) - 2.5f / 8.0f) < 0.01f, "box: mean " << p->boxMean (0, 4, 0, 2));
        CHECK (std::abs (p->boxMean (1.5, 2.5, 0, 1) - 0.5f) < 1e-4f, "box: fractional " << p->boxMean (1.5, 2.5, 0, 1));
        CHECK (std::abs (p->boxMean (0.25, 0.5, 0.25, 0.5) - 1.0f) < 1e-4f, "box: inside one pixel");
        CHECK (std::abs (p->at (3, 1) - 0.5f) < 0.02f, "alpha → half " << p->at (3, 1));
        auto big = Picture::fromImage (solid (3000, 1500, juce::Colours::grey), "big");
        CHECK (big->w == Picture::kMaxW && big->h == 512, "fit to max size " << big->w << "x" << big->h);
        for (auto& id : Pictures::factoryIds())
        {
            auto fp = Picture::fromImage (Pictures::makeFactory (id), id);
            CHECK (! fp->empty() && fp->mean > 0.01f && fp->mean < 0.6f, "factory " << id << ": mean " << fp->mean);
        }
        auto tp = Picture::fromImage (Pictures::makeText ("Hello"), "text");
        CHECK (! tp->empty() && tp->mean > 0.02f, "text picture");
        const auto enc = Pictures::encode (Pictures::makeFactory ("enso"));
        const auto dec = Pictures::decode (enc);
        CHECK (dec.isValid() && dec.getWidth() == 1024, "png base64 round trip");
        Look look;
        CHECK (look.amp (1.0f) == 1.0f && look.amp (0.0f) == 0.0f, "look ends");
        look.invert = true;
        CHECK (look.amp (0.0f) == 1.0f, "invert");
    }

    std::cout << "scales\n";
    for (int s = 0; s < 3; ++s)
    {
        FreqMap m (s, 100.0, 10000.0);
        CHECK (std::abs (m.pos (100.0)) < 1e-9 && std::abs (m.pos (10000.0) - 1.0) < 1e-9, "scale " << s << " ends");
        for (double f : { 150.0, 1000.0, 7000.0 })
            CHECK (std::abs (m.freq (m.pos (f)) - f) < 1e-6 * f, "scale " << s << " round trip " << f);
    }
    {
        FreqMap lg (1, 100.0, 10000.0);
        CHECK (std::abs (lg.freq (0.5) - 1000.0) < 0.01, "log midpoint 1 kHz");
        FreqMap ln (0, 0.0, 10000.0);
        CHECK (std::abs (ln.freq (0.5) - 5000.0) < 0.01, "lin midpoint 5 kHz");
    }

    // ---------------------------------------------------------------- прозрачность
    std::cout << "pass-through\n";
    for (int res = 0; res < 4; ++res)
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        setParam (p, ParamId::resolution, (float) res);
        p.prepareToPlay (kRate, kBlock);
        setParam (p, ParamId::mode, 1);
        setParam (p, ParamId::blend, 1);
        setParam (p, ParamId::flatten, 0);
        setParam (p, ParamId::low, 20);
        setParam (p, ParamId::high, 22000);
        p.setPictureImage (solid (64, 64, juce::Colours::white), "white");
        const int N = p.engine.getLatency();
        CHECK (N == 1024 << res, "latency " << N);
        CHECK (p.getLatencySamples() == N, "reported latency");
        juce::AudioBuffer<float> in (2, (int) (kRate * 1.0));
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float v = 0.4f * std::sin (0.0573f * (float) i) + 0.2f * std::sin (0.61f * (float) i);
            in.setSample (0, i, v);
            in.setSample (1, i, -v);
        }
        for (bool playing : { true, false })
        {
            ph.playing = playing;
            ph.ppq = 0;
            p.engine.reset();
            auto out = run (p, ph, 1.0, &in);
            float err = 0;
            for (int i = 2 * N; i < out.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c) err = juce::jmax (err, std::abs (out.getSample (c, i) - in.getSample (c, i - N)));
            CHECK (err < 2e-3f, "FFT " << N << (playing ? " playing" : " stopped") << ": max error " << err);
        }
    }

    // ---------------------------------------------------------------- линия на 1 кГц
    std::cout << "line\n";
    for (int s = 0; s < 3; ++s)
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kRate, kBlock);
        setParam (p, ParamId::mode, 0);
        setParam (p, ParamId::scale, (float) s);
        setParam (p, ParamId::low, 100);
        setParam (p, ParamId::high, 10000);
        setParam (p, ParamId::length, 1);   // 1 такт = 2 с
        juce::Image img = solid (256, 128, juce::Colours::black);
        {
            juce::Graphics g (img);
            g.setColour (juce::Colours::white);
            g.fillRect (0, 63, 256, 2);
        }
        p.setPictureImage (img, "line");
        auto out = run (p, ph, 2.5);
        const auto st = stats (out);
        CHECK (! st.nan && st.peak <= 1.0f && st.rms > 0.003f, "line scale " << s << ": rms " << st.rms);
        const double expect = FreqMap (s, 100, 10000).freq (0.5);
        // средний спектр середины
        constexpr int order = 13, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> acc ((size_t) N / 2 + 1, 0.0f), work ((size_t) N * 2);
        for (int at = (int) (0.6 * kRate); at < (int) (1.8 * kRate); at += N / 2)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            for (int i = 0; i < N; ++i) work[(size_t) i] = out.getSample (0, at + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N));
            fft.performFrequencyOnlyForwardTransform (work.data(), true);
            for (int k = 0; k <= N / 2; ++k) acc[(size_t) k] += work[(size_t) k];
        }
        int best = 1;
        for (int k = 1; k <= N / 2; ++k) if (acc[(size_t) k] > acc[(size_t) best]) best = k;
        const double got = best * kRate / N;
        std::cout << "  scale " << s << ": line at " << expect << " Hz, peak " << got << " Hz\n";
        CHECK (std::abs (got / expect - 1.0) < 0.06, "line scale " << s << ": expected " << expect << " got " << got);
    }

    // ---------------------------------------------------------------- картинка в спектрограмме
    std::cout << "fidelity\n";
    {
        constexpr int cols = 64, rows = 32;
        struct Case { const char* pic; int mode, blend, scale; float minCorr; };
        const Case cases[] = {
            { "enso", 0, 0, 1, 0.75f }, { "mountains", 0, 0, 2, 0.75f }, { "testcard", 0, 0, 0, 0.7f }, { "wordmark", 0, 0, 0, 0.75f },
            { "enso", 1, 1, 1, 0.6f }, { "mountains", 1, 1, 1, 0.6f }, { "wordmark", 1, 0, 0, 0.6f },
        };
        for (auto& cs : cases)
        {
            SumiProcessor p;
            TestPlayHead ph;
            p.setPlayHead (&ph);
            p.prepareToPlay (kRate, kBlock);
            setParam (p, ParamId::mode, (float) cs.mode);
            setParam (p, ParamId::blend, (float) cs.blend);
            setParam (p, ParamId::scale, (float) cs.scale);
            setParam (p, ParamId::low, cs.scale == 0 ? 500.0f : 100.0f);
            setParam (p, ParamId::high, 16000);
            setParam (p, ParamId::length, 2);   // 2 такта = 4 с
            setParam (p, ParamId::flatten, 70);
            setParam (p, ParamId::level, cs.mode == 1 && cs.blend == 0 ? 0.0f : 0.0f);
            p.setPictureSource (cs.pic);
            const auto in = noise (4.5, cs.blend == 0 ? 0.02f : 0.3f, 3);
            auto out = run (p, ph, 4.5, cs.mode == 1 ? &in : nullptr);
            checkAudio (out, juce::String (cs.pic) + " mode " + juce::String (cs.mode));
            const auto ep = p.snapshotParams();
            const auto spec = spectroGrid (out, p.engine.getLatency(), 0.0, 4.0, ep.map, cols, rows);
            const auto ref = pictureGrid (*p.getLatestPicture(), ep.look, cols, rows);
            const float c = correlation (ref, spec, ep.look.rangeDb);
            std::cout << "  " << cs.pic << (cs.mode == 0 ? " paint " : cs.blend == 0 ? " add " : " mask ") << juce::StringArray { "lin", "log", "mel" }[cs.scale]
                      << ": correlation " << juce::String (c, 2) << "\n";
            CHECK (c > cs.minCorr, cs.pic << ": picture not visible in spectrogram, correlation " << c);
        }
    }

    // ---------------------------------------------------------------- такты
    std::cout << "timing\n";
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kRate, kBlock);
        setParam (p, ParamId::mode, 0);
        setParam (p, ParamId::start, 3);    // такт 3 = 4 с при 120 BPM
        setParam (p, ParamId::length, 1);   // 1 такт = 2 с
        setParam (p, ParamId::loop, 0);
        p.setPictureImage (solid (32, 32, juce::Colours::white), "white");
        auto out = run (p, ph, 9.0);
        const int L = p.engine.getLatency();
        auto rmsAt = [&] (double a, double b) { return stats (out, (int) (a * kRate) + L, (int) (b * kRate) + L).rms; };
        const float before = rmsAt (0.5, 3.8), during = rmsAt (4.2, 5.8), after = rmsAt (6.2, 8.5);
        std::cout << "  rms before " << before << ", during " << during << ", after " << after << "\n";
        CHECK (before < 1e-5f && after < 1e-5f && during > 0.05f, "picture is not where bar 3 is");
        CHECK (std::abs (juce::Decibels::gainToDecibels (during) + 14.0f) < 2.5f, "full white = about -14 dBFS rms, got " << juce::Decibels::gainToDecibels (during));
        // начало картинки точно на такте 3 (с учётом задержки) — фронт в пределах одного шага кадра
        int onset = -1;
        for (int i = (int) (3.5 * kRate) + L; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 0.02f) { onset = i - L; break; }
        std::cout << "  onset " << onset / kRate << " s\n";
        CHECK (std::abs (onset / kRate - 4.0) < 0.06, "onset at " << onset / kRate);

        setParam (p, ParamId::loop, 1);
        ph.ppq = 0;
        out = run (p, ph, 9.0);
        CHECK (stats (out, (int) (6.3 * kRate) + L, (int) (7.8 * kRate) + L).rms > 0.05f, "loop repeats after the picture");
        ph.playing = false;
        out = run (p, ph, 1.0);
        CHECK (stats (out, L + 1000).rms < 1e-6f, "stopped transport — Paint silent");
        // без транспорта хоста (Standalone) — свободный ход
        ph.has = false;
        ph.playing = true;
        out = run (p, ph, 3.0);
        CHECK (stats (out, L).rms > 0.02f, "free run without host transport");
    }

    // ---------------------------------------------------------------- Mask вырезает, Add добавляет
    std::cout << "imprint\n";
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kRate, kBlock);
        setParam (p, ParamId::mode, 1);
        setParam (p, ParamId::blend, 1);
        setParam (p, ParamId::flatten, 0);
        setParam (p, ParamId::length, 1);
        juce::Image img = solid (64, 64, juce::Colours::black);
        { juce::Graphics g (img); g.setColour (juce::Colours::white); g.fillRect (32, 0, 32, 64); }   // правая половина белая
        p.setPictureImage (img, "half");
        const auto in = noise (2.5, 0.3f);
        auto out = run (p, ph, 2.5, &in);
        const int L = p.engine.getLatency();
        const float dark = stats (out, (int) (0.2 * kRate) + L, (int) (0.8 * kRate) + L).rms;
        const float lit = stats (out, (int) (1.2 * kRate) + L, (int) (1.8 * kRate) + L).rms;
        std::cout << "  mask: dark " << juce::Decibels::gainToDecibels (dark) << " dB, lit " << juce::Decibels::gainToDecibels (lit) << " dB\n";
        CHECK (lit > dark * 10.0f, "mask does not carve");
        setParam (p, ParamId::mix, 50);
        ph.ppq = 0;
        out = run (p, ph, 2.5, &in);
        const float half = stats (out, (int) (0.2 * kRate) + L, (int) (0.8 * kRate) + L).rms;
        CHECK (half > dark * 3.0f && half < lit, "mix 50% lets the dry sound through");

        setParam (p, ParamId::mix, 100);
        setParam (p, ParamId::blend, 0);
        setParam (p, ParamId::level, 0);
        const auto quiet = noise (2.5, 0.01f);
        ph.ppq = 0;
        out = run (p, ph, 2.5, &quiet);
        const float addDark = stats (out, (int) (0.2 * kRate) + L, (int) (0.8 * kRate) + L).rms;
        const float addLit = stats (out, (int) (1.2 * kRate) + L, (int) (1.8 * kRate) + L).rms;
        CHECK (addLit > addDark * 5.0f && addDark > 0.003f, "add: picture on top, input stays (" << addDark << " / " << addLit << ")");
    }

    // ---------------------------------------------------------------- крайние значения и все размеры FFT
    std::cout << "extremes\n";
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kRate, kBlock);
        const auto in = noise (1.5, 0.9f, 5);
        for (int res = 0; res < 4; ++res)
            for (int mode = 0; mode < 2; ++mode)
                for (int blend = 0; blend < 2; ++blend)
                {
                    p.presets.loadFactory (6);   // Test Card
                    setParam (p, ParamId::resolution, (float) res);
                    setParam (p, ParamId::mode, (float) mode);
                    setParam (p, ParamId::blend, (float) blend);
                    setParam (p, ParamId::level, 12);
                    setParam (p, ParamId::output, 6);
                    setParam (p, ParamId::flatten, 100);
                    setParam (p, ParamId::range, 12);
                    setParam (p, ParamId::low, 20);
                    setParam (p, ParamId::high, 22000);
                    setParam (p, ParamId::tone, (float) (res % 2) * 100.0f);
                    setParam (p, ParamId::length, 0);
                    ph.ppq = 0;
                    auto out = run (p, ph, 1.5, &in);
                    checkAudio (out, "extreme res " + juce::String (res) + " mode " + juce::String (mode) + " blend " + juce::String (blend));
                }
        // High ниже Low, частоты выше Найквиста
        setParam (p, ParamId::low, 20000);
        setParam (p, ParamId::high, 500);
        auto out = run (p, ph, 1.0, &in);
        CHECK (! stats (out).nan && stats (out).peak <= 1.0f, "high below low");
        p.prepareToPlay (22050.0, kBlock);
        setParam (p, ParamId::low, 12000);
        setParam (p, ParamId::high, 22000);
        out = run (p, ph, 1.0, &in);
        CHECK (! stats (out).nan, "band above Nyquist");
        p.clearPicture();
        setParam (p, ParamId::mode, 0);
        out = run (p, ph, 1.0, &in);
        CHECK (stats (out, 2 * p.engine.getLatency()).rms < 1e-6f, "no picture — Paint silent");
    }

    // ---------------------------------------------------------------- состояние
    std::cout << "state\n";
    {
        SumiProcessor a;
        juce::Image img = solid (300, 120, juce::Colours::black);
        { juce::Graphics g (img); g.setColour (juce::Colours::white); g.fillEllipse (100, 20, 80, 80); }
        a.setPictureImage (img, "circle");
        setParam (a, ParamId::scale, 2);
        setParam (a, ParamId::low, 333);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        SumiProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        auto pa = a.getLatestPicture(), pb = b.getLatestPicture();
        CHECK (pb != nullptr && pb->w == 300 && pb->h == 120, "picture restored");
        CHECK (pb != nullptr && std::abs (pb->mean - pa->mean) < 1e-3f, "picture content restored");
        CHECK (b.getPictureName() == "circle", "picture name restored");
        CHECK ((int) b.apvts.getRawParameterValue (ParamId::scale)->load() == 2 && std::abs (b.apvts.getRawParameterValue (ParamId::low)->load() - 333.0f) < 1.0f, "params restored");
        std::cout << "  state with image: " << mb.getSize() / 1024 << " KB\n";
        a.setPictureSource ("text:hello");
        mb.reset();
        a.getStateInformation (mb);
        CHECK (mb.getSize() < 4096, "text picture keeps state small (" << (int) mb.getSize() << " bytes)");
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (b.getPictureSource() == "text:hello" && ! b.getLatestPicture()->empty(), "text picture restored");
    }

    // ---------------------------------------------------------------- пресеты
    std::cout << "presets\n";
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kRate, kBlock);
        const auto in = noise (4.5, 0.25f, 9);
        for (int i = 0; i < p.presets.getNumFactory(); ++i)
        {
            p.presets.loadFactory (i);
            ph.ppq = 0;
            auto out = run (p, ph, 4.0, &in);
            const juce::String nm = factoryPresets()[(size_t) i].name;
            checkAudio (out, "preset " + nm, nm == "Hidden Signature" ? 0.01f : 0.004f);
        }
    }

    // ---------------------------------------------------------------- скорость
    std::cout << "speed\n";
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kRate, kBlock);
        p.presets.loadFactory (7);   // Imprint, Mask, Flatten
        setParam (p, ParamId::resolution, 3);
        const auto in = noise (10.0, 0.3f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (p, ph, 10.0, &in);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  10 s of stereo Imprint at FFT 8192: " << juce::String (ms, 0) << " ms (" << juce::String (10000.0 / ms, 0) << "x realtime)\n";
        CHECK (ms < 2500.0, "too slow");
    }

    // ---------------------------------------------------------------- демо
    std::cout << "demo\n";
    {
        struct Demo { int preset; const char* file; };
        const Demo demos[] = { { 0, "enso" }, { 2, "misty_mountains" }, { 3, "bamboo_organ" }, { 5, "sliding_tones" }, { 7, "carve_mountains" }, { 9, "hidden_signature" } };
        for (auto& d : demos)
        {
            SumiProcessor p;
            TestPlayHead ph;
            p.setPlayHead (&ph);
            p.prepareToPlay (kRate, kBlock);
            p.presets.loadFactory (d.preset);
            setParam (p, ParamId::loop, 0);
            const double bars = lengthBars ((int) p.apvts.getRawParameterValue (ParamId::length)->load());
            const double dur = bars * 2.0;
            // вход для Imprint — аккорд с шумом (как готовый звук)
            juce::AudioBuffer<float> in (2, (int) ((dur + 1.0) * kRate));
            juce::Random r (2);
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) kRate;
                float v = 0.0f;
                for (float f : { 110.0f, 164.8f, 220.0f, 277.2f, 329.6f, 440.0f, 659.3f, 880.0f })
                    v += 0.06f * std::sin (juce::MathConstants<float>::twoPi * f * t + f);
                v += 0.03f * (r.nextFloat() * 2.0f - 1.0f);
                in.setSample (0, i, v);
                in.setSample (1, i, v);
            }
            auto out = run (p, ph, dur + 0.5, &in);
            const int L = p.engine.getLatency();
            writeWav (outDir.getChildFile (juce::String ("demo_") + d.file + ".wav"), out, L);
            const auto ep = p.snapshotParams();
            // подпись в верхах показываем крупно — только её полосу, остальные — весь спектр
            const FreqMap view = juce::String (d.file) == "hidden_signature" ? ep.map
                               : ep.map.scale == 1 ? FreqMap (1, 20.0, 24000.0) : FreqMap (ep.map.scale, 0.0, 24000.0);
            writeSpectrogram (outDir.getChildFile (juce::String ("spec_") + d.file + ".png"), out, L, 0.0, dur, view, ep.look.rangeDb);
        }
    }

    // ---------------------------------------------------------------- интерфейс
    std::cout << "ui\n";
    {
        SumiProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kRate, kBlock);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<SumiEditor*> (base.get());
        CHECK (ed != nullptr, "editor");
        ed->setSize (SumiEditor::kBaseW, SumiEditor::kBaseH);

        auto shoot = [&] (const juce::String& name)
        {
            ed->pumpForTest();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (name + ".png");
            f.deleteFile();
            juce::FileOutputStream fos (f);
            juce::PNGImageFormat().writeImageToStream (img, fos);
            std::cout << "  wrote " << f.getFileName() << "\n";
        };

        const auto in = noise (12.0, 0.25f, 4);
        for (auto theme : { ThemeId::Oxygen, ThemeId::Rubber })
        {
            const juce::String tn = theme == ThemeId::Oxygen ? "oxygen" : "rubber";
            ed->setThemeForTest (theme);

            p.presets.loadFactory (0);   // Enso
            ed->setViewForTest (0);
            ph.ppq = 0;
            run (p, ph, 3.0, nullptr, ed);
            shoot ("ui_" + tn + "_canvas");
            ed->setViewForTest (1);
            ph.ppq = 0;
            ed->getViewForTest().clearLive();
            run (p, ph, 6.4, nullptr, ed);
            shoot ("ui_" + tn + "_live");

            p.presets.loadFactory (7);   // Carve Mountains — Imprint, Mask
            ed->setViewForTest (1);
            ed->getViewForTest().clearLive();
            ph.ppq = 0;
            run (p, ph, 6.0, &in, ed);
            shoot ("ui_" + tn + "_imprint");
            ed->setViewForTest (0);
            p.presets.loadFactory (9);   // Hidden Signature — Add
            run (p, ph, 0.5, &in, ed);
            shoot ("ui_" + tn + "_add");

            p.clearPicture();
            run (p, ph, 0.2, nullptr, ed);
            shoot ("ui_" + tn + "_empty");

            ed->showAboutForTest (true, false);
            shoot ("about_" + tn);
            {
                auto& fog = ed->getAboutForTest().getFogForTest();
                std::cout << "  fog particles: " << fog.getNumParticles() << "\n";
                CHECK (fog.getNumParticles() > 80, "fog: too few particles");
                const auto c = fog.getLocalBounds().getCentre().toFloat();
                fog.blowForTest ({ c.x - 160.0f, c.y }, { c.x + 40.0f, c.y + 10.0f }, 12);
                CHECK (fog.isAnimating(), "fog: did not start");
                fog.stepForTest (90);
                CHECK (! fog.isAnimating(), "fog: did not settle");
            }
            ed->showAboutForTest (true, true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            shoot ("game_" + tn);
            auto& about = ed->getAboutForTest();
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (about.isVisible(), "escape in game closes only the game");
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (! about.isVisible(), "escape closes About");
        }
        ed->setThemeForTest (ThemeId::Oxygen);
        base.reset();
    }

    std::cout << (failures == 0 ? "ALL OK\n" : juce::String (failures) + " FAILURE(S)\n");
    return failures == 0 ? 0 : 1;
}
