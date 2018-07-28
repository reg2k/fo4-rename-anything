#pragma once
#include "f4se_common/f4se_version.h"

//-----------------------
// Plugin Information
//-----------------------
#define PLUGIN_VERSION              15
#define PLUGIN_VERSION_STRING       "2.6.5"
#define PLUGIN_NAME_SHORT           "rename_anything"
#define PLUGIN_NAME_LONG            "Rename Anything"
#define SUPPORTED_RUNTIME_VERSION   CURRENT_RELEASE_RUNTIME
#define MINIMUM_RUNTIME_VERSION     SUPPORTED_RUNTIME_VERSION
#define COMPATIBLE(runtimeVersion)  (runtimeVersion == SUPPORTED_RUNTIME_VERSION)