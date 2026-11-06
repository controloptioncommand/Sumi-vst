#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sumi
{
namespace
{
float raw (const juce::AudioProcessorValueTreeState& s, const char* id) { return s.getRawParameterValue (id)->load(); }
}

SumiProcessor::SumiProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Sumi", createLayout()),
      presets (apvts, [this] (const juce::String& s) { setPictureSource (s); })
{
    presets.loadFactory (0);
    rebuildPicture();
    startTimerHz (15);
}

SumiProcessor::~SumiProcessor() { stopTimer(); }

bool SumiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

int SumiProcessor::desiredOrder() const { return fftOrderFor ((int) raw (apvts, ParamId::resolution)); }

double SumiProcessor::getTailLengthSeconds() const { return (double) (1 << desiredOrder()) / juce::jmax (8000.0, sr); }

void SumiProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.setOrder (desiredOrder());
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatency());
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw (apvts, ParamId::output)));
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    if (pending == nullptr) pending = latest;
}

// ---------------------------------------------------------------------------
void SumiProcessor::setPictureSource (const juce::String& s)
{
    auto src = s;
    if (src.isNotEmpty() && ! src.containsChar (':')) src = "factory:" + src;
    apvts.state.setProperty ("pictureSource", src, nullptr);
    apvts.state.removeProperty ("pictureData", nullptr);
    apvts.state.removeProperty ("pictureName", nullptr);
    rebuildPicture();
}

void SumiProcessor::setPictureImage (const juce::Image& img, const juce::String& name)
{
    const auto fitted = Pictures::fit (img);
    if (! fitted.isValid()) return;
    apvts.state.setProperty ("pictureSource", "image", nullptr);
    apvts.state.setProperty ("pictureData", Pictures::encode (fitted), nullptr);
    apvts.state.setProperty ("pictureName", name.substring (0, 80), nullptr);
    rebuildPicture();
}

bool SumiProcessor::loadPictureFile (const juce::File& f)
{
    const auto img = Pictures::loadFile (f);
    if (! img.isValid()) return false;
    setPictureImage (img, f.getFileNameWithoutExtension());
    return true;
}

void SumiProcessor::clearPicture() { setPictureSource ({}); }

juce::String SumiProcessor::getPictureName() const
{
    const auto src = getPictureSource();
    if (src.startsWith ("factory:")) return Pictures::factoryName (src.fromFirstOccurrenceOf (":", false, false));
    if (src.startsWith ("text:")) return juce::String::fromUTF8 ("\xe2\x80\x9c") + src.fromFirstOccurrenceOf (":", false, false) + juce::String::fromUTF8 ("\xe2\x80\x9d");
    if (src == "image") return apvts.state.getProperty ("pictureName", "Image").toString();
    return "None";
}

juce::String SumiProcessor::pictureKey() const
{
    return getPictureSource() + "|" + juce::String (apvts.state.getProperty ("pictureData").toString().hashCode64());
}

void SumiProcessor::rebuildPicture()
{
    const auto src = getPictureSource();
    juce::Image img;
    if (src.startsWith ("factory:")) img = Pictures::makeFactory (src.fromFirstOccurrenceOf (":", false, false));
    else if (src.startsWith ("text:")) img = Pictures::makeText (src.fromFirstOccurrenceOf (":", false, false));
    else if (src == "image") img = Pictures::decode (apvts.state.getProperty ("pictureData").toString());
    std::shared_ptr<const Picture> p = Picture::fromImage (img, getPictureName());
    builtKey = pictureKey();
    latest = p;
    keepAlive.push_back (p);
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    pending = p;
}

void SumiProcessor::timerCallback()
{
    if (pictureKey() != builtKey) rebuildPicture();
    // смена размера FFT меняет задержку — сообщаем хосту из потока сообщений
    const int lat = 1 << desiredOrder();
    if (getLatencySamples() != lat) setLatencySamples (lat);
    keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(),
                                     [] (const std::shared_ptr<const Picture>& p) { return p.use_count() == 1; }),
                     keepAlive.end());
}

