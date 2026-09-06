#include "Plugin.h"

#include "MNOProcessor.h"
#include "char_clap_utils/AUv3Ramp.h"

#include <clap/ext/draft/webview.h>
#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{

const void* CLAP_ABI getExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI requestRestart(const clap_host_t*) {}
void CLAP_ABI requestProcess(const clap_host_t*) {}
void CLAP_ABI requestCallback(const clap_host_t*) {}

const clap_host_t host {
    CLAP_VERSION, nullptr, "MNO tests", "Charlie Culbert", "", "0.1.0",
    getExtension, requestRestart, requestProcess, requestCallback
};

std::vector<std::string> webMessages;

bool CLAP_ABI sendWebMessage(const clap_host_t*, const void* data, uint32_t size)
{
    webMessages.emplace_back(static_cast<const char*>(data), size);
    return true;
}

const clap_host_webview_t hostWebview { sendWebMessage };

const void* CLAP_ABI getMNOExtension(const clap_host_t*, const char* id)
{
    return std::strcmp(id, CLAP_EXT_WEBVIEW) == 0 ? &hostWebview : nullptr;
}

const clap_host_t mnoHost {
    CLAP_VERSION, nullptr, "MNO tests", "Charlie Culbert", "", "0.1.0",
    getMNOExtension, requestRestart, requestProcess, requestCallback
};

struct PresetDiscoveryState
{
    bool locationDeclared = false;
    bool pluginIdsValid = true;
    std::vector<std::string> names;
    std::vector<std::string> keys;
};

bool CLAP_ABI declarePresetLocation(
    const clap_preset_discovery_indexer_t* indexer,
    const clap_preset_discovery_location_t* location)
{
    auto& state = *static_cast<PresetDiscoveryState*>(indexer->indexer_data);
    state.locationDeclared = location != nullptr
        && location->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN
        && location->location == nullptr
        && (location->flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) != 0;
    return state.locationDeclared;
}

bool CLAP_ABI beginPreset(
    const clap_preset_discovery_metadata_receiver_t* receiver,
    const char* name, const char* key)
{
    auto& state = *static_cast<PresetDiscoveryState*>(receiver->receiver_data);
    if (name == nullptr || key == nullptr) return false;
    state.names.emplace_back(name);
    state.keys.emplace_back(key);
    return true;
}

void CLAP_ABI addPresetPluginId(
    const clap_preset_discovery_metadata_receiver_t* receiver,
    const clap_universal_plugin_id_t* pluginId)
{
    auto& state = *static_cast<PresetDiscoveryState*>(receiver->receiver_data);
    state.pluginIdsValid = state.pluginIdsValid && pluginId != nullptr
        && std::strcmp(pluginId->abi, "clap") == 0
        && std::strcmp(pluginId->id, example::mno_plugin::descriptor().id) == 0;
}

void check(bool condition)
{
    if (!condition)
        std::abort();
}

void testFactory(const void* factoryPointer, const char* pluginId,
                 bool expectsGui = true)
{
    const auto* factory = static_cast<const clap_plugin_factory_t*>(factoryPointer);
    check(factory != nullptr);
    check(factory->get_plugin_count(factory) == 1);
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
    check(descriptor != nullptr && std::strcmp(descriptor->id, pluginId) == 0);
    const auto* plugin = factory->create_plugin(factory, &host, pluginId);
    check(plugin != nullptr && plugin->init(plugin));
#if defined(__APPLE__)
    if (expectsGui)
    {
        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            plugin->get_extension(plugin, CLAP_EXT_GUI));
        check(gui != nullptr);
        const char* api = nullptr;
        bool floating = true;
        check(gui->get_preferred_api(plugin, &api, &floating));
        check(api != nullptr && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0
              && !floating);
    }
#endif
    plugin->destroy(plugin);
}

template <size_t eventCount>
struct InputEvents
{
    std::array<const clap_event_header_t*, eventCount> events {};

    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t
        {
            return static_cast<uint32_t>(static_cast<const InputEvents*>(list->ctx)->events.size());
        },
        [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t*
        {
            const auto& self = *static_cast<const InputEvents*>(list->ctx);
            return index < self.events.size() ? self.events[index] : nullptr;
        }
    };
};

bool CLAP_ABI discardEvent(const clap_output_events_t*, const clap_event_header_t*) { return true; }

