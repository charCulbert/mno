#include "Plugin.h"

extern "C"
{

CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION,
    mno_plugin::entryInit,
    mno_plugin::entryDeinit,
    mno_plugin::entryGetFactory
};

}