EngineParams SumiProcessor::snapshotParams() const
{
    EngineParams p;
    p.mode = (int) raw (apvts, ParamId::mode);
    p.blend = (int) raw (apvts, ParamId::blend);
    const float lo = raw (apvts, ParamId::low);
    p.map = FreqMap ((int) raw (apvts, ParamId::scale), lo, juce::jmax (raw (apvts, ParamId::high), lo * 1.05f));
    p.look.gamma = std::pow (2.0f, raw (apvts, ParamId::contrast) / 50.0f);
    p.look.rangeDb = raw (apvts, ParamId::range);
    p.look.invert = raw (apvts, ParamId::invert) > 0.5f;
    p.level = juce::Decibels::decibelsToGain (raw (apvts, ParamId::level));
    p.tone = raw (apvts, ParamId::tone) / 100.0f;
    p.width = raw (apvts, ParamId::width) / 100.0f;
    p.mix = raw (apvts, ParamId::mix) / 100.0f;
    p.flatten = raw (apvts, ParamId::flatten) / 100.0f;
    return p;
}

// ---------------------------------------------------------------------------
void SumiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    {
        const juce::SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked() && pending != nullptr) engine.setPicture (std::move (pending));
    }
    if (engine.getOrder() != desiredOrder()) engine.setOrder (desiredOrder());

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);

    // где мы на шкале DAW
    Timing tm;
    double bpm = 120.0, beatsBar = 4.0;
    bool hostPos = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto q = pos->getPpqPosition())
            {
                hostPos = true;
                tm.ppq = *q;
                tm.running = pos->getIsPlaying();
            }
            if (auto b = pos->getBpm()) bpm = juce::jlimit (10.0, 999.0, *b);
            if (auto ts = pos->getTimeSignature())
                if (ts->numerator > 0 && ts->denominator > 0) beatsBar = ts->numerator * 4.0 / ts->denominator;
        }
    tm.beatsPerSample = bpm / 60.0 / juce::jmax (1.0, getSampleRate() > 0 ? getSampleRate() : sr);
    if (! hostPos)
    {
        // без транспорта хоста (Standalone) — свободный ход, картинка по кругу
        tm.running = true;
        tm.ppq = freePpq;
        freePpq += n * tm.beatsPerSample;
    }
    tm.startBeat = hostPos ? (raw (apvts, ParamId::start) - 1.0) * beatsBar : 0.0;
    tm.lengthBeats = lengthBars ((int) raw (apvts, ParamId::length)) * beatsBar;
    tm.loop = raw (apvts, ParamId::loop) > 0.5f || ! hostPos;
    bar.store (beatsBar);
    hostTransport.store (hostPos);

    const auto p = snapshotParams();
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (numIn, numOut), n, p, tm);

    // громкость и мягкий лимитер (выход не выше 0 dBFS)
    outGain.setTargetValue (juce::Decibels::decibelsToGain (raw (apvts, ParamId::output)));
    const int chans = juce::jmin (2, numOut);
    float* ch[2] = { buffer.getWritePointer (0), chans > 1 ? buffer.getWritePointer (1) : nullptr };
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        for (int c = 0; c < chans; ++c)
        {
            float v = ch[c][i] * g;
            const float a = std::abs (v);
            if (a > 0.8f) v = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), v);
            if (! std::isfinite (v)) v = 0.0f;
            ch[c][i] = v;
            pk = juce::jmax (pk, std::abs (v));
        }
    }

    // в интерфейс: позиция звучащего выхода в картинке, волна, пик
    uiPhase.store ((float) tm.phaseAt ((double) n - engine.getLatency()));
    outPeak.store (juce::jmax (pk, outPeak.load() * 0.85f));
    if (scopeFifo.getFreeSpace() >= n)
    {
        const auto w = scopeFifo.write (n);
        auto put = [&] (int start, int size, int from)
        {
            for (int i = 0; i < size; ++i)
                scopeBuf[(size_t) (start + i)] = chans > 1 ? 0.5f * (ch[0][from + i] + ch[1][from + i]) : ch[0][from + i];
        };
        if (w.blockSize1 > 0) put (w.startIndex1, w.blockSize1, 0);
        if (w.blockSize2 > 0) put (w.startIndex2, w.blockSize2, w.blockSize1);
    }

    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs / juce::jmax (1e-6, n / juce::jmax (1.0, sr)));
    cpuLoad.store (cpuLoad.load() * 0.9f + load * 0.1f);
}

// ---------------------------------------------------------------------------
void SumiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SumiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            rebuildPicture();
        }
}

juce::AudioProcessorEditor* SumiProcessor::createEditor() { return new SumiEditor (*this); }
} // namespace sumi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new sumi::SumiProcessor(); }
