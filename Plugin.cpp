#include "Plugin.h"

#include "char_clap_utils/AUv3Ramp.h"
#include "char_clap_utils/Streams.h"
#include "char_clap_utils/WebUI.h"

#include "char_clap_utils/Process.h"

#include "MNOParameters.h"
#include "MNOProcessor.h"

#include <clap/helpers/param-queue.hh>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <clap/ext/event-registry.h>
#include <clap/factory/preset-discovery.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace example::mno_plugin
{
namespace
{

constexpr char pluginId[] = "com.charlieculbert.mno";

struct PresetValue
{
    mno::MNOParameter parameter;
    double value;
};

struct FactoryPreset
{
    const char* key;
    const char* name;
    const char* feature;
    const PresetValue* values;
    size_t valueCount;
};

constexpr PresetValue warmStack[] {
    { mno::MNOParameter::osc1Waveform, 0 },
    { mno::MNOParameter::osc1Shape, 0.10 },
    { mno::MNOParameter::osc1Tune, -7 },
    { mno::MNOParameter::osc1Level, 0.68 },
    { mno::MNOParameter::osc2Waveform, 3 },
    { mno::MNOParameter::osc2Shape, 0.12 },
    { mno::MNOParameter::osc2Tune, 7 },
    { mno::MNOParameter::osc2Level, 0.32 },
    { mno::MNOParameter::cutoff, 2800 },
    { mno::MNOParameter::resonance, 12 },
    { mno::MNOParameter::attack, 0.018 },
    { mno::MNOParameter::decay, 0.55 },
    { mno::MNOParameter::sustain, 0.70 },
    { mno::MNOParameter::release, 0.85 },
    { mno::MNOParameter::adsrToCutoff, 32 },
    { mno::MNOParameter::lfoRate, 0.23 },
    { mno::MNOParameter::lfoToOsc1Tune, 0.1 },
    { mno::MNOParameter::lfoToOsc2Tune, -0.1 },
    { mno::MNOParameter::velocitySensitivity, 75 },
    { mno::MNOParameter::legato, 0 }
};

constexpr PresetValue rubberBass[] {
    { mno::MNOParameter::osc1Waveform, 2 },
    { mno::MNOParameter::osc1Shape, 0.38 },
    { mno::MNOParameter::osc1Level, 0.72 },
    { mno::MNOParameter::osc2Waveform, 2 },
    { mno::MNOParameter::osc2Shape, 0.58 },
    { mno::MNOParameter::osc2Tune, -1200 },
    { mno::MNOParameter::osc2Level, 0.28 },
    { mno::MNOParameter::cutoff, 180 },
    { mno::MNOParameter::resonance, 20 },
    { mno::MNOParameter::attack, 0.003 },
    { mno::MNOParameter::decay, 0.32 },
    { mno::MNOParameter::sustain, 0.38 },
    { mno::MNOParameter::release, 0.12 },
    { mno::MNOParameter::adsrToCutoff, 52 },
    { mno::MNOParameter::lfoRate, 0.72 },
    { mno::MNOParameter::lfoToOsc1Shape, 16 },
    { mno::MNOParameter::lfoToOsc2Shape, -12 },
    { mno::MNOParameter::velocitySensitivity, 70 },
    { mno::MNOParameter::legato, 1 },
    { mno::MNOParameter::glideTime, 0.055 }
};

constexpr PresetValue glassPluck[] {
    { mno::MNOParameter::osc1Waveform, 3 },
    { mno::MNOParameter::osc1Shape, 0.18 },
    { mno::MNOParameter::osc1Level, 0.74 },
    { mno::MNOParameter::osc2Waveform, 4 },
    { mno::MNOParameter::osc2Shape, 0.32 },
    { mno::MNOParameter::osc2Tune, 1200 },
    { mno::MNOParameter::osc2Level, 0.24 },
    { mno::MNOParameter::cutoff, 1200 },
    { mno::MNOParameter::resonance, 26 },
    { mno::MNOParameter::attack, 0.001 },
    { mno::MNOParameter::decay, 0.18 },
    { mno::MNOParameter::sustain, 0 },
    { mno::MNOParameter::release, 0.70 },
    { mno::MNOParameter::adsrToCutoff, 58 },
    { mno::MNOParameter::velocitySensitivity, 100 },
    { mno::MNOParameter::legato, 0 }
};

constexpr PresetValue slowBloom[] {
    { mno::MNOParameter::osc1Waveform, 1 },
    { mno::MNOParameter::osc1Shape, 0.24 },
    { mno::MNOParameter::osc1Tune, -7 },
    { mno::MNOParameter::osc1Level, 0.56 },
    { mno::MNOParameter::osc2Waveform, 3 },
    { mno::MNOParameter::osc2Shape, 0.16 },
    { mno::MNOParameter::osc2Tune, 7 },
    { mno::MNOParameter::osc2Level, 0.42 },
    { mno::MNOParameter::cutoff, 600 },
    { mno::MNOParameter::resonance, 14 },
    { mno::MNOParameter::attack, 1.35 },
    { mno::MNOParameter::decay, 1.8 },
    { mno::MNOParameter::sustain, 0.76 },
    { mno::MNOParameter::release, 3.2 },
    { mno::MNOParameter::adsrToCutoff, 44 },
    { mno::MNOParameter::lfoRate, 0.09 },
    { mno::MNOParameter::lfoToOsc1Shape, 7 },
    { mno::MNOParameter::lfoToOsc2Shape, -5 },
    { mno::MNOParameter::lfoToCutoff, 5 },
    { mno::MNOParameter::lfoToOsc1Tune, 0.1 },
    { mno::MNOParameter::lfoToOsc2Tune, -0.1 },
    { mno::MNOParameter::velocitySensitivity, 55 },
    { mno::MNOParameter::legato, 1 },
    { mno::MNOParameter::glideTime, 0.16 }
};

constexpr PresetValue syncLead[] {
    { mno::MNOParameter::osc1Waveform, 0 },
    { mno::MNOParameter::osc1Shape, 0.18 },
    { mno::MNOParameter::osc1Level, 0.25 },
    { mno::MNOParameter::osc2Waveform, 0 },
    { mno::MNOParameter::osc2Shape, 0.35 },
    { mno::MNOParameter::osc2Tune, 1200 },
    { mno::MNOParameter::osc2Level, 0.75 },
    { mno::MNOParameter::hardSync, 1 },
    { mno::MNOParameter::cutoff, 1400 },
    { mno::MNOParameter::resonance, 24 },
    { mno::MNOParameter::attack, 0.004 },
    { mno::MNOParameter::decay, 0.24 },
    { mno::MNOParameter::sustain, 0.56 },
    { mno::MNOParameter::release, 0.28 },
    { mno::MNOParameter::adsrToCutoff, 38 },
    { mno::MNOParameter::lfoRate, 5.4 },
    { mno::MNOParameter::lfoToOsc1Tune, 0.1 },
    { mno::MNOParameter::lfoToOsc2Tune, 0.1 },
    { mno::MNOParameter::velocitySensitivity, 80 },
    { mno::MNOParameter::legato, 1 },
    { mno::MNOParameter::glideTime, 0.035 }
};

constexpr std::array factoryPresets {
    FactoryPreset { "init", "Init", "instrument", nullptr, 0 },
    FactoryPreset { "warm-stack", "Warm Stack", "keys", warmStack, std::size(warmStack) },
    FactoryPreset { "rubber-bass", "Rubber Bass", "bass", rubberBass, std::size(rubberBass) },
    FactoryPreset { "glass-pluck", "Glass Pluck", "pluck", glassPluck, std::size(glassPluck) },
    FactoryPreset { "slow-bloom", "Slow Bloom", "pad", slowBloom, std::size(slowBloom) },
    FactoryPreset { "sync-lead", "Sync Lead", "lead", syncLead, std::size(syncLead) }
};

constexpr char presetProviderId[] = "com.charlieculbert.mno.presets";
const clap_preset_discovery_provider_descriptor_t presetProviderDescriptor {
    CLAP_VERSION, presetProviderId, "MNO Factory Presets", "Charlie Culbert"
};

struct PresetProvider
{
    clap_preset_discovery_provider_t interface;
    const clap_preset_discovery_indexer_t* indexer;
};

bool CLAP_ABI presetProviderInit(const clap_preset_discovery_provider_t* provider)
{
    const auto& self = *static_cast<const PresetProvider*>(provider->provider_data);
    if (self.indexer == nullptr || self.indexer->declare_location == nullptr) return false;
    const clap_preset_discovery_location_t location {
        CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
        "MNO Factory Presets",
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr
    };
    return self.indexer->declare_location(self.indexer, &location);
}

void CLAP_ABI presetProviderDestroy(const clap_preset_discovery_provider_t* provider)
{
    delete static_cast<PresetProvider*>(provider->provider_data);
}

bool CLAP_ABI presetProviderGetMetadata(
    const clap_preset_discovery_provider_t*, uint32_t locationKind, const char* location,
    const clap_preset_discovery_metadata_receiver_t* receiver)
{
    if (locationKind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location != nullptr
        || receiver == nullptr || receiver->begin_preset == nullptr)
        return false;

    const clap_universal_plugin_id_t universalId { "clap", pluginId };
    for (const auto& preset : factoryPresets)
    {
        if (!receiver->begin_preset(receiver, preset.name, preset.key)) return false;
        if (receiver->add_plugin_id) receiver->add_plugin_id(receiver, &universalId);
        if (receiver->add_feature) receiver->add_feature(receiver, preset.feature);
    }
    return true;
}

const void* CLAP_ABI presetProviderExtension(
    const clap_preset_discovery_provider_t*, const char*)
{
    return nullptr;
}

uint32_t CLAP_ABI presetProviderCount(const clap_preset_discovery_factory_t*) { return 1; }

const clap_preset_discovery_provider_descriptor_t* CLAP_ABI presetProviderGetDescriptor(
    const clap_preset_discovery_factory_t*, uint32_t index)
{
    return index == 0 ? &presetProviderDescriptor : nullptr;
}

const clap_preset_discovery_provider_t* CLAP_ABI presetProviderCreate(
    const clap_preset_discovery_factory_t*, const clap_preset_discovery_indexer_t* indexer,
    const char* providerId)
{
    if (indexer == nullptr || providerId == nullptr
        || std::strcmp(providerId, presetProviderId) != 0)
        return nullptr;
    auto* provider = new PresetProvider {};
    provider->interface = {
        &presetProviderDescriptor, provider, presetProviderInit, presetProviderDestroy,
        presetProviderGetMetadata, presetProviderExtension
    };
    provider->indexer = indexer;
    return &provider->interface;
}

const clap_preset_discovery_factory_t presetDiscoveryFactory {
    presetProviderCount, presetProviderGetDescriptor, presetProviderCreate
};

enum class EditType : uint8_t { begin, value, end };
struct Edit { EditType type; clap_id id; double value; };

class Plugin final
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Minimal>
{
    using Base = clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                       clap::helpers::CheckingLevel::Minimal>;

public:
    explicit Plugin(const clap_host_t* host)
        : Base(&descriptor(), host),
          host(host),
          ui(host, [this](std::string_view message) { return receiveUI(message); })
    {}

protected:
    bool init() noexcept override
    {
        hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
        hostState = static_cast<const clap_host_state_t*>(host->get_extension(host, CLAP_EXT_STATE));
        if (const auto* registry = static_cast<const clap_host_event_registry_t*>(
                host->get_extension(host, CLAP_EXT_EVENT_REGISTRY)))
            registry->query(host, char_clap::rampEventSpaceName, &rampEventSpace);
        return true;
    }

    bool activate(double sampleRate, uint32_t minimumFrames, uint32_t maximumFrames) noexcept override
    {
        telemetryInterval = std::max<uint32_t>(1, static_cast<uint32_t>(sampleRate / 60.0));
        telemetryFrames = 0;
        active = processor.prepare(sampleRate, minimumFrames, maximumFrames);
        return active;
    }

    void deactivate() noexcept override { active = false; }
    void reset() noexcept override { processor.reset(); }
    bool startProcessing() noexcept override { return true; }

    clap_process_status process(const clap_process_t* process) noexcept override
    {
        if (process == nullptr) return CLAP_PROCESS_ERROR;
        const auto result = processor.process(*process, rampEventSpace);
        if (containsParameterValue(process->in_events)) requestUIValues();
        emitEdits(char_clap::ProcessView { *process }.outputEvents());
        if (uiReady.load(std::memory_order_acquire)
            && uiVisible.load(std::memory_order_acquire))
        {
            telemetryFrames += process->frames_count;
            if (telemetryFrames >= telemetryInterval)
            {
                telemetryFrames %= telemetryInterval;
                if (!telemetryDirty.exchange(true, std::memory_order_acq_rel))
                    host->request_callback(host);
            }
        }
        return result == CLAP_PROCESS_ERROR ? result : CLAP_PROCESS_CONTINUE;
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return isInput ? 0u : 1u; }

    bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept override
    {
        if (isInput || index != 0 || info == nullptr) return false;
        *info = {};
        info->id = 0x4d4e4f55;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        std::snprintf(info->name, sizeof(info->name), "Stereo Output");
        return true;
    }

    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override { return isInput ? 1u : 0u; }

    bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t* info) const noexcept override
    {
        if (!isInput || index != 0 || info == nullptr) return false;
        *info = {};
        info->id = 0x4d4e4e54;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf(info->name, sizeof(info->name), "Notes");
        return true;
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return static_cast<uint32_t>(mno::parameterCount); }

    bool paramsInfo(uint32_t index, clap_param_info_t* info) const noexcept override
    {
        if (index >= mno::parameterCount || info == nullptr) return false;
        const auto& definition = mno::MNOProcessor::parameterDefinitions()[index];
        *info = {};
        info->id = definition.id;
        info->flags = definition.flags;
        info->min_value = definition.minimum;
        info->max_value = definition.maximum;
        info->default_value = definition.defaultValue;
        std::snprintf(info->name, sizeof(info->name), "%s", definition.name);
        std::snprintf(info->module, sizeof(info->module), "%s", definition.module);
        return true;
    }

    bool paramsValue(clap_id id, double* value) noexcept override
    {
        const auto index = mno::MNOProcessor::parameterIndex(id);
        if (index < 0 || value == nullptr) return false;
        *value = processor.parameter(static_cast<size_t>(index)).baseValueForMainThread();
        return true;
    }

    bool paramsValueToText(clap_id id, double value, char* text, uint32_t size) noexcept override
    {
        const auto index = mno::MNOProcessor::parameterIndex(id);
        if (index < 0 || text == nullptr || size == 0) return false;
        const auto& spec = mno::mnoParameterEndpoints[static_cast<size_t>(index)];
        return std::snprintf(text, size, "%.*f%s", precisionFor(spec.step), value, spec.unit) > 0;
    }

    bool paramsTextToValue(clap_id id, const char* text, double* value) noexcept override
    {
        const auto index = mno::MNOProcessor::parameterIndex(id);
        if (index < 0 || text == nullptr || value == nullptr) return false;
        char* end = nullptr;
        const auto parsed = std::strtod(text, &end);
        if (end == text || !std::isfinite(parsed)) return false;
        const auto& spec = mno::mnoParameterEndpoints[static_cast<size_t>(index)];
        *value = std::clamp(parsed, static_cast<double>(spec.minimum),
                           static_cast<double>(spec.maximum));
        return true;
    }

    void paramsFlush(const clap_input_events_t* input, const clap_output_events_t* output) noexcept override
    {
        processor.flushParameters(input, rampEventSpace, active);
        if (containsParameterValue(input)) requestUIValues();
        emitEdits(char_clap::OutputEventsView { output });
    }

    bool implementsState() const noexcept override { return true; }

    bool implementsPresetLoad() const noexcept override { return true; }

    bool presetLoadFromLocation(uint32_t locationKind, const char* location,
                                const char* loadKey) noexcept override
    {
        if (locationKind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location != nullptr
            || loadKey == nullptr)
            return false;
        const auto preset = std::find_if(factoryPresets.begin(), factoryPresets.end(),
                                         [loadKey](const auto& candidate)
                                         {
                                             return std::strcmp(candidate.key, loadKey) == 0;
                                         });
        if (preset == factoryPresets.end()) return false;

        for (size_t i = 0; i < mno::parameterCount; ++i)
            processor.parameter(i).publishBaseFromMainThread(
                mno::mnoParameterEndpoints[i].defaultValue);
        for (size_t i = 0; i < preset->valueCount; ++i)
        {
            const auto& value = preset->values[i];
            processor.parameter(static_cast<size_t>(value.parameter))
                .publishBaseFromMainThread(value.value);
        }
        if (hostState) hostState->mark_dirty(host);
        notifyValuesChanged();
        return true;
    }

    bool stateSave(const clap_ostream_t* stream) noexcept override
    {
        if (stream == nullptr) return false;
        State state;
        for (size_t i = 0; i < mno::parameterCount; ++i)
            state.values[i] = processor.parameter(i).baseValueForMainThread();
        return char_clap::writeComplete(*stream, &state, sizeof(state));
    }

    bool stateLoad(const clap_istream_t* stream) noexcept override
    {
        State state;
        if (stream == nullptr
            || !char_clap::readComplete(*stream, &state, sizeof(state))
            || state.magic != stateMagic || state.version != 1)
            return false;
        for (size_t i = 0; i < mno::parameterCount; ++i)
            processor.parameter(i).publishBaseFromMainThread(state.values[i]);
        notifyValuesChanged();
        return true;
    }

    bool enableDraftExtensions() const noexcept override { return true; }
    bool implementsWebview() const noexcept override { return true; }
    int32_t webviewGetUri(char* uri, uint32_t capacity) const noexcept override
    {
        return ui.getUri(uri, capacity);
    }
    bool webviewGetResource(const char* path, char* mime, uint32_t mimeCapacity,
                            const clap_ostream_t* stream) override
    {
        return ui.getResource(path, mime, mimeCapacity, stream);
    }
    bool webviewReceive(const void* data, uint32_t size) const noexcept override
    {
        return ui.receiveBytes(data, size);
    }

    bool implementsGui() const noexcept override { return true; }
    bool guiIsApiSupported(const char* api, bool floating) noexcept override
    {
        return ui.guiIsApiSupported(api, floating);
    }
    bool guiGetPreferredApi(const char** api, bool* floating) noexcept override
    {
        return ui.guiGetPreferredApi(api, floating);
    }
    bool guiCreate(const char* api, bool floating) noexcept override
    {
        return ui.guiCreate(api, floating, 640, 450);
    }
    void guiDestroy() noexcept override
    {
        uiVisible.store(false, std::memory_order_release);
        uiReady.store(false, std::memory_order_release);
        telemetryDirty.store(false, std::memory_order_release);
        ui.guiDestroy();
    }
    bool guiShow() noexcept override
    {
        if (!ui.guiShow()) return false;
        uiVisible.store(true, std::memory_order_release);
        if (uiReady.load(std::memory_order_acquire)
            && !telemetryDirty.exchange(true, std::memory_order_acq_rel))
            host->request_callback(host);
        return true;
    }
    bool guiHide() noexcept override
    {
        if (!ui.guiHide()) return false;
        uiVisible.store(false, std::memory_order_release);
        telemetryDirty.store(false, std::memory_order_release);
        return true;
    }
    bool guiGetSize(uint32_t* width, uint32_t* height) noexcept override
    {
        return ui.guiGetSize(width, height);
    }
    bool guiCanResize() const noexcept override { return true; }
    bool guiAdjustSize(uint32_t*, uint32_t*) noexcept override { return true; }
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        return ui.guiSetSize(width, height);
    }
    bool guiSetParent(const clap_window_t* window) noexcept override
    {
        return ui.guiSetParent(window);
    }

    const void* extension(const char* id) noexcept override
    {
        if (std::strcmp(id, CLAP_WRAPPER_EXT_AUV3_PARAM_RAMP) != 0) return nullptr;
        static const clap_wrapper_plugin_auv3_param_ramp_t rampExtension {
            CLAP_WRAPPER_AUV3_PARAM_RAMP_ABI_VERSION,
            [](const clap_plugin_t* plugin,
               const clap_wrapper_auv3_param_ramp_info_t* ramp,
               void* storage, uint32_t capacity, uint32_t* size) -> bool
            {
                if (!plugin || !ramp || !size) return false;
                auto& self = static_cast<Plugin&>(Base::from(plugin));
                return char_clap::writeRampEvent(self.rampEventSpace, *ramp,
                                              storage, capacity, *size);
            }
        };
        return &rampExtension;
    }

    void onMainThread() noexcept override
    {
        if (uiDirty.exchange(false, std::memory_order_acq_rel)) sendValues();
        if (telemetryDirty.exchange(false, std::memory_order_acq_rel)
            && uiVisible.load(std::memory_order_acquire))
            sendTelemetry();
    }

private:
    static constexpr uint32_t stateMagic = 0x4d4e4f31;
    struct State
    {
        uint32_t magic = stateMagic;
        uint32_t version = 1;
        std::array<double, mno::parameterCount> values {};
    };

    static int precisionFor(float step) noexcept
    {
        if (step >= 1.0f) return 0;
        if (step >= 0.1f) return 1;
        if (step >= 0.01f) return 2;
        if (step >= 0.001f) return 3;
        return 4;
    }

    bool receiveUI(std::string_view message)
    {
        if (message == "ready")
        {
            uiReady.store(true, std::memory_order_release);
            uiDirty.store(false, std::memory_order_release);
            sendMetadata();
            sendValues();
            if (uiVisible.load(std::memory_order_acquire)) sendTelemetry();
            return true;
        }
        const std::string text(message);
        unsigned id = 0;
        double value = 0.0;
        if (std::sscanf(text.c_str(), "begin:%u", &id) == 1)
            return queueEdit({ EditType::begin, id, 0.0 });
        if (std::sscanf(text.c_str(), "value:%u:%lf", &id, &value) == 2)
        {
            const auto index = mno::MNOProcessor::parameterIndex(id);
            if (index < 0) return false;
            const auto& spec = mno::mnoParameterEndpoints[static_cast<size_t>(index)];
            value = std::clamp(value, static_cast<double>(spec.minimum),
                               static_cast<double>(spec.maximum));
            processor.parameter(static_cast<size_t>(index)).publishBaseFromMainThread(value);
            if (hostState) hostState->mark_dirty(host);
            notifyValuesChanged();
            return queueEdit({ EditType::value, id, value });
        }
        if (std::sscanf(text.c_str(), "end:%u", &id) == 1)
            return queueEdit({ EditType::end, id, 0.0 });
        return false;
    }

    bool queueEdit(const Edit& edit)
    {
        if (mno::MNOProcessor::parameterIndex(edit.id) < 0 || !edits.tryPush(edit)) return false;
        if (hostParams) hostParams->request_flush(host);
        host->request_process(host);
        return true;
    }

    void notifyValuesChanged() noexcept
    {
        requestUIValues();
        if (hostParams) hostParams->rescan(host, CLAP_PARAM_RESCAN_VALUES);
    }

    void requestUIValues() noexcept
    {
        if (!uiDirty.exchange(true, std::memory_order_acq_rel))
            host->request_callback(host);
    }

    static bool containsParameterValue(const clap_input_events_t* rawEvents) noexcept
    {
        const mno::InputEventsView events { rawEvents };
        for (uint32_t index = 0; index < events.size(); ++index)
        {
            const auto* event = events[index];
            if (event != nullptr
                && event->space_id == CLAP_CORE_EVENT_SPACE_ID
                && event->type == CLAP_EVENT_PARAM_VALUE
                && event->size >= sizeof(clap_event_param_value_t))
            {
                const auto& value = reinterpret_cast<const clap_event_param_value_t&>(*event);
                if (mno::MNOProcessor::parameterIndex(value.param_id) >= 0)
                    return true;
            }
        }
        return false;
    }

    void sendValues() const
    {
        std::string message = "values:";
        char pair[64];
        for (size_t i = 0; i < mno::parameterCount; ++i)
        {
            if (i != 0) message += ';';
            const auto id = mno::parameterIdBase + static_cast<clap_id>(i);
            std::snprintf(pair, sizeof(pair), "%u=%.9g", id,
                          processor.parameter(i).baseValueForMainThread());
            message += pair;
        }
        ui.send(message);
    }

    void sendMetadata() const
    {
        for (size_t i = 0; i < mno::parameterCount; ++i)
        {
            const auto& definition = mno::MNOProcessor::parameterDefinitions()[i];
            const auto& spec = mno::mnoParameterEndpoints[i];
            const auto presentation = mno::mnoParameterPresentation[i];
            const char* options = "";
            const auto parameter = static_cast<mno::MNOParameter>(i);
            if (parameter == mno::MNOParameter::osc1Waveform)
                options = "Saw|Ramp|Pulse|Triangle|Sine";
            else if (parameter == mno::MNOParameter::osc2Waveform)
                options = "Saw|Ramp|Pulse|Triangle|Sine|Noise";
            else if (parameter == mno::MNOParameter::lfoWaveform
                     || parameter == mno::MNOParameter::lfo2Waveform)
                options = "Sine|Triangle|Square";
            else if (parameter == mno::MNOParameter::hardSync
                     || parameter == mno::MNOParameter::legato)
                options = "Off|On";

            char message[512];
            std::snprintf(message, sizeof(message),
                          "parameter\t%u\t%s\t%s\t%s\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%d\t%s\t%s",
                          definition.id, spec.identifier, definition.module, definition.name,
                          spec.minimum, spec.maximum, spec.defaultValue, spec.step,
                          presentation.midpoint, presentation.hasMidpoint ? 1 : 0,
                          spec.scale == mno::MNOParameterScale::logarithmic ? "log" : "linear",
                          options);
            std::string withUnit(message);
            withUnit += '\t';
            withUnit += spec.unit;
            ui.send(withUnit);
        }
        ui.send("metadata-end");
    }

    void sendTelemetry() const
    {
        const auto modulation = processor.getModulationState();
        std::array<float, 256> samples {};
        const auto count = processor.copyScope(samples.data(), static_cast<int>(samples.size()));
        char modulationMessage[512];
        std::snprintf(
            modulationMessage, sizeof(modulationMessage),
            "mod:%.6g,%.6g,%.6g,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g",
            modulation.lfoPhase, modulation.lfoValue, modulation.adsrValue,
            modulation.adsrActive, modulation.adsrStage, modulation.adsrStageProgress,
            modulation.adsrVelocityScale, modulation.lfo2Phase, modulation.lfo2Value,
            modulation.adsr2Value, modulation.adsr2Active, modulation.adsr2Stage,
            modulation.adsr2StageProgress, modulation.osc1Shape, modulation.osc2Shape,
            modulation.filterCutoff, modulation.filterResonance);

        std::string message = modulationMessage;
        if (count <= 0)
        {
            ui.send(message);
            return;
        }

        message += "|scope:";
        message.reserve(static_cast<size_t>(count) * 11);
        char value[24];
        for (int i = 0; i < count; ++i)
        {
            if (i != 0) message += ',';
            std::snprintf(value, sizeof(value), "%.6g", samples[static_cast<size_t>(i)]);
            message += value;
        }
        ui.send(message);
    }

    void emitEdits(char_clap::OutputEventsView output) noexcept
    {
        Edit edit;
        while (edits.tryPeek(edit))
        {
            bool pushed = false;
            if (edit.type == EditType::value)
            {
                const clap_event_param_value_t event {
                    { sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID,
                      CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE },
                    edit.id, nullptr, -1, -1, -1, -1, edit.value
                };
                pushed = output.tryPush(event);
            }
            else
            {
                const clap_event_param_gesture_t event {
                    { sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID,
                      static_cast<uint16_t>(edit.type == EditType::begin
                                                ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                : CLAP_EVENT_PARAM_GESTURE_END),
                      CLAP_EVENT_IS_LIVE }, edit.id
                };
                pushed = output.tryPush(event);
            }
            if (!pushed) break;
            edits.consume();
        }
    }

    const clap_host_t* host;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    char_clap::WebUI ui;
    mno::MNOProcessor processor;
    clap::helpers::ParamQueue<Edit, 128> edits;
    std::atomic<bool> uiDirty { true };
    std::atomic<bool> uiReady { false };
    std::atomic<bool> uiVisible { false };
    std::atomic<bool> telemetryDirty { false };
    uint16_t rampEventSpace = UINT16_MAX;
    uint32_t telemetryInterval = 1600;
    uint32_t telemetryFrames = 0;
    bool active = false;
};

uint32_t pluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* pluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &descriptor() : nullptr;
}
const clap_plugin_t* createPlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
{
    if (!host || !id || std::strcmp(id, pluginId) != 0) return nullptr;
    return (new Plugin(host))->clapPlugin();
}

} // namespace

const clap_plugin_descriptor_t& descriptor() noexcept
{
    static const char* features[] {
        CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
        CLAP_PLUGIN_FEATURE_STEREO, nullptr
    };
    static const clap_plugin_descriptor_t value {
        CLAP_VERSION, pluginId, "MNO", "Charlie Culbert",
        "", "", "", "0.1.0", "MNO CLAP example", features
    };
    return value;
}

bool entryInit(const char* path) { return char_clap::setResourceRoot(path); }
void entryDeinit() { char_clap::resourceRoot.clear(); }

const void* entryGetFactory(const char* factoryId)
{
    if (!factoryId) return nullptr;
    if (std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0
        || std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return &presetDiscoveryFactory;
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) != 0) return nullptr;
    static const clap_plugin_factory_t factory { pluginCount, pluginDescriptor, createPlugin };
    return &factory;
}

} // namespace example::mno_plugin
