#pragma once

// Private to core: reads the editor: part of a device profile.

#include "arranger/core/SynthEditor.h"

#include <yaml-cpp/yaml.h>

namespace arranger::synth_edit
{
    SynthEditor parseEditor (const YAML::Node&);
}
