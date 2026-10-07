#include "plugin.h"
#include "Presets.h"
#include "MNOProcessor.h"
#include "core/messages.h"

#include <clap/ext/draft/webview.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "Failed at %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)

namespace
{
std::vector<core::Value> pageMessages;

const clap_host_webview_t hostWebview { [](const clap_host_t*, const void* data, uint32_t size) {
    auto message = core::decode(data, size);
    CHECK(message);
    pageMessages.push_back(*message);
    return true;
} };

const clap_host_t host { CLAP_VERSION, nullptr, "MNO tests", "", "", "1",
    [](const clap_host_t*, const char* id) -> const void* { return std::strcmp(id, CLAP_EXT_WEBVIEW) ? nullptr : &hostWebview; },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {} };

struct Input
{
    std::vector<const clap_event_header_t*> events;
    clap_input_events_t list { this,
        [](const clap_input_events_t* l) { return static_cast<uint32_t>(static_cast<const Input*>(l->ctx)->events.size()); },
        [](const clap_input_events_t* l, uint32_t i) { return static_cast<const Input*>(l->ctx)->events[i]; } };
};

const clap_output_events_t discard { nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; } };

clap_event_note_t noteOn(int16_t key)
{
    return { { sizeof(clap_event_note_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 }, -1, 0, 0, key, 1.0 };
}

std::optional<core::Value> lastMessage(std::string_view type)
{
    for (auto m = pageMessages.rbegin(); m != pageMessages.rend(); ++m)
        if ((*m)["type"].text() == type)
            return *m;
    return std::nullopt;
}

const core::Value::Array& array(const core::Value& value)
{
    const auto* a = std::get_if<core::Value::Array>(&value.data);
    CHECK(a);
    return *a;
}

bool sendToPlugin(const clap_plugin_t* plugin, const clap_plugin_webview_t* webview, const core::Value& message)
{
    const auto bytes = core::encode(message);
    return webview->receive(plugin, bytes.data(), static_cast<uint32_t>(bytes.size()));
}

void parameterPublication()
{
    const mno::ParameterDefinition definition { 0, "Test", "", 0, -100000.0, 100000.0, 0.0 };
    mno::ParameterRenderState parameter;
    parameter.bind(definition);
    CHECK(!parameter.consumePublishedBaseOnAudioThread());
    parameter.publishBaseFromMainThread(1.0);
    parameter.publishBaseFromMainThread(2.0);
    CHECK(parameter.consumePublishedBaseOnAudioThread());
    CHECK(parameter.nextGlobalValue() == 2.0);
    parameter.applyAutomatedBase(3.0);
    CHECK(!parameter.consumePublishedBaseOnAudioThread());
    CHECK(parameter.baseValueForMainThread() == 3.0);
    parameter.applyHostGlobalModulation(1.0);
    CHECK(parameter.nextGlobalValue() == 4.0);
    parameter.publishBaseFromMainThread(6.0);
    parameter.bind(definition);
    CHECK(!parameter.consumePublishedBaseOnAudioThread());
    CHECK(parameter.nextGlobalValue() == 0.0);
    CHECK(parameter.baseValueForMainThread() == 0.0);

    constexpr uint32_t iterations = 100000;
    std::atomic<bool> started { false }, mainDone { false };
    std::thread audio([&] {
        double previous = 0.0;
        started.store(true);
        while (previous < iterations)
        {
            if (parameter.consumePublishedBaseOnAudioThread())
            {
                const auto value = parameter.nextGlobalValue();
                // Page edits may coalesce, but must not replay over newer automation.
                CHECK(value > previous && value <= iterations && std::floor(value) == value);
                previous = value;
                parameter.applyAutomatedBase(-value);
            }
            CHECK(parameter.nextGlobalValue() == -previous);
        }
        while (!mainDone.load()) std::this_thread::yield();
        parameter.applyAutomatedBase(-previous);
    });
    while (!started.load()) std::this_thread::yield();
    for (uint32_t i = 1; i <= iterations; ++i)
    {
        parameter.publishBaseFromMainThread(i);
        const auto shown = parameter.baseValueForMainThread();
        CHECK(std::isfinite(shown) && std::abs(shown) <= iterations);
    }
    mainDone.store(true);
    audio.join();
    CHECK(parameter.baseValueForMainThread() == -double(iterations));
}

void presetDiscovery()
{
    struct Found { bool location = false; bool pluginIds = true; std::vector<std::string> names, keys; } found;
    clap_preset_discovery_indexer_t indexer {};
    indexer.indexer_data = &found;
    indexer.declare_location = [](const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
        return static_cast<Found*>(i->indexer_data)->location = l && l->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN
            && !l->location && (l->flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    };
    const auto* factory = static_cast<const clap_preset_discovery_factory_t*>(
        clap_entry.get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    CHECK(factory && factory->count(factory) == 1);
    const auto* provider = factory->create(factory, &indexer, factory->get_descriptor(factory, 0)->id);
    CHECK(provider && provider->init(provider) && found.location);

    clap_preset_discovery_metadata_receiver_t receiver {};
    receiver.receiver_data = &found;
    receiver.on_error = [](const clap_preset_discovery_metadata_receiver_t*, int32_t, const char*) {};
    receiver.begin_preset = [](const clap_preset_discovery_metadata_receiver_t* r, const char* name, const char* key) {
        static_cast<Found*>(r->receiver_data)->names.emplace_back(name);
        static_cast<Found*>(r->receiver_data)->keys.emplace_back(key);
        return true;
    };
    receiver.add_plugin_id = [](const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
        auto& f = *static_cast<Found*>(r->receiver_data);
        f.pluginIds = f.pluginIds && !std::strcmp(id->abi, "clap") && !std::strcmp(id->id, mno::pluginId);
    };
    receiver.set_flags = [](const clap_preset_discovery_metadata_receiver_t*, uint32_t) {};
    receiver.add_creator = [](const clap_preset_discovery_metadata_receiver_t*, const char*) {};
    receiver.add_feature = [](const clap_preset_discovery_metadata_receiver_t*, const char*) {};
    CHECK(provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &receiver));
    const std::vector<std::string> names { "Init", "Warm Stack", "Rubber Bass", "Glass Pluck", "Slow Bloom", "Sync Lead" };
    const std::vector<std::string> keys { "init", "warm-stack", "rubber-bass", "glass-pluck", "slow-bloom", "sync-lead" };
    CHECK(found.names == names && found.keys == keys && found.pluginIds);
    provider->destroy(provider);
}

// The plugin end to end: parameters, presets, state, sound, and the messages
// the page sees.
void plugin()
{
    const auto* factory = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    CHECK(factory && factory->get_plugin_count(factory) == 1);
    const auto* p = factory->create_plugin(factory, &host, mno::pluginId);
    CHECK(p && p->init(p));
    CHECK(p->activate(p, 48000, 1, 4096) && p->start_processing(p));

    const auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    const auto* presets = static_cast<const clap_plugin_preset_load_t*>(p->get_extension(p, CLAP_EXT_PRESET_LOAD));
    const auto* state = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    const auto* webview = static_cast<const clap_plugin_webview_t*>(p->get_extension(p, CLAP_EXT_WEBVIEW));
    const auto* gui = static_cast<const clap_plugin_gui_t*>(p->get_extension(p, CLAP_EXT_GUI));
    CHECK(params && presets && state && webview && gui);

    CHECK(params->count(p) == mno::parameterCount);
    const auto& definitions = mno::MNOProcessor::parameterDefinitions();
    for (uint32_t i = 0; i < mno::parameterCount; ++i)
    {
        clap_param_info_t info {};
        CHECK(params->get_info(p, i, &info) && info.id == i && !std::strcmp(info.name, definitions[i].name));
        CHECK(info.min_value == definitions[i].minimum && info.max_value == definitions[i].maximum
              && info.default_value == definitions[i].defaultValue);
        char text[64];
        double back = 0;
        CHECK(params->value_to_text(p, i, info.default_value, text, sizeof(text)));
        CHECK(params->text_to_value(p, i, text, &back) && std::abs(back - info.default_value) <= std::max(1e-3, std::abs(info.default_value) * 1e-3));
    }

    const auto value = [&](mno::MNOParameter parameter) {
        double v = 0;
        CHECK(params->get_value(p, static_cast<clap_id>(parameter), &v));
        return v;
    };
    for (const auto& preset : mno::factoryPresets)
    {
        CHECK(presets->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, preset.key));
        CHECK(value(mno::MNOParameter::output) == 1.0);
    }
    CHECK(!presets->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "missing"));
    CHECK(presets->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "rubber-bass"));
    CHECK(value(mno::MNOParameter::osc2Tune) == -1200.0);

    // State round trip: save Rubber Bass, load Init, restore.
    std::vector<unsigned char> saved;
    clap_ostream_t out { &saved, [](const clap_ostream_t* s, const void* data, uint64_t size) -> int64_t {
        auto* bytes = static_cast<const unsigned char*>(data);
        static_cast<std::vector<unsigned char>*>(s->ctx)->insert(static_cast<std::vector<unsigned char>*>(s->ctx)->end(), bytes, bytes + size);
        return static_cast<int64_t>(size);
    } };
    CHECK(state->save(p, &out));
    CHECK(presets->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));
    CHECK(value(mno::MNOParameter::osc2Tune) == mno::mnoParameterEndpoints[static_cast<size_t>(mno::MNOParameter::osc2Tune)].defaultValue);
    struct Reader { const std::vector<unsigned char>* bytes; size_t at = 0; } reader { &saved };
    clap_istream_t in { &reader, [](const clap_istream_t* s, void* data, uint64_t size) -> int64_t {
        auto& r = *static_cast<Reader*>(s->ctx);
        const auto n = std::min<uint64_t>(size, r.bytes->size() - r.at);
        std::memcpy(data, r.bytes->data() + r.at, n);
        r.at += n;
        return static_cast<int64_t>(n);
    } };
    CHECK(state->load(p, &in));
    CHECK(value(mno::MNOParameter::osc2Tune) == -1200.0);
    CHECK(presets->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "init"));

    // The page opens and asks for its metadata and values.
    CHECK(gui->create(p, CLAP_WINDOW_API_WEBVIEW, false) && gui->show(p));
    pageMessages.clear();
    CHECK(sendToPlugin(p, webview, core::Value::Map { { "type", "ready" } }));
    const auto metadata = lastMessage("metadata");
    CHECK(metadata && array((*metadata)["parameters"]).size() == mno::parameterCount);
    CHECK(array((*metadata)["parameters"])[static_cast<size_t>(mno::MNOParameter::osc2Waveform)]["options"].data.index() == 4);
    CHECK(lastMessage("values") && array((*lastMessage("values"))["values"]).size() == mno::parameterCount);

    // A note and a host cutoff change: sound, telemetry, and the new value on the page.
    std::array<float, 4096> left {}, right {};
    std::array<float*, 2> channels { left.data(), right.data() };
    clap_audio_buffer_t buffer { channels.data(), nullptr, 2, 0, 0 };
    const auto note = noteOn(69);
    const clap_event_param_value_t cutoff { { sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 },
        static_cast<clap_id>(mno::MNOParameter::cutoff), nullptr, -1, -1, -1, -1, 1234.0 };
    Input events;
    events.events = { &cutoff.header, &note.header };
    clap_process_t block {};
    block.frames_count = static_cast<uint32_t>(left.size());
    block.audio_outputs = &buffer;
    block.audio_outputs_count = 1;
    block.in_events = &events.list;
    block.out_events = &discard;
    CHECK(p->process(p, &block) == CLAP_PROCESS_CONTINUE);
    float peak = 0;
    for (const auto s : left)
    {
        CHECK(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    CHECK(peak > 0.001f && peak < 1.0f);

    pageMessages.clear();
    p->on_main_thread(p);
    CHECK(lastMessage("values") && array((*lastMessage("values"))["values"])[static_cast<size_t>(mno::MNOParameter::cutoff)].number() == 1234.0);
    CHECK(sendToPlugin(p, webview, core::Value::Map { { "type", "visual" } }));
    const auto visual = lastMessage("visual");
    CHECK(visual && array((*visual)["modulation"]).size() == 17 && !array((*visual)["scope"]).empty());
    CHECK(array((*visual)["modulation"])[3].number() == 1.0); // adsrActive

    // A page edit reaches the host as a gesture and a value, in order.
    const auto cutoffId = static_cast<double>(mno::MNOParameter::cutoff);
    CHECK(sendToPlugin(p, webview, core::Value::Map { { "type", "begin" }, { "id", cutoffId } }));
    CHECK(sendToPlugin(p, webview, core::Value::Map { { "type", "value" }, { "id", cutoffId }, { "value", 900.0 } }));
    CHECK(sendToPlugin(p, webview, core::Value::Map { { "type", "end" }, { "id", cutoffId } }));
    CHECK(!sendToPlugin(p, webview, core::Value::Map { { "type", "value" }, { "id", 999.0 }, { "value", 1.0 } }));
    std::vector<uint16_t> sent;
    clap_output_events_t record { &sent, [](const clap_output_events_t* o, const clap_event_header_t* e) {
        static_cast<std::vector<uint16_t>*>(o->ctx)->push_back(e->type);
        return true;
    } };
    Input none;
    params->flush(p, &none.list, &record);
    CHECK((sent == std::vector<uint16_t> { CLAP_EVENT_PARAM_GESTURE_BEGIN, CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_PARAM_GESTURE_END }));
    CHECK(value(mno::MNOParameter::cutoff) == 900.0);

    gui->destroy(p);
    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
}

void outputHeadroom()
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (const auto key : { 36, 51, 69, 84 })
        {
            mno::MNOProcessor processor;
            CHECK(processor.prepare(sampleRate, 1, 256));
            const auto note = noteOn(static_cast<int16_t>(key));
            Input events, none;
            events.events = { &note.header };
            std::array<float, 256> samples {};
            float* channels[] { samples.data() };
            clap_audio_buffer_t output { channels, nullptr, 1, 0, 0 };
            clap_process_t block {};
            block.frames_count = 256;
            block.audio_outputs = &output;
            block.audio_outputs_count = 1;
            block.in_events = &events.list;
            float peak = 0, steadyPeak = 0;
            for (int frame = 0; frame < int(sampleRate * 0.25); frame += 256)
            {
                CHECK(processor.process(block) != CLAP_PROCESS_ERROR);
                block.in_events = &none.list;
                for (const auto s : samples)
                {
                    CHECK(std::isfinite(s));
                    peak = std::max(peak, std::abs(s));
                    if (frame >= int(sampleRate * 0.125))
                        steadyPeak = std::max(steadyPeak, std::abs(s));
                }
            }
            CHECK(peak > 0.01f && peak < 0.5f);

            std::array<float, mno::MNOScopeRing::capacity> scope {};
            const auto count = processor.copyScope(scope.data(), static_cast<int>(scope.size()));
            CHECK(count > 0);
            float scopePeak = 0;
            for (int i = 0; i < count; ++i)
            {
                CHECK(std::isfinite(scope[static_cast<size_t>(i)]));
                scopePeak = std::max(scopePeak, std::abs(scope[static_cast<size_t>(i)]));
            }
            CHECK(scopePeak > steadyPeak * 3.5f && scopePeak < steadyPeak * 4.5f);
        }
}

