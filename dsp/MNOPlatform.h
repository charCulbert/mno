#pragma once

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
#include <limits>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mno
{

// A block's input events, by index.
struct InputEventsView
{
    const clap_input_events_t* events;
    uint32_t size() const noexcept { return events && events->size ? events->size(events) : 0; }
    const clap_event_header_t* operator[] (uint32_t i) const noexcept { return events->get(events, i); }
};

// Runs a block in stretches between its events: render(begin, end) up to each event's
// time, then onEvent(event), so every event lands on its sample.
template <typename OnEvent, typename Render>
void processEventChunks (InputEventsView events, uint32_t frames, OnEvent&& onEvent, Render&& render) noexcept
{
    uint32_t at = 0;
    for (uint32_t i = 0; i < events.size(); ++i)
    {
        const auto* event = events[i];
        if (event == nullptr)
            continue;
        const auto time = std::min (event->time, frames);
        if (time > at)
            render (at, time);
        at = std::max (at, time);
        onEvent (*event);
    }
    if (at < frames)
        render (at, frames);
}

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

// One parameter's value: the base the host or page sets, plus the host's modulation.
// Bind while stopped; publish and read on the main thread, render and automate on the
// audio thread. A value published from the main thread reaches the audio thread at its
// next block; the main thread reads back whichever of the two moved it last.
class ParameterRenderState
{
public:
    void bind (const ParameterDefinition& next) noexcept
    {
        minimum = next.minimum;
        maximum = next.maximum;
        base = mainBase = clamp (next.defaultValue);
        audioBase.store (base, std::memory_order_relaxed);
        published.store (none, std::memory_order_relaxed);
        modulation = 0.0;
        current = base;
        latestIsMain.store (false, std::memory_order_relaxed);
    }

    void publishBaseFromMainThread (double value) noexcept
    {
        mainBase = clamp (value);
        published.store (mainBase, std::memory_order_release);
        latestIsMain.store (true, std::memory_order_release);
    }

    bool consumePublishedBaseOnAudioThread() noexcept
    {
        const auto value = published.exchange (none, std::memory_order_acquire);
        if (std::isnan (value))
            return false;
        applyAutomatedBase (value);
        return true;
    }

    void applyAutomatedBase (double value) noexcept
    {
        base = current = clamp (value);
        audioBase.store (base, std::memory_order_relaxed);
        latestIsMain.store (false, std::memory_order_release);
    }

    void applyHostGlobalModulation (double amount) noexcept { modulation = amount; }
    double nextGlobalValue() noexcept { return current = clamp (base + modulation); }
    double currentGlobalValue() const noexcept { return current; }

    double baseValueForMainThread() const noexcept
    {
        return latestIsMain.load (std::memory_order_acquire) ? mainBase : audioBase.load (std::memory_order_relaxed);
    }

private:
    double clamp (double value) const noexcept
    {
        return std::clamp (std::isfinite (value) ? value : minimum, minimum, maximum);
    }

    double minimum = 0.0, maximum = 1.0;
    double base = 0.0, modulation = 0.0, current = 0.0; // audio thread
    double mainBase = 0.0;                              // main thread
    // The latest value from the main thread, or none: the audio thread takes it once.
    static constexpr double none = std::numeric_limits<double>::quiet_NaN();
    std::atomic<double> published { none }, audioBase { 0.0 };
    std::atomic<bool> latestIsMain { false };
};

} // namespace mno
