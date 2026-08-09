#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mno
{

enum class MNOParameter : std::size_t
{
    osc1Waveform,
    osc1Shape,
    osc1Tune,
    osc1Level,
    osc2Waveform,
    osc2Shape,
    osc2Tune,
    osc2Level,
    hardSync,
    lfoRate,
    lfoDepth,
    cutoff,
    resonance,
    attack,
    decay,
    sustain,
    release,
    legato,
    glideTime,
    pitchBendRange,
    output,

    // New parameters are appended so existing numeric IDs remain stable.
    glideShape,
    glideTimeRateMorph,
    velocitySensitivity,

    lfoToOsc1Tune,
    lfoToOsc1Shape,
    lfoToOsc1Level,
    lfoToOsc2Tune,
    lfoToOsc2Shape,
    lfoToOsc2Level,
    lfoToCutoff,
    lfoToResonance,

    adsrToOsc1Tune,
    adsrToOsc1Shape,
    adsrToOsc1Level,
    adsrToOsc2Tune,
    adsrToOsc2Shape,
    adsrToOsc2Level,
    adsrToCutoff,
    adsrToResonance,

    // Appended to preserve all existing parameter IDs.
    lfoWaveform,

    lfo2Rate,
    lfo2Depth,
    lfo2Waveform,
    adsr2Attack,
    adsr2Decay,
    adsr2Sustain,
    adsr2Release,

    lfo2ToOsc1Tune,
    lfo2ToOsc1Shape,
    lfo2ToOsc1Level,
    lfo2ToOsc2Tune,
    lfo2ToOsc2Shape,
    lfo2ToOsc2Level,
    lfo2ToCutoff,
    lfo2ToResonance,

    adsr2ToOsc1Tune,
    adsr2ToOsc1Shape,
    adsr2ToOsc1Level,
    adsr2ToOsc2Tune,
    adsr2ToOsc2Shape,
    adsr2ToOsc2Level,
    adsr2ToCutoff,
    adsr2ToResonance,
    count
};

enum class MNOParameterScale
{
    linear,
    logarithmic
};

struct MNOParameterFormatter {};

struct MNOParameterSpec
{
    const char* identifier = "";
    const char* name = "";
    const char* unit = "";
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    float step = 0.0f;
    MNOParameterScale scale = MNOParameterScale::linear;
};

constexpr MNOParameterSpec mnoParameter (
    const char* identifier,
    const char* name,
    const char* unit,
    float minimum,
    float maximum,
    float defaultValue,
    float step,
    int,
    MNOParameterFormatter = {},
    MNOParameterScale scale = MNOParameterScale::linear) noexcept
{
    return {
        identifier, name, unit, minimum, maximum, defaultValue, step, scale
    };
}

inline constexpr int sampleAccurate = 1;

inline constexpr std::array mnoParameterEndpoints
{
    mnoParameter (
        "osc1Waveform", "Osc 1 Wave", "", 0, 4, 0, 1, sampleAccurate),
    mnoParameter (
        "osc1Shape", "Shape", "", 0, 1, 0, 0.0001f, sampleAccurate),
    mnoParameter (
        "osc1Tune", "Tune", " ct", -2400, 2400, 0, 1, sampleAccurate),
    mnoParameter (
        "osc1Level", "Level", "", 0, 1, 1, 0.0001f, sampleAccurate),
    mnoParameter (
        "osc2Waveform", "Osc 2 Wave", "", 0, 5, 2, 1, sampleAccurate),
    mnoParameter (
        "osc2Shape", "Shape", "", 0, 1, 0.5f, 0.0001f, sampleAccurate),
    mnoParameter (
        "osc2Tune", "Tune", " ct", -2400, 2400, 0, 1, sampleAccurate),
    mnoParameter (
        "osc2Level", "Level", "", 0, 1, 0.4f, 0.0001f, sampleAccurate),
    mnoParameter (
        "hardSync", "Hard Sync", "", 0, 1, 0, 1, sampleAccurate),
    mnoParameter (
        "lfoRate", "Rate", " Hz", 0.01f, 40, 0.5f, 0.001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "lfoDepth", "Depth", " %", 0, 100, 100, 0.1f,
        sampleAccurate),
    mnoParameter (
        "cutoff", "Cutoff", " Hz", 20, 20000, 20000, 0.1f, sampleAccurate,
        {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "resonance", "Resonance", " %", 0, 100, 0, 0.1f, sampleAccurate),
    mnoParameter (
        "attack", "Attack", " s", 0.001f, 10, 0.008f, 0.0001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "decay", "Decay", " s", 0.001f, 5, 0.18f, 0.001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "sustain", "Sustain", "", 0, 1, 0.62f, 0.001f, sampleAccurate),
    mnoParameter (
        "release", "Release", " s", 0.001f, 10, 0.6f, 0.001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "legato", "Legato", "", 0, 1, 1, 1, sampleAccurate),
    mnoParameter (
        "glideTime", "Glide Time", " s", 0, 2, 0.08f, 0.001f,
        sampleAccurate),
    mnoParameter (
        "pitchBendRange", "Bend Range", " st", 0, 24, 2, 1,
        sampleAccurate),
    mnoParameter (
        "output", "Output", "", 0, 1, 1, 0.0001f, sampleAccurate),

    mnoParameter (
        "glideShape", "Glide Shape", "", 0, 1, 0.5f, 0.001f,
        sampleAccurate),
    mnoParameter (
        "glideTimeRateMorph", "Time / Rate", "", 0, 1, 0, 0.001f,
        sampleAccurate),
    mnoParameter (
        "velocitySensitivity", "Velocity", " %", 0, 100, 100, 0.1f,
        sampleAccurate),

    mnoParameter (
        "lfoToOsc1Tune", "LFO to Osc 1 Tune", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfoToOsc1Shape", "LFO to Osc 1 Shape", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfoToOsc1Level", "LFO to Osc 1 Level", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfoToOsc2Tune", "LFO to Osc 2 Tune", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfoToOsc2Shape", "LFO to Osc 2 Shape", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfoToOsc2Level", "LFO to Osc 2 Level", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfoToCutoff", "LFO to Cutoff", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfoToResonance", "LFO to Resonance", " %", -100, 100, 0, 0.1f,
        sampleAccurate),

    mnoParameter (
        "adsrToOsc1Tune", "ADSR to Osc 1 Tune", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsrToOsc1Shape", "ADSR to Osc 1 Shape", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsrToOsc1Level", "ADSR to Osc 1 Level", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsrToOsc2Tune", "ADSR to Osc 2 Tune", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsrToOsc2Shape", "ADSR to Osc 2 Shape", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsrToOsc2Level", "ADSR to Osc 2 Level", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsrToCutoff", "ADSR to Cutoff", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsrToResonance", "ADSR to Resonance", " %", -100, 100, 0, 0.1f,
        sampleAccurate),

    mnoParameter (
        "lfoWaveform", "LFO 1 Wave", "", 0, 2, 0, 1, sampleAccurate),

    mnoParameter (
        "lfo2Rate", "Rate", " Hz", 0.01f, 40, 0.5f, 0.001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "lfo2Depth", "Depth", " %", 0, 100, 100, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfo2Waveform", "LFO 2 Wave", "", 0, 2, 0, 1, sampleAccurate),
    mnoParameter (
        "adsr2Attack", "Attack", " s", 0.001f, 10, 0.008f, 0.0001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "adsr2Decay", "Decay", " s", 0.001f, 5, 0.18f, 0.001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),
    mnoParameter (
        "adsr2Sustain", "Sustain", "", 0, 1, 0.62f, 0.001f,
        sampleAccurate),
    mnoParameter (
        "adsr2Release", "Release", " s", 0.001f, 10, 0.6f, 0.001f,
        sampleAccurate, {}, MNOParameterScale::logarithmic),

    mnoParameter (
        "lfo2ToOsc1Tune", "LFO 2 to Osc 1 Tune", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "lfo2ToOsc1Shape", "LFO 2 to Osc 1 Shape", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "lfo2ToOsc1Level", "LFO 2 to Osc 1 Level", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "lfo2ToOsc2Tune", "LFO 2 to Osc 2 Tune", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "lfo2ToOsc2Shape", "LFO 2 to Osc 2 Shape", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "lfo2ToOsc2Level", "LFO 2 to Osc 2 Level", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "lfo2ToCutoff", "LFO 2 to Cutoff", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "lfo2ToResonance", "LFO 2 to Resonance", " %", -100, 100, 0,
        0.1f, sampleAccurate),

    mnoParameter (
        "adsr2ToOsc1Tune", "ADSR 2 to Osc 1 Tune", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "adsr2ToOsc1Shape", "ADSR 2 to Osc 1 Shape", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "adsr2ToOsc1Level", "ADSR 2 to Osc 1 Level", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "adsr2ToOsc2Tune", "ADSR 2 to Osc 2 Tune", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "adsr2ToOsc2Shape", "ADSR 2 to Osc 2 Shape", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "adsr2ToOsc2Level", "ADSR 2 to Osc 2 Level", " %", -100, 100, 0,
        0.1f, sampleAccurate),
    mnoParameter (
        "adsr2ToCutoff", "ADSR 2 to Cutoff", " %", -100, 100, 0, 0.1f,
        sampleAccurate),
    mnoParameter (
        "adsr2ToResonance", "ADSR 2 to Resonance", " %", -100, 100, 0,
        0.1f, sampleAccurate)
};

struct MNOParameterPresentation
{
    float midpoint = 0.0f;
    bool hasMidpoint = false;
};

inline constexpr std::array mnoParameterPresentation
{
    MNOParameterPresentation {},
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation {},
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation {},
    MNOParameterPresentation { 2.0f, true },
    MNOParameterPresentation { 50.0f, true },
    MNOParameterPresentation { 1000.0f, true },
    MNOParameterPresentation { 50.0f, true },
    MNOParameterPresentation { 1.5f, true },
    MNOParameterPresentation { 1.5f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 1.5f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 0.2f, true },
    MNOParameterPresentation { 12.0f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 50.0f, true },

    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },

    MNOParameterPresentation {},

    MNOParameterPresentation { 2.0f, true },
    MNOParameterPresentation { 50.0f, true },
    MNOParameterPresentation {},
    MNOParameterPresentation { 1.5f, true },
    MNOParameterPresentation { 1.5f, true },
    MNOParameterPresentation { 0.5f, true },
    MNOParameterPresentation { 1.5f, true },

    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },

    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true },
    MNOParameterPresentation { 0.0f, true }
};

