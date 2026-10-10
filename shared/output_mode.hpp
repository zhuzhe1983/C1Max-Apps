#pragma once
#include <string>

namespace casting {
// Preferences are sampled when a new item/game starts. Changing a preference
// must not silently interrupt a running decoder or move its sound output.
enum class OutputMode { Local, Remote, Both };
OutputMode output_mode(const std::string &app);
bool set_output_mode(const std::string &app, OutputMode mode);
const char *output_mode_name(OutputMode mode);
const char *output_mode_label(OutputMode mode);
}
