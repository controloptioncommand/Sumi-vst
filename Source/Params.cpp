#include "Params.h"

namespace sumi
{
juce::StringArray lengthNames() { return { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars", "64 bars" }; }
float lengthBars (int i) { return i <= 0 ? 0.5f : (float) (1 << juce::jlimit (0, 6, i - 1)); }
int fftOrderFor (int i) { return 10 + juce::jlimit (0, 3, i); }

juce::String formatHz (float hz)
{
    if (hz >= 1000.0f)
    {
        const float k = hz / 1000.0f;
        return (k >= 10.0f || std::abs (k - std::round (k)) < 0.05f ? juce::String (juce::roundToInt (k)) : juce::String (k, 1)) + " kHz";
    }
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

namespace
{
juce::String minusSign() { return juce::String::fromUTF8 ("\xe2\x88\x92"); }

juce::String signedText (float v, const juce::String& unit, int decimals = 0)
{
    const auto r = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
    if (v > 0.0001f) return "+" + r + unit;
    if (v < -0.0001f) return minusSign() + r.substring (1) + unit;
    return (decimals == 0 ? juce::String ("0") : juce::String (0.0f, decimals)) + unit;
}

std::unique_ptr<juce::AudioParameterFloat> pct (const char* id, const char* name, float lo, float hi, float def, bool bipolar = false)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 1.0f), def,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([bipolar] (float v, int)
        {
            return bipolar ? signedText (v, "%") : juce::String (juce::roundToInt (v)) + "%";
        }));
}

std::unique_ptr<juce::AudioParameterFloat> hz (const char* id, const char* name, float lo, float hi, float def)
{
    // частота — по логарифму: равный поворот ручки = равный интервал
    juce::NormalisableRange<float> r (lo, hi,
        [] (float a, float b, float t) { return a * std::pow (b / a, t); },
        [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
        [] (float, float, float v) { return v < 1000.0f ? std::round (v) : std::round (v / 10.0f) * 10.0f; });
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return formatHz (v); }));
}

std::unique_ptr<juce::AudioParameterFloat> db (const char* id, const char* name, float lo, float hi, float def, int decimals = 1)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
        juce::NormalisableRange<float> (lo, hi, decimals == 0 ? 1.0f : 0.1f), def,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([decimals] (float v, int) { return signedText (v, " dB", decimals); }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    l.add (std::make_unique<C> (juce::ParameterID { ParamId::mode, 1 }, "Mode", juce::StringArray { "Paint", "Imprint" }, 0));
    l.add (std::make_unique<C> (juce::ParameterID { ParamId::blend, 1 }, "Blend", juce::StringArray { "Add", "Mask" }, 1));

    l.add (std::make_unique<C> (juce::ParameterID { ParamId::scale, 1 }, "Scale", juce::StringArray { "Lin", "Log", "Mel" }, 1));
    l.add (hz (ParamId::low, "Low", 20.0f, 20000.0f, 200.0f));
    l.add (hz (ParamId::high, "High", 40.0f, 22000.0f, 16000.0f));
    l.add (pct (ParamId::contrast, "Contrast", -100.0f, 100.0f, 0.0f, true));
    {
        juce::NormalisableRange<float> r (12.0f, 96.0f, 1.0f);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamId::range, 1 }, "Range", r, 60.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " dB"; })));
    }
    l.add (std::make_unique<B> (juce::ParameterID { ParamId::invert, 1 }, "Invert", false));

    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ParamId::start, 1 }, "Start", 1, 256, 1,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return "Bar " + juce::String (v); })));
    l.add (std::make_unique<C> (juce::ParameterID { ParamId::length, 1 }, "Length", lengthNames(), 3));
    l.add (std::make_unique<B> (juce::ParameterID { ParamId::loop, 1 }, "Loop", true));

    l.add (db (ParamId::level, "Level", -48.0f, 12.0f, 0.0f));
    l.add (pct (ParamId::tone, "Tone", 0.0f, 100.0f, 0.0f));
    l.add (pct (ParamId::width, "Width", 0.0f, 100.0f, 100.0f));
    l.add (pct (ParamId::mix, "Mix", 0.0f, 100.0f, 100.0f));
    l.add (pct (ParamId::flatten, "Flatten", 0.0f, 100.0f, 50.0f));
    l.add (std::make_unique<C> (juce::ParameterID { ParamId::resolution, 1 }, "Resolution", juce::StringArray { "1024", "2048", "4096", "8192" }, 2));
    l.add (db (ParamId::output, "Output", -36.0f, 6.0f, 0.0f));
    return l;
}
} // namespace sumi