/// Audio-engine copy of the exact UI mapping. The constants are constructed
/// before rendering; the audio loop only evaluates the selected mapping.
struct MNOParameterMapping
{
    float minimum = 0.0f;
    float maximum = 1.0f;
    float midpoint = 0.5f;
    float logarithmicShape = 1.0f;
    bool hasMidpoint = false;
    bool isLogarithmic = false;

    float normalise (float value) const noexcept
    {
        const auto clamped = std::clamp (
            std::isfinite (value) ? value : minimum, minimum, maximum);
        if (maximum == minimum)
            return 0.0f;

        if (isLogarithmic)
        {
            const auto exponent =
                std::log (clamped / minimum)
                / std::log (maximum / minimum);
            return std::clamp (
                std::pow (std::max (0.0f, exponent),
                          1.0f / logarithmicShape),
                0.0f, 1.0f);
        }

        if (hasMidpoint)
        {
            if (clamped <= midpoint)
                return 0.5f * (clamped - minimum)
                    / (midpoint - minimum);
            return 0.5f + 0.5f * (clamped - midpoint)
                / (maximum - midpoint);
        }

        return (clamped - minimum) / (maximum - minimum);
    }

    float denormalise (float position) const noexcept
    {
        const auto normal = std::clamp (
            std::isfinite (position) ? position : 0.0f, 0.0f, 1.0f);
        if (maximum == minimum)
            return minimum;

        if (isLogarithmic)
            return minimum * std::pow (
                maximum / minimum,
                std::pow (normal, logarithmicShape));

        if (hasMidpoint)
        {
            if (normal <= 0.5f)
                return minimum + (midpoint - minimum) * normal / 0.5f;
            return midpoint + (maximum - midpoint)
                * (normal - 0.5f) / 0.5f;
        }

        return minimum + (maximum - minimum) * normal;
    }
};