void testParameterPublication()
{
    const mno::ParameterDefinition definition { 0, "Test", "", 0, -100000.0, 100000.0, 0.0 };
    mno::ParameterRenderState parameter;
    parameter.bind(definition);
    check(!parameter.consumePublishedBaseOnAudioThread());
    parameter.publishBaseFromMainThread(1.0);
    parameter.publishBaseFromMainThread(2.0);
    check(parameter.consumePublishedBaseOnAudioThread());
    check(parameter.nextGlobalValue() == 2.0);
    parameter.applyAutomatedBase(3.0);
    check(!parameter.consumePublishedBaseOnAudioThread());
    check(parameter.baseValueForMainThread() == 3.0);
    parameter.beginRamp(5.0, 2);
    check(parameter.nextGlobalValue() == 3.0);
    check(parameter.currentGlobalValue() == 3.0);
    check(parameter.nextGlobalValue() == 4.0);
    check(parameter.nextGlobalValue() == 5.0);
    check(parameter.nextGlobalValue() == 5.0);
    parameter.beginRamp(7.0, 0);
    check(parameter.nextGlobalValue() == 7.0);
    parameter.applyHostGlobalModulation(1.0);
    parameter.publishBaseFromMainThread(6.0);
    parameter.bind(definition);
    check(!parameter.consumePublishedBaseOnAudioThread());
    check(parameter.nextGlobalValue() == 0.0);
    check(parameter.baseValueForMainThread() == 0.0);

    constexpr uint32_t iterations = 100000;
    std::atomic<bool> started { false }, mainDone { false };
    std::thread audio([&]
    {
        double previous = 0.0;
        started.store(true);
        while (previous < iterations)
        {
            if (parameter.consumePublishedBaseOnAudioThread())
            {
                const auto value = parameter.nextGlobalValue();
                // UI updates may coalesce, but must not replay over newer automation.
                check(value > previous && value <= iterations && std::floor(value) == value);
                previous = value;
                parameter.applyAutomatedBase(-value);
            }
            check(parameter.nextGlobalValue() == -previous);
        }
        while (!mainDone.load()) std::this_thread::yield();
        parameter.applyAutomatedBase(-previous);
    });
    while (!started.load()) std::this_thread::yield();
    for (uint32_t i = 1; i <= iterations; ++i)
    {
        parameter.publishBaseFromMainThread(i);
        const auto displayed = parameter.baseValueForMainThread();
        check(std::isfinite(displayed) && std::abs(displayed) <= iterations);
    }
    mainDone.store(true);
    audio.join();
    check(parameter.baseValueForMainThread() == -double(iterations));
}

void testTimedRampAcrossBlocks()
{
    mno::MNOProcessor processor;
    check(processor.prepare(48000.0, 1, 2));
    const auto index = static_cast<size_t>(mno::MNOParameter::cutoff);
    auto& parameter = processor.parameter(index);
    parameter.applyAutomatedBase(1000.0);

    constexpr uint16_t rampSpace = 17;
    const char_clap::RampEvent ramp {
        { sizeof(char_clap::RampEvent), 1, rampSpace, char_clap::rampEventType, 0 },
        processor.parameterDefinitions()[index].id, 2000.0, 2
    };
    InputEvents<1> events { { &ramp.header } };
    InputEvents<0> noEvents;
    const clap_output_events_t outputEvents { nullptr, discardEvent };
    float left[2] {}, right[2] {};
    float* channels[] { left, right };
    clap_audio_buffer_t output { channels, nullptr, 2, 0, 0 };
    clap_process_t process {
        0, 2, nullptr, nullptr, &output, 0, 1, &events.interface, &outputEvents
    };
    check(processor.process(process, rampSpace) != CLAP_PROCESS_ERROR);
    check(parameter.currentGlobalValue() == 1000.0); // Ramp starts at frame 1.
    check(parameter.baseValueForMainThread() == 2000.0);
    process.in_events = &noEvents.interface;
    process.frames_count = 1;
    check(processor.process(process, rampSpace) != CLAP_PROCESS_ERROR);
    check(parameter.currentGlobalValue() == 1500.0);
    check(processor.process(process, rampSpace) != CLAP_PROCESS_ERROR);
    check(parameter.currentGlobalValue() == 2000.0);
}

