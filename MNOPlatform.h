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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

class ParameterRenderState
{
public:
    void bind(const ParameterDefinition& next) noexcept
    {
        definition = &next;
        published.store(next.defaultValue);
        mainBase.store(next.defaultValue);
        audioBase.store(next.defaultValue);
        automated = rendered = current = rampTarget = next.defaultValue;
    }

    void publishBaseFromMainThread(double value) noexcept
    {
        const auto next = clamp(value);
        published.store(next);
        publishedGeneration.fetch_add(1, std::memory_order_release);
        mainBase.store(next);
        mainSequence.store(sequence.fetch_add(1, std::memory_order_acq_rel) + 1,
                           std::memory_order_release);
    }

    bool consumePublishedBaseOnAudioThread() noexcept
    {
        const auto generation = publishedGeneration.load(std::memory_order_acquire);
        if (generation == consumedGeneration) return false;
        double value = 0.0;
        if (!published.tryLoad(value)) return false;
        consumedGeneration = generation;
        applyAutomatedBase(value);
        return true;
    }

    void applyAutomatedBase(double value) noexcept
    {
        automated = rendered = current = clamp(value);
        rampFrames = 0;
        audioBase.store(automated);
        audioSequence.store(sequence.fetch_add(1, std::memory_order_acq_rel) + 1,
                            std::memory_order_release);
    }

    void applyHostGlobalModulation(double amount) noexcept { modulation = amount; }

    void beginRamp(double target, uint32_t durationFrames) noexcept
    {
        rampTarget = clamp(target);
        rampFrames = durationFrames;
        rampIncrement = durationFrames == 0
            ? 0.0 : (rampTarget - rendered) / static_cast<double>(durationFrames);
        if (durationFrames == 0) automated = rendered = rampTarget;
        audioBase.store(rampTarget);
        audioSequence.store(sequence.fetch_add(1, std::memory_order_acq_rel) + 1,
                            std::memory_order_release);
    }

    double nextGlobalValue() noexcept
    {
        if (rampFrames != 0)
        {
            rendered += rampIncrement;
            if (--rampFrames == 0) rendered = rampTarget;
        }
        current = clamp(rendered + modulation);
        return current;
    }

    double currentGlobalValue() const noexcept { return current; }

    double baseValueForMainThread() const noexcept
    {
        return mainSequence.load(std::memory_order_acquire)
                   > audioSequence.load(std::memory_order_acquire)
            ? mainBase.load() : audioBase.load();
    }

private:
    double clamp(double value) const noexcept
    {
        if (definition == nullptr) return 0.0;
        if (!std::isfinite(value)) value = definition->minimum;
        return std::clamp(value, definition->minimum, definition->maximum);
    }

    const ParameterDefinition* definition = nullptr;
    char_clap::detail::PublishedDouble published;
    char_clap::detail::PublishedDouble mainBase;
    char_clap::detail::PublishedDouble audioBase;
    std::atomic<uint32_t> publishedGeneration { 0 };
    uint32_t consumedGeneration = 0;
    std::atomic<uint32_t> sequence { 0 };
    std::atomic<uint32_t> mainSequence { 0 };
    std::atomic<uint32_t> audioSequence { 0 };
    double automated = 0.0;
    double rendered = 0.0;
    double current = 0.0;
    double modulation = 0.0;
    double rampTarget = 0.0;
    double rampIncrement = 0.0;
    uint32_t rampFrames = 0;
};

constexpr uint16_t parameterRampEventType = 0;

struct ParameterRampEvent
{
    clap_event_header_t header;
    clap_id paramId;
    double targetValue;
    uint32_t durationFrames;
};

} // namespace mno