inline const auto mnoParameterMappings = []
{
    std::array<MNOParameterMapping, mnoParameterEndpoints.size()> result {};

    for (size_t index = 0; index < result.size(); ++index)
    {
        const auto& endpoint = mnoParameterEndpoints[index];
        const auto presentation = mnoParameterPresentation[index];
        auto& mapping = result[index];
        mapping.minimum = endpoint.minimum;
        mapping.maximum = endpoint.maximum;
        mapping.midpoint = presentation.midpoint;
        mapping.hasMidpoint =
            presentation.hasMidpoint
            && presentation.midpoint > endpoint.minimum
            && presentation.midpoint < endpoint.maximum;
        mapping.isLogarithmic =
            endpoint.scale == MNOParameterScale::logarithmic
            && endpoint.minimum > 0.0f
            && endpoint.maximum > endpoint.minimum;

        if (mapping.isLogarithmic && mapping.hasMidpoint)
        {
            const auto exponentAtHalf =
                std::log (mapping.midpoint / mapping.minimum)
                / std::log (mapping.maximum / mapping.minimum);
            if (exponentAtHalf > 0.0f && exponentAtHalf < 1.0f)
                mapping.logarithmicShape = std::max (
                    1.0e-12f,
                    std::log (exponentAtHalf) / std::log (0.5f));
        }
    }
    return result;
}();

inline const MNOParameterMapping& mnoParameterMapping (
    MNOParameter parameter) noexcept
{
    return mnoParameterMappings[static_cast<size_t> (parameter)];
}

static_assert (
    mnoParameterEndpoints.size()
    == static_cast<size_t> (MNOParameter::count));
static_assert (
    mnoParameterPresentation.size() == mnoParameterEndpoints.size());

} // namespace mno
