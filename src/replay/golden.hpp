#pragma once

#include <filesystem>

namespace kstudio {

/// Writes the synthetic golden take: `frames` depth+ir frames at 30 Hz and
/// color at 15 Hz (procedural, fully deterministic — same bytes every run),
/// through the real TakeRecorder so the fixture exercises the production
/// take format. The primary regression fixture (roadmap phase 2); real
/// takes of people are never committed.
bool writeGoldenTake(const std::filesystem::path& path, int frames = 60);

}  // namespace kstudio
