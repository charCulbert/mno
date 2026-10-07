// MNO: a monophonic synthesizer whose interface is a web page (resources/page/),
// shown by the host itself where it supports clap.webview, otherwise in a
// WebView inside the host's window. The sound is in dsp/ (MNOProcessor); this
// file is the CLAP plugin around it.
#include "clap/clap.h"
#include "clap/ext/draft/webview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#include "core/messages.h"
#include "core/resources.h"
#include "webview/gui.h"
#include "plugin.h"
#include "Presets.h"
#include "MNOProcessor.h"

using namespace mno;

static constexpr uint32_t P_COUNT = (uint32_t)parameterCount;

struct MyPlugin
{
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_params_t *hostParams;
    const clap_host_state_t *hostState;
    const clap_host_gui_t *hostGui;
    const clap_host_preset_load_t *hostPresets;
    MNOProcessor processor;
    bool active = false;

    // The page's edits, from the main thread: begin, value, end flags per
    // parameter, emitted to the host by flush or process.
    std::atomic<bool> gestureBegin[P_COUNT], edited[P_COUNT], gestureEnd[P_COUNT];
    std::atomic<double> editedValue[P_COUNT];
    std::atomic<bool> uiReady{false}, valuesDirty{false};

    webview::Gui gui;
    bool guiSizesInPoints = true;
    uint32_t guiWidth = 640, guiHeight = 450;
};

static bool PluginReceiveMessage(MyPlugin *plugin, const core::Value &message);
static void PluginSendValues(MyPlugin *plugin);

static const char *const pluginFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_MONO,
    nullptr,
};

static const clap_plugin_descriptor_t pluginDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = pluginId,
    .name = "MNO",
    .vendor = "Charlie Culbert",
    .url = "https://github.com/charCulbert/mno",
    .manual_url = "",
    .support_url = "",
    .version = "1.0.0",
    .description = "A monophonic synthesizer with two oscillators, envelopes, modulation and a filter",
    .features = pluginFeatures,
};

// The choices of a stepped parameter, or none.
static std::vector<const char *> PluginOptions(uint32_t i)
{
    switch ((MNOParameter)i)
    {
    case MNOParameter::osc1Waveform: return {"Saw", "Ramp", "Pulse", "Triangle", "Sine"};
    case MNOParameter::osc2Waveform: return {"Saw", "Ramp", "Pulse", "Triangle", "Sine", "Noise"};
    case MNOParameter::lfoWaveform:
    case MNOParameter::lfo2Waveform: return {"Sine", "Triangle", "Square"};
    case MNOParameter::hardSync:
    case MNOParameter::legato: return {"Off", "On"};
    default: return {};
    }
}

static int PluginDigits(float step)
{
    return step >= 1 ? 0 : step >= 0.1f ? 1 : step >= 0.01f ? 2 : step >= 0.001f ? 3 : 4;
}

static double PluginClamp(uint32_t i, double value)
{
    const auto &spec = mnoParameterEndpoints[i];
    return std::clamp(std::isfinite(value) ? value : (double)spec.defaultValue, (double)spec.minimum, (double)spec.maximum);
}

static double PluginGetValue(MyPlugin *plugin, uint32_t i)
{
    return plugin->processor.parameter(i).baseValueForMainThread();
}

// A value set on the main thread: the processor takes it at its next block.
static void PluginSetValue(MyPlugin *plugin, uint32_t i, double value)
{
    plugin->processor.parameter(i).publishBaseFromMainThread(PluginClamp(i, value));
}

// After values change on the main thread: the host and the page re-read them.
static void PluginValuesChanged(MyPlugin *plugin)
{
    if (plugin->hostParams)
        plugin->hostParams->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES);
    if (plugin->hostState)
        plugin->hostState->mark_dirty(plugin->host);
    if (plugin->uiReady.load(std::memory_order_acquire))
        PluginSendValues(plugin);
}

