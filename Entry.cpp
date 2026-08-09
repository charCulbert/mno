#include "Plugin.h"

extern "C"
{

CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    example::mno_plugin::entryInit,
    example::mno_plugin::entryDeinit,
    example::mno_plugin::entryGetFactory
};

}