void scopeCyclePhase()
{
    mno::MNOOscillator oscillator;
    oscillator.prepare(48000.0f);
    oscillator.reset();
    oscillator.setWaveform(mno::MNOWaveform::saw);
    int wrap = 0, previousBoundary = -1, boundaries = 0;
    for (int frame = 0; frame < 20000 && boundaries < 5; ++frame)
    {
        if (oscillator.next(997.0f).trigger < 0.5f) continue;
        ++wrap;
        if (oscillator.scopeCyclePhase() != 0) continue;
        if (previousBoundary >= 0) CHECK(wrap - previousBoundary == 2);
        previousBoundary = wrap;
        ++boundaries;
    }
    CHECK(boundaries == 5);
}

void independentPWM()
{
    mno::MNOProcessor processor;
    processor.setScopeRing(nullptr);
    CHECK(processor.prepare(48000.0, 1, 1200));
    const auto set = [&](mno::MNOParameter parameter, double value) {
        processor.parameter(static_cast<size_t>(parameter)).applyAutomatedBase(value);
    };
    set(mno::MNOParameter::osc1Waveform, 2.0);
    set(mno::MNOParameter::osc2Waveform, 2.0);
    set(mno::MNOParameter::osc1Shape, 0.5);
    set(mno::MNOParameter::osc2Shape, 0.5);
    set(mno::MNOParameter::osc1PWMRate, 10.0);
    set(mno::MNOParameter::osc2PWMRate, 5.0);
    set(mno::MNOParameter::osc1PWMDepth, 25.0);
    set(mno::MNOParameter::osc2PWMDepth, 25.0);
    set(mno::MNOParameter::lfoRate, 40.0);
    Input none;
    std::array<float, 1200> samples {};
    float* channels[] { samples.data() };
    clap_audio_buffer_t output { channels, nullptr, 1, 0, 0 };
    clap_process_t block {};
    block.frames_count = static_cast<uint32_t>(samples.size());
    block.audio_outputs = &output;
    block.audio_outputs_count = 1;
    block.in_events = &none.list;
    CHECK(processor.process(block) != CLAP_PROCESS_ERROR);
    const auto modulation = processor.getModulationState();
    CHECK(std::min(modulation.lfoPhase, 1.0f - modulation.lfoPhase) < 0.001f);
    CHECK(std::abs(modulation.osc1Shape - modulation.osc2Shape - 0.07029f) < 0.002f);
}
}

int main()
{
    CHECK(clap_entry.init("."));
    parameterPublication();
    presetDiscovery();
    plugin();
    outputHeadroom();
    scopeCyclePhase();
    independentPWM();
    clap_entry.deinit();
    std::puts("PASS: parameter publication, preset discovery, plugin end to end, headroom, scope phase, PWM");
}