static const clap_plugin_note_ports_t extensionNotePorts = {
    .count = [](const clap_plugin_t *plugin, bool isInput) -> uint32_t { return isInput ? 1 : 0; },
    .get = [](const clap_plugin_t *plugin, uint32_t index, bool isInput, clap_note_port_info_t *info) -> bool
    {
        if (!isInput || index)
            return false;
        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        snprintf(info->name, sizeof(info->name), "%s", "Notes");
        return true;
    },
};

static const clap_plugin_audio_ports_t extensionAudioPorts = {
    .count = [](const clap_plugin_t *plugin, bool isInput) -> uint32_t { return isInput ? 0 : 1; },
    .get = [](const clap_plugin_t *plugin, uint32_t index, bool isInput, clap_audio_port_info_t *info) -> bool
    {
        if (isInput || index)
            return false;
        info->id = 0;
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        snprintf(info->name, sizeof(info->name), "%s", "Stereo Output");
        return true;
    },
};

static void PluginSendGesture(const clap_output_events_t *out, clap_id id, uint16_t type)
{
    clap_event_param_gesture_t event = {};
    event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, type, CLAP_EVENT_IS_LIVE};
    event.param_id = id;
    out->try_push(out, &event.header);
}

// The page's edits reach the host as gesture and value events, in order.
static void PluginSyncMainToAudio(MyPlugin *plugin, const clap_output_events_t *out)
{
    for (uint32_t i = 0; i < P_COUNT; i++)
    {
        if (plugin->gestureBegin[i].exchange(false, std::memory_order_acq_rel))
            PluginSendGesture(out, i, CLAP_EVENT_PARAM_GESTURE_BEGIN);
        if (plugin->edited[i].exchange(false, std::memory_order_acq_rel))
        {
            clap_event_param_value_t event = {};
            event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE};
            event.param_id = i;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = plugin->editedValue[i].load(std::memory_order_relaxed);
            out->try_push(out, &event.header);
        }
        if (plugin->gestureEnd[i].exchange(false, std::memory_order_acq_rel))
            PluginSendGesture(out, i, CLAP_EVENT_PARAM_GESTURE_END);
    }
}

// A host value change: the page hears of it on the main thread.
static void PluginNoteHostValues(MyPlugin *plugin, const clap_input_events_t *in)
{
    const uint32_t count = in ? in->size(in) : 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const auto *event = in->get(in, i);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE)
        {
            if (!plugin->valuesDirty.exchange(true, std::memory_order_acq_rel))
                plugin->host->request_callback(plugin->host);
            return;
        }
    }
}

static const clap_plugin_params_t extensionParams = {
    .count = [](const clap_plugin_t *plugin) -> uint32_t { return P_COUNT; },

    .get_info = [](const clap_plugin_t *_plugin, uint32_t index, clap_param_info_t *info) -> bool
    {
        if (index >= P_COUNT)
            return false;
        const auto &definition = MNOProcessor::parameterDefinitions()[index];
        *info = {};
        info->id = index;
        info->flags = definition.flags | (PluginOptions(index).empty() ? 0 : CLAP_PARAM_IS_ENUM);
        info->min_value = definition.minimum;
        info->max_value = definition.maximum;
        info->default_value = definition.defaultValue;
        snprintf(info->name, sizeof(info->name), "%s", definition.name);
        snprintf(info->module, sizeof(info->module), "%s", definition.module);
        return true;
    },

    .get_value = [](const clap_plugin_t *_plugin, clap_id id, double *value) -> bool
    {
        if (id >= P_COUNT)
            return false;
        *value = PluginGetValue((MyPlugin *)_plugin->plugin_data, id);
        return true;
    },

    .value_to_text = [](const clap_plugin_t *_plugin, clap_id id, double value, char *text, uint32_t size) -> bool
    {
        if (id >= P_COUNT || !size)
            return false;
        const auto options = PluginOptions(id);
        const auto &spec = mnoParameterEndpoints[id];
        const int written = !options.empty()
            ? snprintf(text, size, "%s", options[(size_t)std::clamp((int)std::lround(value), 0, (int)options.size() - 1)])
            : snprintf(text, size, "%.*f%s%s", PluginDigits(spec.step), value, *spec.unit ? " " : "", spec.unit);
        return written >= 0 && (uint32_t)written < size;
    },

    .text_to_value = [](const clap_plugin_t *_plugin, clap_id id, const char *text, double *value) -> bool
    {
        if (id >= P_COUNT)
            return false;
        const auto options = PluginOptions(id);
        for (size_t i = 0; i < options.size(); i++)
            if (!strcmp(options[i], text))
            {
                *value = (double)i;
                return true;
            }
        char *end = nullptr;
        const double parsed = strtod(text, &end);
        if (end == text || !std::isfinite(parsed))
            return false;
        *value = PluginClamp(id, parsed);
        return true;
    },

    .flush = [](const clap_plugin_t *_plugin, const clap_input_events_t *in, const clap_output_events_t *out)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        PluginSyncMainToAudio(plugin, out);
        plugin->processor.flushParameters(in, plugin->active);
        PluginNoteHostValues(plugin, in);
    },
};