void testMNOProcessorAndParameters()
{
    PresetDiscoveryState discoveryState;
    clap_preset_discovery_indexer_t indexer {};
    indexer.indexer_data = &discoveryState;
    indexer.declare_location = declarePresetLocation;
    const auto* discoveryFactory = static_cast<const clap_preset_discovery_factory_t*>(
        example::mno_plugin::entryGetFactory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    check(discoveryFactory != nullptr && discoveryFactory->count(discoveryFactory) == 1);
    const auto* providerDescriptor = discoveryFactory->get_descriptor(discoveryFactory, 0);
    check(providerDescriptor != nullptr);
    const auto* provider = discoveryFactory->create(
        discoveryFactory, &indexer, providerDescriptor->id);
    check(provider != nullptr && provider->init(provider));
    check(discoveryState.locationDeclared);

    clap_preset_discovery_metadata_receiver_t receiver {};
    receiver.receiver_data = &discoveryState;
    receiver.begin_preset = beginPreset;
    receiver.add_plugin_id = addPresetPluginId;
    check(provider->get_metadata(
        provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &receiver));
    constexpr std::array expectedNames {
        "Init", "Warm Stack", "Rubber Bass", "Glass Pluck", "Slow Bloom", "Sync Lead"
    };
    constexpr std::array expectedKeys {
        "init", "warm-stack", "rubber-bass", "glass-pluck", "slow-bloom", "sync-lead"
    };
    check(discoveryState.names.size() == expectedNames.size());
    check(discoveryState.keys.size() == expectedKeys.size());
    check(std::equal(discoveryState.names.begin(), discoveryState.names.end(), expectedNames.begin()));
    check(std::equal(discoveryState.keys.begin(), discoveryState.keys.end(), expectedKeys.begin()));
    check(discoveryState.pluginIdsValid);
    provider->destroy(provider);

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        example::mno_plugin::entryGetFactory(CLAP_PLUGIN_FACTORY_ID));
    const auto* plugin = factory->create_plugin(factory, &mnoHost,
                                                example::mno_plugin::descriptor().id);
    check(plugin != nullptr && plugin->init(plugin));
    check(plugin->activate(plugin, 48'000.0, 1, 4096));
    check(plugin->start_processing(plugin));

    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    check(params != nullptr && params->count(plugin) == mno::parameterCount);
    const auto* presetLoad = static_cast<const clap_plugin_preset_load_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD));
    check(presetLoad != nullptr);
    const auto& definitions = mno::MNOProcessor::parameterDefinitions();
    for (uint32_t i = 0; i < mno::parameterCount; ++i)
    {
        clap_param_info_t info {};
        check(params->get_info(plugin, i, &info));
        check(info.id == mno::parameterIdBase + i);
        check(std::strcmp(info.name, definitions[i].name) == 0);
        check(info.min_value == definitions[i].minimum);
        check(info.max_value == definitions[i].maximum);
        check(info.default_value == definitions[i].defaultValue);
    }

    double value = 0.0;
    for (const auto* key : expectedKeys)
    {
        check(presetLoad->from_location(
            plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, key));
        check(params->get_value(
            plugin,
            mno::parameterIdBase + static_cast<clap_id>(mno::MNOParameter::output),
            &value));
        check(value == 1.0);
    }
    check(!presetLoad->from_location(
        plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "missing"));

    check(presetLoad->from_location(
        plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "rubber-bass"));
    check(params->get_value(
        plugin,
        mno::parameterIdBase + static_cast<clap_id>(mno::MNOParameter::osc2Tune),
        &value));
    check(value == -1200.0);
    check(presetLoad->from_location(
        plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    check(params->get_value(
        plugin,
        mno::parameterIdBase + static_cast<clap_id>(mno::MNOParameter::osc2Tune),
        &value));
    check(value == mno::mnoParameterEndpoints[
        static_cast<size_t>(mno::MNOParameter::osc2Tune)].defaultValue);

    const auto* webview = static_cast<const clap_plugin_webview_t*>(
        plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    check(webview != nullptr);
    const auto* gui = static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    check(gui != nullptr);
    check(gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false));
    check(gui->show(plugin));
    check(webview->receive(plugin, "ready", 5));
    webMessages.clear();

    InputEvents<0> noEvents;
    const clap_output_events_t outputEvents { nullptr, discardEvent };
    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    std::array<float*, 2> channels { left.data(), right.data() };
    clap_audio_buffer_t outputBuffer { channels.data(), nullptr, 2, 0, 0 };
    const clap_process_t idleProcess {
        0, static_cast<uint32_t>(left.size()), nullptr, nullptr, &outputBuffer, 0, 1,
        &noEvents.interface, &outputEvents
    };
    check(plugin->process(plugin, &idleProcess) == CLAP_PROCESS_CONTINUE);

    const clap_event_note_t note {
        { sizeof(clap_event_note_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 },
        -1, 0, 0, 69, 1.0
    };
    const clap_event_param_value_t cutoff {
        { sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID,
          CLAP_EVENT_PARAM_VALUE, 0 },
        mno::parameterIdBase + static_cast<clap_id>(mno::MNOParameter::cutoff),
        nullptr, -1, -1, -1, -1, 1234.0
    };
    InputEvents<2> inputEvents { { &cutoff.header, &note.header } };
    const clap_process_t process {
        0, static_cast<uint32_t>(left.size()), nullptr, nullptr, &outputBuffer, 0, 1,
        &inputEvents.interface, &outputEvents
    };
    check(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE);

    float peak = 0.0f;
    for (const auto sample : left)
    {
        check(std::isfinite(sample));
        peak = std::max(peak, std::abs(sample));
    }
    check(peak > 0.001f && peak < 1.0f);

    plugin->on_main_thread(plugin);
    check(std::any_of(webMessages.begin(), webMessages.end(), [](const auto& message)
    {
        const auto scope = message.find("|scope:");
        return message.rfind("mod:", 0) == 0
            && scope != std::string::npos
            && message.size() > scope + 7;
    }));
    check(std::any_of(webMessages.begin(), webMessages.end(), [](const auto& message)
    {
        return message.rfind("values:", 0) == 0
            && message.find("=1234") != std::string::npos;
    }));

    webMessages.clear();
    check(gui->hide(plugin));
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    check(plugin->process(plugin, &idleProcess) == CLAP_PROCESS_CONTINUE);
    plugin->on_main_thread(plugin);
    check(std::none_of(webMessages.begin(), webMessages.end(), [](const auto& message)
    {
        return message.rfind("mod:", 0) == 0;
    }));
    check(std::any_of(left.begin(), left.end(), [](float sample)
    {
        return std::abs(sample) > 0.001f;
    }));

    check(gui->show(plugin));
    plugin->on_main_thread(plugin);
    check(std::any_of(webMessages.begin(), webMessages.end(), [](const auto& message)
    {
        return message.rfind("mod:", 0) == 0;
    }));
    gui->destroy(plugin);

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
}

void testMNOOutputHeadroom()
{
    for (const auto sampleRate : { 44'100.0, 48'000.0, 96'000.0 })
    {
        for (const auto key : { 36, 51, 69, 84 })
        {
            mno::MNOProcessor processor;
            processor.setScopeRing(nullptr);
            check(processor.prepare(sampleRate, 1, 256));
            const clap_event_note_t note {
                { sizeof(clap_event_note_t), 0, CLAP_CORE_EVENT_SPACE_ID,
                  CLAP_EVENT_NOTE_ON, 0 },
                -1, 0, 0, static_cast<int16_t>(key), 1.0
            };
            InputEvents<1> events { { &note.header } };
            InputEvents<0> noEvents;
            std::array<float, 256> samples {};
            float* channels[] { samples.data() };
            clap_audio_buffer_t output { channels, nullptr, 1, 0, 0 };
            clap_process_t process {
                0, 256, nullptr, nullptr, &output, 0, 1, &events.interface, nullptr
            };
            float peak = 0.0f;
            for (int frame = 0; frame < int(sampleRate * 0.25); frame += 256)
            {
                check(processor.process(process, 17) != CLAP_PROCESS_ERROR);
                process.in_events = &noEvents.interface;
                for (const auto sample : samples)
                {
                    check(std::isfinite(sample));
                    peak = std::max(peak, std::abs(sample));
                }
            }
            check(peak > 0.01f && peak < 0.5f);
        }
    }
}

void testMNOScopeCyclePhase()
{
    mno::MNOOscillator oscillator;
    oscillator.prepare(48'000.0f);
    oscillator.reset();
    oscillator.setWaveform(mno::MNOWaveform::saw);

    int wrap = 0;
    int previousBoundary = -1;
    int boundaries = 0;
    for (int frame = 0; frame < 20'000 && boundaries < 5; ++frame)
    {
        const auto output = oscillator.next(997.0f);
        if (output.trigger < 0.5f) continue;
        ++wrap;
        if (oscillator.scopeCyclePhase() != 0) continue;
        if (previousBoundary >= 0) check(wrap - previousBoundary == 2);
        previousBoundary = wrap;
        ++boundaries;
    }
    check(boundaries == 5);
}

} // namespace

int main()
{
    check(example::mno_plugin::entryInit("."));
    testFactory(example::mno_plugin::entryGetFactory(CLAP_PLUGIN_FACTORY_ID),
                example::mno_plugin::descriptor().id);
    testParameterPublication();
    testTimedRampAcrossBlocks();
    testMNOProcessorAndParameters();
    testMNOOutputHeadroom();
    testMNOScopeCyclePhase();
    example::mno_plugin::entryDeinit();
}
