#include "MNOProcessor.h"

#include "char_clap_utils/AUv3Ramp.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mno
{
namespace
{
constexpr auto automated = CLAP_PARAM_IS_AUTOMATABLE
                         | CLAP_PARAM_IS_MODULATABLE;

constexpr bool isStepped (MNOParameter parameter) noexcept
{
    switch (parameter)
    {
        case MNOParameter::osc1Waveform:
        case MNOParameter::osc2Waveform:
        case MNOParameter::hardSync:
        case MNOParameter::legato:
        case MNOParameter::pitchBendRange:
        case MNOParameter::lfoWaveform:
        case MNOParameter::lfo2Waveform:
            return true;
        default:
            return false;
    }
}

constexpr const char* module (MNOParameter parameter) noexcept
{
    const auto index = static_cast<std::size_t> (parameter);
    if (index <= static_cast<std::size_t> (MNOParameter::osc1Level))
        return "Oscillator 1";
    if (index <= static_cast<std::size_t> (MNOParameter::hardSync))
        return "Oscillator 2";
    if (parameter == MNOParameter::lfoRate
        || parameter == MNOParameter::lfoDepth
        || parameter == MNOParameter::lfoWaveform)
        return "LFO 1";
    if (parameter >= MNOParameter::lfo2Rate
        && parameter <= MNOParameter::lfo2Waveform)
        return "LFO 2";
    if (parameter >= MNOParameter::attack
        && parameter <= MNOParameter::release)
        return "Envelope 1";
    if (parameter >= MNOParameter::adsr2Attack
        && parameter <= MNOParameter::adsr2Release)
        return "Envelope 2";
    if (parameter == MNOParameter::cutoff
        || parameter == MNOParameter::resonance)
        return "Filter";
    if (parameter == MNOParameter::osc1PWMRate
        || parameter == MNOParameter::osc1PWMDepth)
        return "Oscillator 1";
    if (parameter == MNOParameter::osc2PWMRate
        || parameter == MNOParameter::osc2PWMDepth)
        return "Oscillator 2";
    if (index >= static_cast<std::size_t> (MNOParameter::lfoToOsc1Tune))
        return "Modulation";
    return "Performance";
}

const std::array<mno::ParameterDefinition, parameterCount>
definitions = []
{
    std::array<mno::ParameterDefinition, parameterCount> result {};
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        const auto parameter = static_cast<MNOParameter> (index);
        const auto& source = mnoParameterEndpoints[index];
        result[index] = {
            parameterIdBase + static_cast<clap_id> (index),
            source.name,
            module (parameter),
            automated
                | (isStepped (parameter) ? CLAP_PARAM_IS_STEPPED : 0u),
            source.minimum,
            source.maximum,
            source.defaultValue,
            0,
            1,
            nullptr,
            nullptr
        };
    }
    return result;
}();

bool globalAddress (int32_t noteId,
                    int16_t port,
                    int16_t channel,
                    int16_t key) noexcept
{
    return noteId < 0 && port < 0 && channel < 0 && key < 0;
}
}

MNOProcessor::MNOProcessor() noexcept
{
    for (std::size_t index = 0; index < parameters.size(); ++index)
        parameters[index].bind (definitions[index]);
}

const std::array<mno::ParameterDefinition, parameterCount>&
MNOProcessor::parameterDefinitions() noexcept
{
    return definitions;
}

int MNOProcessor::parameterIndex (clap_id id) noexcept
{
    return id >= parameterIdBase && id < parameterIdBase + parameterCount
        ? static_cast<int> (id - parameterIdBase)
        : -1;
}