static bool PluginWriteAll(const clap_ostream_t *stream, const unsigned char *data, uint64_t size)
{
    while (size)
    {
        const int64_t written = stream->write(stream, data, size);
        if (written <= 0)
            return false;
        data += written;
        size -= (uint64_t)written;
    }
    return true;
}

// The state is the parameters as a CBOR map from identifier to value, with a
// version, so later versions can add parameters and still read this one.
static const clap_plugin_state_t extensionState = {
    .save = [](const clap_plugin_t *_plugin, const clap_ostream_t *stream) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        core::Value::Map state{{"version", 1}};
        for (uint32_t i = 0; i < P_COUNT; i++)
            state[mnoParameterEndpoints[i].identifier] = PluginGetValue(plugin, i);
        const auto bytes = core::encode(state);
        return PluginWriteAll(stream, bytes.data(), bytes.size());
    },

    .load = [](const clap_plugin_t *_plugin, const clap_istream_t *stream) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        std::vector<unsigned char> bytes;
        unsigned char buffer[256];
        for (int64_t read; (read = stream->read(stream, buffer, sizeof(buffer))) > 0;)
        {
            bytes.insert(bytes.end(), buffer, buffer + read);
            if (bytes.size() > core::maxMessageBytes)
                return false;
        }
        auto state = core::decode(bytes.data(), bytes.size());
        if (!state || (*state)["version"].number() < 1)
            return false;
        for (uint32_t i = 0; i < P_COUNT; i++)
        {
            const auto &spec = mnoParameterEndpoints[i];
            PluginSetValue(plugin, i, (*state)[spec.identifier].number(spec.defaultValue));
        }
        PluginValuesChanged(plugin);
        return true;
    },
};

static const clap_plugin_preset_load_t extensionPresetLoad = {
    .from_location = [](const clap_plugin_t *_plugin, uint32_t kind, const char *location, const char *key) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location || !key)
            return false;
        for (const auto &preset : factoryPresets)
            if (!strcmp(preset.key, key))
            {
                for (uint32_t i = 0; i < P_COUNT; i++)
                    PluginSetValue(plugin, i, mnoParameterEndpoints[i].defaultValue);
                for (size_t v = 0; v < preset.valueCount; v++)
                    PluginSetValue(plugin, (uint32_t)preset.values[v].parameter, preset.values[v].value);
                PluginValuesChanged(plugin);
                if (plugin->hostPresets)
                    plugin->hostPresets->loaded(plugin->host, kind, location, key);
                return true;
            }
        return false;
    },
};

