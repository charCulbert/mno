#pragma once

#include "MNOParameters.h"

#include <array>
#include <cstddef>

// The factory presets: each is the defaults plus the values it changes.
namespace mno
{

struct PresetValue
{
    MNOParameter parameter;
    double value;
};

struct FactoryPreset
{
    const char* key;
    const char* name;
    const char* feature;
    const PresetValue* values;
    std::size_t valueCount;
};

constexpr PresetValue warmStack[] {
    { MNOParameter::osc1Waveform, 0 },
    { MNOParameter::osc1Shape, 0.10 },
    { MNOParameter::osc1Tune, -7 },
    { MNOParameter::osc1Level, 0.68 },
    { MNOParameter::osc2Waveform, 3 },
    { MNOParameter::osc2Shape, 0.12 },
    { MNOParameter::osc2Tune, 7 },
    { MNOParameter::osc2Level, 0.32 },
    { MNOParameter::cutoff, 2800 },
    { MNOParameter::resonance, 12 },
    { MNOParameter::attack, 0.018 },
    { MNOParameter::decay, 0.55 },
    { MNOParameter::sustain, 0.70 },
    { MNOParameter::release, 0.85 },
    { MNOParameter::adsrToCutoff, 32 },
    { MNOParameter::lfoRate, 0.23 },
    { MNOParameter::lfoToOsc1Tune, 0.1 },
    { MNOParameter::lfoToOsc2Tune, -0.1 },
    { MNOParameter::velocitySensitivity, 75 },
    { MNOParameter::legato, 0 },
    { MNOParameter::lfo2Rate, 0.11 },
    { MNOParameter::lfo2ToOsc1Shape, 6 }
};

constexpr PresetValue rubberBass[] {
    { MNOParameter::osc1Waveform, 2 },
    { MNOParameter::osc1Shape, 0.38 },
    { MNOParameter::osc1Level, 0.72 },
    { MNOParameter::osc2Waveform, 2 },
    { MNOParameter::osc2Shape, 0.58 },
    { MNOParameter::osc2Tune, -1200 },
    { MNOParameter::osc2Level, 0.28 },
    { MNOParameter::cutoff, 180 },
    { MNOParameter::resonance, 20 },
    { MNOParameter::attack, 0.003 },
    { MNOParameter::decay, 0.32 },
    { MNOParameter::sustain, 0.38 },
    { MNOParameter::release, 0.12 },
    { MNOParameter::adsrToCutoff, 52 },
    { MNOParameter::osc1PWMRate, 0.72 },
    { MNOParameter::osc1PWMDepth, 16 },
    { MNOParameter::osc2PWMRate, 0.72 },
    { MNOParameter::osc2PWMDepth, -12 },
    { MNOParameter::velocitySensitivity, 70 },
    { MNOParameter::legato, 1 },
    { MNOParameter::glideTime, 0.055 },
    { MNOParameter::adsr2Attack, 0.001 },
    { MNOParameter::adsr2Decay, 0.09 },
    { MNOParameter::adsr2Sustain, 0 },
    { MNOParameter::adsr2Release, 0.08 },
    { MNOParameter::adsr2ToOsc1Shape, 22 }
};

constexpr PresetValue glassPluck[] {
    { MNOParameter::osc1Waveform, 3 },
    { MNOParameter::osc1Shape, 0.18 },
    { MNOParameter::osc1Level, 0.74 },
    { MNOParameter::osc2Waveform, 4 },
    { MNOParameter::osc2Shape, 0.32 },
    { MNOParameter::osc2Tune, 1200 },
    { MNOParameter::osc2Level, 0.24 },
    { MNOParameter::cutoff, 1200 },
    { MNOParameter::resonance, 26 },
    { MNOParameter::attack, 0.001 },
    { MNOParameter::decay, 0.18 },
    { MNOParameter::sustain, 0 },
    { MNOParameter::release, 0.70 },
    { MNOParameter::adsrToCutoff, 58 },
    { MNOParameter::velocitySensitivity, 100 },
    { MNOParameter::legato, 0 },
    { MNOParameter::adsr2Attack, 0.001 },
    { MNOParameter::adsr2Decay, 0.05 },
    { MNOParameter::adsr2Sustain, 0 },
    { MNOParameter::adsr2Release, 0.05 },
    { MNOParameter::adsr2ToOsc2Tune, 2 },
    { MNOParameter::adsrToResonance, 18 }
};

constexpr PresetValue slowBloom[] {
    { MNOParameter::osc1Waveform, 1 },
    { MNOParameter::osc1Shape, 0.24 },
    { MNOParameter::osc1Tune, -7 },
    { MNOParameter::osc1Level, 0.56 },
    { MNOParameter::osc2Waveform, 3 },
    { MNOParameter::osc2Shape, 0.16 },
    { MNOParameter::osc2Tune, 7 },
    { MNOParameter::cutoff, 600 },
    { MNOParameter::resonance, 14 },
    { MNOParameter::attack, 1.35 },
    { MNOParameter::decay, 1.8 },
    { MNOParameter::sustain, 0.76 },
    { MNOParameter::release, 3.2 },
    { MNOParameter::adsrToCutoff, 44 },
    { MNOParameter::lfoRate, 0.09 },
    { MNOParameter::osc1PWMRate, 0.09 },
    { MNOParameter::osc1PWMDepth, 7 },
    { MNOParameter::osc2PWMRate, 0.09 },
    { MNOParameter::osc2PWMDepth, -5 },
    { MNOParameter::lfoToCutoff, 5 },
    { MNOParameter::lfoToOsc1Tune, 0.1 },
    { MNOParameter::lfoToOsc2Tune, -0.1 },
    { MNOParameter::velocitySensitivity, 55 },
    { MNOParameter::legato, 1 },
    { MNOParameter::glideTime, 0.16 },
    { MNOParameter::osc2Level, 0 },
    { MNOParameter::adsr2Attack, 2.6 },
    { MNOParameter::adsr2Decay, 1.0 },
    { MNOParameter::adsr2Sustain, 1.0 },
    { MNOParameter::adsr2Release, 3.2 },
    { MNOParameter::adsr2ToOsc2Level, 42 },
    { MNOParameter::lfo2Rate, 0.05 },
    { MNOParameter::lfo2Waveform, 1 },
    { MNOParameter::lfo2ToCutoff, 6 },
    { MNOParameter::lfo2ToResonance, 8 }
};

constexpr PresetValue syncLead[] {
    { MNOParameter::osc1Waveform, 0 },
    { MNOParameter::osc1Shape, 0.18 },
    { MNOParameter::osc1Level, 0.25 },
    { MNOParameter::osc2Waveform, 0 },
    { MNOParameter::osc2Shape, 0.35 },
    { MNOParameter::osc2Tune, 700 },
    { MNOParameter::osc2Level, 0.75 },
    { MNOParameter::hardSync, 1 },
    { MNOParameter::cutoff, 1400 },
    { MNOParameter::resonance, 24 },
    { MNOParameter::attack, 0.004 },
    { MNOParameter::decay, 0.24 },
    { MNOParameter::sustain, 0.56 },
    { MNOParameter::release, 0.28 },
    { MNOParameter::adsrToCutoff, 38 },
    { MNOParameter::lfoRate, 5.4 },
    { MNOParameter::lfoToOsc1Tune, 0.1 },
    { MNOParameter::lfoToOsc2Tune, 0.1 },
    { MNOParameter::velocitySensitivity, 80 },
    { MNOParameter::legato, 1 },
    { MNOParameter::glideTime, 0.035 },
    { MNOParameter::adsr2Attack, 0.001 },
    { MNOParameter::adsr2Decay, 0.45 },
    { MNOParameter::adsr2Sustain, 0.12 },
    { MNOParameter::adsr2Release, 0.3 },
    { MNOParameter::adsr2ToOsc2Tune, 30 }
};

constexpr std::array factoryPresets {
    FactoryPreset { "init", "Init", "instrument", nullptr, 0 },
    FactoryPreset { "warm-stack", "Warm Stack", "keys", warmStack, std::size(warmStack) },
    FactoryPreset { "rubber-bass", "Rubber Bass", "bass", rubberBass, std::size(rubberBass) },
    FactoryPreset { "glass-pluck", "Glass Pluck", "pluck", glassPluck, std::size(glassPluck) },
    FactoryPreset { "slow-bloom", "Slow Bloom", "pad", slowBloom, std::size(slowBloom) },
    FactoryPreset { "sync-lead", "Sync Lead", "lead", syncLead, std::size(syncLead) }
};

} // namespace mno
