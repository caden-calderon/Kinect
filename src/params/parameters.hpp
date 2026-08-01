#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace kstudio {

/// Typed, ranged, serializable parameters (discovery 06 contract 5).
///
/// The engine registers every creative control here once, with group,
/// range, and default; the UI renders panels generically from the
/// registry; presets are JSON snapshots carrying a schema version. A
/// preset + a take + a seed is the deterministic-replay input (E7), so
/// serialization must round-trip exactly (floats via shortest-round-trip
/// formatting).
///
/// Threading: owned and mutated by the engine thread (ImGui lives there
/// too — discovery 06 §4); other threads must not touch it.
class Parameters {
 public:
  struct Float {
    float value, def, min, max;
  };
  struct Int {
    int value, def, min, max;
  };
  struct Bool {
    bool value, def;
  };
  struct Enum {
    int value, def;
    std::vector<std::string> options;
  };
  using Entry = std::variant<Float, Int, Bool, Enum>;

  /// Registration (engine setup). Returns a stable pointer into the
  /// registry, valid for the Parameters lifetime — hot-path reads are a
  /// plain dereference.
  float* addFloat(const std::string& group, const std::string& name, float def, float min,
                  float max);
  int* addInt(const std::string& group, const std::string& name, int def, int min, int max);
  bool* addBool(const std::string& group, const std::string& name, bool def);
  int* addEnum(const std::string& group, const std::string& name, int def,
               std::vector<std::string> options);

  void resetAllToDefaults();

  /// Preset I/O. JSON object: {"schema": 1, "params": {"group.name": value}}.
  /// Unknown keys are reported (not silently dropped); missing params keep
  /// their current value and are reported.
  std::string savePreset() const;
  struct LoadReport {
    bool ok = false;
    std::vector<std::string> unknown_keys;
    std::vector<std::string> missing_params;
    std::string error;
  };
  LoadReport loadPreset(const std::string& json);

  /// UI iteration: stable registration order within groups.
  struct Item {
    std::string group, name;
    Entry* entry;
  };
  const std::vector<Item>& items() const { return items_; }

  static constexpr int kSchemaVersion = 1;

 private:
  Entry* add(const std::string& group, const std::string& name, Entry entry);
  std::vector<Item> items_;
  std::map<std::string, size_t> by_key_;         // "group.name" -> index
  std::vector<std::unique_ptr<Entry>> storage_;  // stable addresses
};

}  // namespace kstudio