static const clap_plugin_webview_t extensionWebview = {
    .get_uri = [](const clap_plugin_t *_plugin, char *uri, uint32_t capacity) -> int32_t
    {
        static const char start[] = "/page/index.html";
        if (capacity)
            snprintf(uri, capacity, "%s", start);
        return sizeof(start);
    },

    .get_resource = [](const clap_plugin_t *_plugin, const char *path, char *mime, uint32_t mimeCapacity,
                       const clap_ostream_t *stream) -> bool
    {
        auto resource = core::readResource(path);
        if (!resource || resource->mime.size() >= mimeCapacity)
            return false;
        strcpy(mime, resource->mime.c_str());
        return PluginWriteAll(stream, (const unsigned char *)resource->bytes.data(), resource->bytes.size());
    },

    .receive = [](const clap_plugin_t *_plugin, const void *buffer, uint32_t size) -> bool
    {
        auto message = core::decode(buffer, size);
        return message && PluginReceiveMessage((MyPlugin *)_plugin->plugin_data, *message);
    },
};

// Below the minimum the page clips; above the maximum it only spreads out.
#define GUI_MIN_WIDTH (360)
#define GUI_MIN_HEIGHT (420)
#define GUI_MAX_WIDTH (760)
#define GUI_MAX_HEIGHT (520)

static const clap_plugin_gui_t extensionGui = {
    .is_api_supported = [](const clap_plugin_t *_plugin, const char *api, bool isFloating) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.isApiSupported(api, isFloating); },

    .get_preferred_api = [](const clap_plugin_t *_plugin, const char **api, bool *isFloating) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.getPreferredApi(api, isFloating); },

    .create = [](const clap_plugin_t *_plugin, const char *api, bool isFloating) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (!plugin->gui.create(api, isFloating))
            return false;
        plugin->guiSizesInPoints = strcmp(api, CLAP_WINDOW_API_WIN32) != 0 && strcmp(api, CLAP_WINDOW_API_X11) != 0;
        plugin->gui.setSize(plugin->guiWidth, plugin->guiHeight);
        return true;
    },

    .destroy = [](const clap_plugin_t *_plugin)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        plugin->uiReady.store(false, std::memory_order_release);
        plugin->gui.destroy();
    },

    .set_scale = [](const clap_plugin_t *_plugin, double scale) -> bool { return false; },

    .get_size = [](const clap_plugin_t *_plugin, uint32_t *width, uint32_t *height) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        *width = plugin->guiWidth;
        *height = plugin->guiHeight;
        return true;
    },

    .can_resize = [](const clap_plugin_t *_plugin) -> bool { return true; },

    .get_resize_hints = [](const clap_plugin_t *_plugin, clap_gui_resize_hints_t *hints) -> bool
    {
        hints->can_resize_horizontally = true;
        hints->can_resize_vertically = true;
        hints->preserve_aspect_ratio = false;
        return true;
    },

    .adjust_size = [](const clap_plugin_t *_plugin, uint32_t *width, uint32_t *height) -> bool
    {
        *width = std::clamp<uint32_t>(*width, GUI_MIN_WIDTH, GUI_MAX_WIDTH);
        *height = std::clamp<uint32_t>(*height, GUI_MIN_HEIGHT, GUI_MAX_HEIGHT);
        return true;
    },

    .set_size = [](const clap_plugin_t *_plugin, uint32_t width, uint32_t height) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (width < GUI_MIN_WIDTH || width > GUI_MAX_WIDTH || height < GUI_MIN_HEIGHT || height > GUI_MAX_HEIGHT)
            return false;
        plugin->guiWidth = width;
        plugin->guiHeight = height;
        plugin->gui.setSize(width, height);
        return true;
    },

    .set_parent = [](const clap_plugin_t *_plugin, const clap_window_t *window) -> bool
    { return ((MyPlugin *)_plugin->plugin_data)->gui.setParent(window); },

    .set_transient = [](const clap_plugin_t *_plugin, const clap_window_t *window) -> bool { return false; },

    .suggest_title = [](const clap_plugin_t *_plugin, const char *title) {},

    .show = [](const clap_plugin_t *_plugin) -> bool { return ((MyPlugin *)_plugin->plugin_data)->gui.show(); },

    .hide = [](const clap_plugin_t *_plugin) -> bool { return ((MyPlugin *)_plugin->plugin_data)->gui.hide(); },
};