void MNOProcessor::handleEvent (
    const clap_event_header_t& event,
    uint16_t rampSpace) noexcept
{
    if (event.space_id == rampSpace
        && event.type == char_clap::rampEventType
        && event.size >= sizeof (char_clap::RampEvent))
    {
        const auto& ramp =
            reinterpret_cast<const char_clap::RampEvent&> (event);
        if (const auto index = parameterIndex (ramp.parameterId); index >= 0)
            parameters[static_cast<std::size_t> (index)].beginRamp (
                ramp.target, ramp.durationFrames);
        return;
    }

    if (event.space_id != CLAP_CORE_EVENT_SPACE_ID)
        return;

    if (event.type == CLAP_EVENT_PARAM_VALUE
        && event.size >= sizeof (clap_event_param_value_t))
    {
        const auto& parameter =
            reinterpret_cast<const clap_event_param_value_t&> (event);
        if (const auto index = parameterIndex (parameter.param_id); index >= 0)
            parameters[static_cast<std::size_t> (index)]
                .applyAutomatedBase (parameter.value);
        return;
    }

    if (event.type == CLAP_EVENT_PARAM_MOD
        && event.size >= sizeof (clap_event_param_mod_t))
    {
        const auto& modulation =
            reinterpret_cast<const clap_event_param_mod_t&> (event);
        if (globalAddress (
                modulation.note_id,
                modulation.port_index,
                modulation.channel,
                modulation.key))
            if (const auto index =
                    parameterIndex (modulation.param_id); index >= 0)
                parameters[static_cast<std::size_t> (index)]
                    .applyHostGlobalModulation (modulation.amount);
        return;
    }

    if ((event.type == CLAP_EVENT_NOTE_ON
         || event.type == CLAP_EVENT_NOTE_OFF)
        && event.size >= sizeof (clap_event_note_t))
    {
        const auto& note = reinterpret_cast<const clap_event_note_t&> (event);
        if (event.type == CLAP_EVENT_NOTE_ON && note.velocity > 0.0)
        {
            if (note.key < 0 || note.key > 127
                || note.channel < 0 || note.channel > 15
                || note.port_index < 0)
                return;
            const MNOKey key {
                note.note_id, note.port_index, note.channel, note.key
            };
            applyAction (controller.start (
                key, static_cast<float> (note.key),
                static_cast<float> (note.velocity)));
        }
        else
            applyAction (controller.stopMatching (
                [&note] (const MNOKey& key) noexcept
                {
                    return key.matches (note);
                }));
        return;
    }

    if (event.type != CLAP_EVENT_MIDI
        || event.size < sizeof (clap_event_midi_t))
        return;

    const auto& midi = reinterpret_cast<const clap_event_midi_t&> (event);
    const auto status = midi.data[0] & 0xf0u;
    const auto channel = midi.data[0] & 0x0fu;
    const auto data1 = midi.data[1] & 0x7fu;
    const auto data2 = midi.data[2] & 0x7fu;
    const MNOKey key {
        -1,
        0,
        static_cast<int16_t> (channel),
        static_cast<int16_t> (data1)
    };

    if (status == 0x90u && data2 != 0)
        applyAction (controller.start (
            key, static_cast<float> (data1),
            static_cast<float> (data2) / 127.0f));
    else if (status == 0x80u || status == 0x90u)
        applyAction (controller.stop (key));
    else if (status == 0xb0u)
    {
        if (data1 == 1)
            parameters[static_cast<std::size_t> (MNOParameter::lfoDepth)]
                .applyAutomatedBase (
                    static_cast<double> (data2) * 100.0 / 127.0);
        else if (data1 == 64)
            applyAction (controller.setSustain (data2 >= 64));
        else if (data1 == 120 || data1 == 123)
        {
            applyAction (controller.releaseAll());
            if (data1 == 120)
            {
                envelope1.forceRelease (0.0f);
                envelope2.forceRelease (0.0f);
            }
        }
    }
    else if (status == 0xe0u)
    {
        const auto raw = static_cast<int> (data1)
                       | (static_cast<int> (data2) << 7);
        pitchBend = std::clamp (
            static_cast<float> (raw - 8192) / 8192.0f,
            -1.0f,
            1.0f);
    }
}

void MNOProcessor::flushParameters (
    const clap_input_events_t* rawEvents,
    uint16_t rampSpace,
    bool active) noexcept
{
    const mno::InputEventsView events { rawEvents };
    for (uint32_t index = 0; index < events.size(); ++index)
    {
        const auto* event = events[index];
        if (event == nullptr)
            continue;
        if (active)
        {
            handleEvent (*event, rampSpace);
            continue;
        }
        if (event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && event->type == CLAP_EVENT_PARAM_VALUE
            && event->size >= sizeof (clap_event_param_value_t))
        {
            const auto& value =
                reinterpret_cast<const clap_event_param_value_t&> (*event);
            if (const auto slot = parameterIndex (value.param_id); slot >= 0)
                parameters[static_cast<std::size_t> (slot)]
                    .publishBaseFromMainThread (value.value);
        }
    }
}

} // namespace mno
