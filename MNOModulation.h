#pragma once

#include <array>
#include <cstddef>

#include "MNOParameters.h"

namespace mno
{

enum class MNOModulationSource : size_t
{
    lfo1,
    lfo2,
    adsr1,
    adsr2,
    count
};

inline constexpr size_t mnoModulationSourceCount =
    static_cast<size_t> (MNOModulationSource::count);

struct MNOModulationFrame
{
    std::array<float, mnoModulationSourceCount> values {};

    float get (MNOModulationSource source) const noexcept
    {
        return values[static_cast<size_t> (source)];
    }
};

struct MNOModulationRouteSet
{
    bool isRoutable = false;
    std::array<MNOParameter, mnoModulationSourceCount> amounts {};
};

inline constexpr auto mnoModulationRouteSets = []
{
    std::array<
        MNOModulationRouteSet,
        static_cast<size_t> (MNOParameter::count)
    > result {};

    const auto add = [&] (
        MNOParameter destination,
        MNOParameter lfo1,
        MNOParameter lfo2,
        MNOParameter adsr1,
        MNOParameter adsr2)
    {
        result[static_cast<size_t> (destination)] = {
            true,
            { lfo1, lfo2, adsr1, adsr2 }
        };
    };

    add (
        MNOParameter::osc1Tune,
        MNOParameter::lfoToOsc1Tune,
        MNOParameter::lfo2ToOsc1Tune,
        MNOParameter::adsrToOsc1Tune,
        MNOParameter::adsr2ToOsc1Tune);
    add (
        MNOParameter::osc1Shape,
        MNOParameter::lfoToOsc1Shape,
        MNOParameter::lfo2ToOsc1Shape,
        MNOParameter::adsrToOsc1Shape,
        MNOParameter::adsr2ToOsc1Shape);
    add (
        MNOParameter::osc1Level,
        MNOParameter::lfoToOsc1Level,
        MNOParameter::lfo2ToOsc1Level,
        MNOParameter::adsrToOsc1Level,
        MNOParameter::adsr2ToOsc1Level);
    add (
        MNOParameter::osc2Tune,
        MNOParameter::lfoToOsc2Tune,
        MNOParameter::lfo2ToOsc2Tune,
        MNOParameter::adsrToOsc2Tune,
        MNOParameter::adsr2ToOsc2Tune);
    add (
        MNOParameter::osc2Shape,
        MNOParameter::lfoToOsc2Shape,
        MNOParameter::lfo2ToOsc2Shape,
        MNOParameter::adsrToOsc2Shape,
        MNOParameter::adsr2ToOsc2Shape);
    add (
        MNOParameter::osc2Level,
        MNOParameter::lfoToOsc2Level,
        MNOParameter::lfo2ToOsc2Level,
        MNOParameter::adsrToOsc2Level,
        MNOParameter::adsr2ToOsc2Level);
    add (
        MNOParameter::cutoff,
        MNOParameter::lfoToCutoff,
        MNOParameter::lfo2ToCutoff,
        MNOParameter::adsrToCutoff,
        MNOParameter::adsr2ToCutoff);
    add (
        MNOParameter::resonance,
        MNOParameter::lfoToResonance,
        MNOParameter::lfo2ToResonance,
        MNOParameter::adsrToResonance,
        MNOParameter::adsr2ToResonance);

    return result;
}();

inline const MNOModulationRouteSet& mnoModulationRoutes (
    MNOParameter destination) noexcept
{
    return mnoModulationRouteSets[static_cast<size_t> (destination)];
}

inline float mnoModulatedValue (
    MNOParameter destination,
    float baseValue,
    const MNOModulationFrame& frame,
    const std::array<float, mnoModulationSourceCount>& amounts,
    float directContribution = 0.0f) noexcept
{
    auto contribution = directContribution;
    auto hasRoute = directContribution != 0.0f;

    for (size_t index = 0; index < mnoModulationSourceCount; ++index)
    {
        const auto amount = amounts[index];
        hasRoute = hasRoute || amount != 0.0f;
        contribution += frame.values[index] * amount * 0.01f;
    }

    // Keep the common zero-route path bit-identical to the base signal.
    if (! hasRoute)
        return baseValue;

    const auto& mapping = mnoParameterMapping (destination);
    const auto position = std::clamp (
        mapping.normalise (baseValue) + contribution,
        0.0f,
        1.0f);
    return mapping.denormalise (position);
}

} // namespace mno
