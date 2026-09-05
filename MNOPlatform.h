#pragma once

#include "char_clap_utils/EventChunks.h"
#include "char_clap_utils/ParameterState.h"
#include "char_clap_utils/Process.h"
#include "chardsp/chardsp_ADSREnvelope.h"
#include "chardsp/chardsp_EllipticBLEP.h"
#include "chardsp/chardsp_MonoVoiceController.h"
#include "chardsp/chardsp_OscillatorEvents.h"
#include "chardsp/chardsp_OscillatorPhase.h"
#include "chardsp/chardsp_Portamento.h"
#include "chardsp/chardsp_SimpleDownsampler.h"
#include "chardsp/chardsp_SmoothedValue.h"
#include "chardsp/chardsp_StateVariableFilter.h"

#include <clap/clap.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace mno
{

using char_clap::InputEventsView;
using char_clap::ProcessView;
using char_clap::processEventChunks;

using ParameterFormatter = bool (*)(double, char*, std::size_t);
using ParameterParser = bool (*)(const char*, double&);

struct ParameterDefinition
{
    clap_id id = CLAP_INVALID_ID;
    const char* name = "";
    const char* module = "";
    uint32_t flags = 0;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    uint32_t smoothingFrames = 0;
    uint32_t version = 1;
    ParameterFormatter format = nullptr;
    ParameterParser parse = nullptr;
};

// Bind while stopped; publish/read UI values on main, render and automate on audio.
class ParameterRenderState
{
public:
    void bind(const ParameterDefinition& next) noexcept
    {
        state.emplace(next.minimum, next.maximum, next.defaultValue);
        current = state->clamp(next.defaultValue);
    }

    void publishBaseFromMainThread(double value) noexcept { state->publishBase(value); }
    bool consumePublishedBaseOnAudioThread() noexcept { return state->consumePublishedBase(); }

    void applyAutomatedBase(double value) noexcept
    {
        state->setAutomatedBase(value);
        current = state->clamp(value);
    }

    void applyHostGlobalModulation(double amount) noexcept { state->setGlobalModulation(amount); }
    void beginRamp(double target, uint32_t frames) noexcept { state->beginTimedRamp(target, frames); }
    double nextGlobalValue() noexcept { return current = state->nextValue(); }
    double currentGlobalValue() const noexcept { return current; }
    double baseValueForMainThread() const noexcept { return state->baseValueForMainThread(); }

private:
    std::optional<char_clap::ParameterState> state;
    double current = 0.0;
};

} // namespace mno