static const clap_plugin_timer_support_t extensionTimerSupport = {
    .on_timer = [](const clap_plugin_t *_plugin, clap_id timerId)
    { ((MyPlugin *)_plugin->plugin_data)->gui.onTimer(timerId); },
};

// Messages between the page and the plugin (main thread only):
//   page -> plugin  {type: "ready"}
//                   {type: "begin" | "end", id}       a drag starts or ends
//                   {type: "value", id, value}
//                   {type: "visual"}                  on each animation frame
//                   {type: "resize", width, height, scale}
//   plugin -> page  {type: "metadata", parameters: [{id, identifier, module, name, unit, min, max,
//                     initial, step, digits, mid, curve, options}]}
//                   {type: "values", values: [value per parameter id]}
//                   {type: "visual", modulation: [...], scope: [...]}
static void PluginSendMetadata(MyPlugin *plugin)
{
    core::Value::Array list;
    for (uint32_t i = 0; i < P_COUNT; i++)
    {
        const auto &spec = mnoParameterEndpoints[i];
        const auto &presentation = mnoParameterPresentation[i];
        core::Value::Array options;
        for (const char *name : PluginOptions(i))
            options.push_back(name);
        list.push_back(core::Value::Map{
            {"id", (double)i}, {"identifier", spec.identifier}, {"module", MNOProcessor::parameterDefinitions()[i].module},
            {"name", spec.name}, {"unit", spec.unit}, {"min", (double)spec.minimum}, {"max", (double)spec.maximum},
            {"initial", (double)spec.defaultValue}, {"step", (double)spec.step}, {"digits", (double)PluginDigits(spec.step)},
            {"mid", (double)presentation.midpoint}, {"hasMid", presentation.hasMidpoint},
            {"curve", spec.scale == MNOParameterScale::logarithmic ? "log" : "linear"}, {"options", options}});
    }
    plugin->gui.send(core::Value::Map{{"type", "metadata"}, {"parameters", list}});
}

static void PluginSendValues(MyPlugin *plugin)
{
    core::Value::Array list;
    for (uint32_t i = 0; i < P_COUNT; i++)
        list.push_back(PluginGetValue(plugin, i));
    plugin->gui.send(core::Value::Map{{"type", "values"}, {"values", list}});
}

static void PluginSendVisual(MyPlugin *plugin)
{
    const auto m = plugin->processor.getModulationState();
    std::array<float, 256> samples{};
    const int count = plugin->processor.copyScope(samples.data(), (int)samples.size());
    core::Value::Array scope;
    for (int i = 0; i < count; i++)
        scope.push_back((double)samples[(size_t)i]);
    plugin->gui.send(core::Value::Map{
        {"type", "visual"},
        {"modulation", core::Value::Array{m.lfoPhase, m.lfoValue, m.adsrValue, (double)m.adsrActive, (double)m.adsrStage,
                                          m.adsrStageProgress, m.adsrVelocityScale, m.lfo2Phase, m.lfo2Value, m.adsr2Value,
                                          (double)m.adsr2Active, (double)m.adsr2Stage, m.adsr2StageProgress, m.osc1Shape,
                                          m.osc2Shape, m.filterCutoff, m.filterResonance}},
        {"scope", scope}});
}

