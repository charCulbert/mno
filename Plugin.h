#pragma once

#include <clap/clap.h>

namespace mno_plugin
{

const clap_plugin_descriptor_t& descriptor() noexcept;
bool entryInit(const char* path);
void entryDeinit();
const void* entryGetFactory(const char* factoryId);

} // namespace mno_plugin
