#include "params/parameters.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace kstudio {

using nlohmann::json;

Parameters::Entry* Parameters::add(const std::string& group, const std::string& name, Entry entry) {
  storage_.push_back(std::make_unique<Entry>(std::move(entry)));
  Entry* e = storage_.back().get();
  items_.push_back({group, name, e});
  by_key_[group + "." + name] = items_.size() - 1;
  return e;
}

float* Parameters::addFloat(const std::string& group, const std::string& name, float def, float min,
                            float max) {
  return &std::get<Float>(*add(group, name, Float{def, def, min, max})).value;
}

int* Parameters::addInt(const std::string& group, const std::string& name, int def, int min,
                        int max) {
  return &std::get<Int>(*add(group, name, Int{def, def, min, max})).value;
}

bool* Parameters::addBool(const std::string& group, const std::string& name, bool def) {
  return &std::get<Bool>(*add(group, name, Bool{def, def})).value;
}

int* Parameters::addEnum(const std::string& group, const std::string& name, int def,
                         std::vector<std::string> options) {
  return &std::get<Enum>(*add(group, name, Enum{def, def, std::move(options)})).value;
}

void Parameters::resetAllToDefaults() {
  for (auto& item : items_) {
    std::visit(
        [](auto& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (!std::is_same_v<T, std::monostate>) e.value = e.def;
        },
        *item.entry);
  }
}

std::string Parameters::savePreset() const {
  json params = json::object();
  for (const auto& item : items_) {
    const std::string key = item.group + "." + item.name;
    std::visit(
        [&](const auto& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<T, Float>)
            params[key] = e.value;
          else if constexpr (std::is_same_v<T, Int>)
            params[key] = e.value;
          else if constexpr (std::is_same_v<T, Bool>)
            params[key] = e.value;
          else if constexpr (std::is_same_v<T, Enum>)
            params[key] = e.value;
        },
        *item.entry);
  }
  return json{{"schema", kSchemaVersion}, {"params", params}}.dump(2);
}

Parameters::LoadReport Parameters::loadPreset(const std::string& text) {
  LoadReport report;
  json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.contains("params") || !root["params"].is_object()) {
    report.error = "not a preset (need {schema, params})";
    return report;
  }
  if (root.value("schema", -1) != kSchemaVersion) {
    report.error = "preset schema version mismatch";
    return report;
  }

  std::vector<bool> seen(items_.size(), false);
  for (auto& [key, value] : root["params"].items()) {
    auto it = by_key_.find(key);
    if (it == by_key_.end()) {
      report.unknown_keys.push_back(key);
      continue;
    }
    seen[it->second] = true;
    Entry* entry = items_[it->second].entry;
    std::visit(
        [&](auto& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<T, Float>) {
            if (value.is_number()) e.value = std::clamp(value.get<float>(), e.min, e.max);
          } else if constexpr (std::is_same_v<T, Int>) {
            if (value.is_number_integer()) e.value = std::clamp(value.get<int>(), e.min, e.max);
          } else if constexpr (std::is_same_v<T, Bool>) {
            if (value.is_boolean()) e.value = value.get<bool>();
          } else if constexpr (std::is_same_v<T, Enum>) {
            if (value.is_number_integer())
              e.value = std::clamp(value.get<int>(), 0, int(e.options.size()) - 1);
          }
        },
        *entry);
  }
  for (size_t i = 0; i < items_.size(); ++i)
    if (!seen[i]) report.missing_params.push_back(items_[i].group + "." + items_[i].name);
  report.ok = true;
  return report;
}

}  // namespace kstudio