static bool PluginReceiveMessage(MyPlugin *plugin, const core::Value &message)
{
    const auto type = message["type"].text();
    if (type == "ready")
    {
        plugin->uiReady.store(true, std::memory_order_release);
        PluginSendMetadata(plugin);
        PluginSendValues(plugin);
        return true;
    }
    if (type == "visual")
    {
        PluginSendVisual(plugin);
        return true;
    }
    if (type == "resize")
    {
        const double perPixel = plugin->guiSizesInPoints ? 1 : message["scale"].number(1);
        uint32_t width = (uint32_t)std::max(1.0, message["width"].number(0) * perPixel);
        uint32_t height = (uint32_t)std::max(1.0, message["height"].number(0) * perPixel);
        extensionGui.adjust_size(&plugin->plugin, &width, &height);
        if (plugin->hostGui && (width != plugin->guiWidth || height != plugin->guiHeight))
            plugin->hostGui->request_resize(plugin->host, width, height);
        return true;
    }

    const double number = message["id"].number(-1);
    if (!(number >= 0 && number < P_COUNT && number == std::floor(number)))
        return false;
    const uint32_t i = (uint32_t)number;
    if (type == "value")
    {
        const double value = PluginClamp(i, message["value"].number(PluginGetValue(plugin, i)));
        PluginSetValue(plugin, i, value);
        plugin->editedValue[i].store(value, std::memory_order_relaxed);
        plugin->edited[i].store(true, std::memory_order_release);
        if (plugin->hostState)
            plugin->hostState->mark_dirty(plugin->host);
    }
    else if (type == "begin")
        plugin->gestureBegin[i].store(true, std::memory_order_release);
    else if (type == "end")
        plugin->gestureEnd[i].store(true, std::memory_order_release);
    else
        return false;
    if (plugin->hostParams)
        plugin->hostParams->request_flush(plugin->host);
    plugin->host->request_process(plugin->host);
    return true;
}

static const clap_plugin_t pluginClass = {
    .desc = &pluginDescriptor,
    .plugin_data = nullptr,

    .init = [](const clap_plugin *_plugin) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        const auto *host = plugin->host;
        plugin->hostParams = (const clap_host_params_t *)host->get_extension(host, CLAP_EXT_PARAMS);
        plugin->hostState = (const clap_host_state_t *)host->get_extension(host, CLAP_EXT_STATE);
        plugin->hostGui = (const clap_host_gui_t *)host->get_extension(host, CLAP_EXT_GUI);
        plugin->hostPresets = (const clap_host_preset_load_t *)host->get_extension(host, CLAP_EXT_PRESET_LOAD);
        plugin->gui.init(_plugin, host);
        return true;
    },

    .destroy = [](const clap_plugin *_plugin)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        plugin->gui.destroy();
        delete plugin;
    },

    .activate = [](const clap_plugin *_plugin, double sampleRate, uint32_t minimumFrames, uint32_t maximumFrames) -> bool
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        plugin->active = plugin->processor.prepare(sampleRate, minimumFrames, maximumFrames);
        return plugin->active;
    },

    .deactivate = [](const clap_plugin *_plugin) { ((MyPlugin *)_plugin->plugin_data)->active = false; },

    .start_processing = [](const clap_plugin *_plugin) -> bool { return true; },

    .stop_processing = [](const clap_plugin *_plugin) {},

    .reset = [](const clap_plugin *_plugin) { ((MyPlugin *)_plugin->plugin_data)->processor.reset(); },

    .process = [](const clap_plugin *_plugin, const clap_process_t *process) -> clap_process_status
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        PluginSyncMainToAudio(plugin, process->out_events);
        const auto status = plugin->processor.process(*process);
        PluginNoteHostValues(plugin, process->in_events);
        return status == CLAP_PROCESS_ERROR ? status : CLAP_PROCESS_CONTINUE;
    },

    .get_extension = [](const clap_plugin *plugin, const char *id) -> const void *
    {
        if (!strcmp(id, CLAP_EXT_NOTE_PORTS))
            return &extensionNotePorts;
        if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
            return &extensionAudioPorts;
        if (!strcmp(id, CLAP_EXT_PARAMS))
            return &extensionParams;
        if (!strcmp(id, CLAP_EXT_STATE))
            return &extensionState;
        if (!strcmp(id, CLAP_EXT_PRESET_LOAD))
            return &extensionPresetLoad;
        if (!strcmp(id, CLAP_EXT_WEBVIEW))
            return &extensionWebview;
        if (!strcmp(id, CLAP_EXT_GUI))
            return &extensionGui;
        if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT))
            return &extensionTimerSupport;
        return nullptr;
    },

    .on_main_thread = [](const clap_plugin *_plugin)
    {
        MyPlugin *plugin = (MyPlugin *)_plugin->plugin_data;
        if (plugin->valuesDirty.exchange(false, std::memory_order_acq_rel) && plugin->uiReady.load(std::memory_order_acquire))
            PluginSendValues(plugin);
    },
};

static const clap_preset_discovery_provider_descriptor_t presetProviderDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "com.charlieculbert.mno.presets",
    .name = "MNO Presets",
    .vendor = "Charlie Culbert",
};

struct PresetProvider
{
    clap_preset_discovery_provider_t provider;
    const clap_preset_discovery_indexer_t *indexer;
};

static const clap_preset_discovery_provider_t presetProviderClass = {
    .desc = &presetProviderDescriptor,
    .provider_data = nullptr,

    .init = [](const clap_preset_discovery_provider_t *provider) -> bool
    {
        static const clap_preset_discovery_location_t location = {
            .flags = CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
            .name = "Factory Presets",
            .kind = CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
            .location = nullptr,
        };
        const auto *indexer = ((PresetProvider *)provider->provider_data)->indexer;
        return indexer->declare_location(indexer, &location);
    },

    .destroy = [](const clap_preset_discovery_provider_t *provider) { delete (PresetProvider *)provider->provider_data; },

    .get_metadata = [](const clap_preset_discovery_provider_t *provider, uint32_t kind, const char *location,
                       const clap_preset_discovery_metadata_receiver_t *receiver) -> bool
    {
        if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location || !receiver)
            return false;
        const clap_universal_plugin_id_t plugin = {.abi = "clap", .id = pluginId};
        for (const auto &preset : factoryPresets)
        {
            if (!receiver->begin_preset(receiver, preset.name, preset.key))
                return false;
            receiver->add_plugin_id(receiver, &plugin);
            receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
            receiver->add_creator(receiver, "Charlie Culbert");
            receiver->add_feature(receiver, preset.feature);
        }
        return true;
    },

    .get_extension = [](const clap_preset_discovery_provider_t *provider, const char *id) -> const void * { return nullptr; },
};

static const clap_preset_discovery_factory_t presetDiscoveryFactory = {
    .count = [](const clap_preset_discovery_factory_t *factory) -> uint32_t { return 1; },

    .get_descriptor = [](const clap_preset_discovery_factory_t *factory, uint32_t index)
        -> const clap_preset_discovery_provider_descriptor_t * { return index == 0 ? &presetProviderDescriptor : nullptr; },

    .create = [](const clap_preset_discovery_factory_t *factory, const clap_preset_discovery_indexer_t *indexer,
                 const char *id) -> const clap_preset_discovery_provider_t *
    {
        if (!indexer || !id || strcmp(id, presetProviderDescriptor.id))
            return nullptr;
        PresetProvider *provider = new PresetProvider{presetProviderClass, indexer};
        provider->provider.provider_data = provider;
        return &provider->provider;
    },
};

const clap_plugin_descriptor_t *getPluginDescriptor() { return &pluginDescriptor; }

const clap_preset_discovery_factory_t *getPresetDiscoveryFactory() { return &presetDiscoveryFactory; }

const clap_plugin_t *createPlugin(const clap_host_t *host)
{
    MyPlugin *plugin = new MyPlugin();
    plugin->host = host;
    plugin->plugin = pluginClass;
    plugin->plugin.plugin_data = plugin;
    return &plugin->plugin;
}
