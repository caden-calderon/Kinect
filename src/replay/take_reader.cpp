#include "replay/take_reader.hpp"

#include <mcap/reader.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

namespace kstudio {

namespace {

#pragma pack(push, 1)
struct WireHeader {
  uint32_t seq;
  uint32_t t_device;
  uint64_t t_host_ns;
  uint32_t gap_before;
  uint32_t payload_len;
};
#pragma pack(pop)
static_assert(sizeof(WireHeader) == 24);

/// Minimal field scanner for the calibration JSON we write ourselves
/// (recorder.cpp) — not a general JSON parser.
double jsonNumber(const std::string& json, const std::string& key, double fallback = 0) {
  const auto pos = json.find("\"" + key + "\":");
  if (pos == std::string::npos) return fallback;
  return std::strtod(json.c_str() + pos + key.size() + 3, nullptr);
}

std::string jsonString(const std::string& json, const std::string& key) {
  const auto pos = json.find("\"" + key + "\":\"");
  if (pos == std::string::npos) return {};
  const auto start = pos + key.size() + 4;
  const auto end = json.find('"', start);
  return json.substr(start, end - start);
}

uint64_t jsonHex(const std::string& json, const std::string& key, uint64_t fallback = 0) {
  const std::string value = jsonString(json, key);
  if (value.empty()) return fallback;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(value.c_str(), &end, 16);
  return end != value.c_str() && *end == '\0' ? uint64_t(parsed) : fallback;
}

}  // namespace

struct TakeReader::Impl {
  mcap::McapReader reader;
  std::shared_ptr<CalibrationBlob> calib;
  std::string calibration_json;

  std::vector<uint64_t> depth_times;  // logTime per depth frame, in play order

  // Sequential cursor. The view owns the iteration; rebuilt on seek.
  std::unique_ptr<mcap::LinearMessageView> view;
  std::optional<mcap::LinearMessageView::Iterator> it;
  size_t next_depth_index = 0;
  std::vector<uint8_t> color_scratch;

  bool dispatch(const mcap::MessageView& msg, const Callbacks& callbacks) {
    // Returns true when the message was a depth frame (the pump unit).
    const std::string& topic = msg.channel->topic;
    if (msg.message.dataSize < sizeof(WireHeader)) return false;
    WireHeader header;
    std::memcpy(&header, msg.message.data, sizeof(header));
    const auto* payload = reinterpret_cast<const uint8_t*>(msg.message.data) + sizeof(header);
    const size_t payload_size = msg.message.dataSize - sizeof(header);
    if (header.payload_len != payload_size) return false;

    if (topic == "/depth") {
      if (payload_size != size_t(kDepthWidth) * kDepthHeight * 2) return false;
      if (callbacks.on_depth)
        callbacks.on_depth(DepthMsg{header.seq, header.t_device, header.gap_before,
                                    header.t_host_ns, reinterpret_cast<const uint16_t*>(payload)});
      return true;
    }
    if (topic == "/ir") {
      if (payload_size != size_t(kDepthWidth) * kDepthHeight * 2) return false;
      if (callbacks.on_ir)
        callbacks.on_ir(IrMsg{header.seq, reinterpret_cast<const uint16_t*>(payload)});
      return false;
    }
    if (topic == "/color_jpeg") {
      if (callbacks.on_color)
        callbacks.on_color(ColorMsg{header.seq, header.t_device, header.gap_before,
                                    header.t_host_ns, payload, header.payload_len});
      return false;
    }
    return false;
  }

  void rebuildViewFrom(uint64_t start_time) {
    mcap::ReadMessageOptions options;
    options.startTime = start_time;
    options.endTime = mcap::MaxTime;
    view = std::make_unique<mcap::LinearMessageView>(
        reader.readMessages([](const mcap::Status&) {}, options));
    it = view->begin();
  }
};

TakeReader::TakeReader() : impl_(std::make_unique<Impl>()) {}
TakeReader::~TakeReader() { close(); }

bool TakeReader::open(const std::filesystem::path& path) {
  close();
  if (!impl_->reader.open(path.string()).ok()) return false;
  (void)impl_->reader.readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);

  // Index pass: depth logTimes + calibration (metadata-weight only).
  auto calib = std::make_shared<CalibrationBlob>();
  bool have_calib = false;
  {
    mcap::ReadMessageOptions options;
    auto view = impl_->reader.readMessages([](const mcap::Status&) {}, options);
    for (const auto& msg : view) {
      const std::string& topic = msg.channel->topic;
      if (topic == "/depth") {
        impl_->depth_times.push_back(msg.message.logTime);
      } else if (topic == "/calibration") {
        const std::string json(reinterpret_cast<const char*>(msg.message.data),
                               msg.message.dataSize);
        impl_->calibration_json = json;
        calib->device_serial = jsonString(json, "serial");
        calib->firmware = jsonString(json, "firmware");
        calib->content_hash = jsonHex(json, "content_hash");
        auto& ir = calib->ir;
        ir.fx = float(jsonNumber(json, "fx"));  // first "fx" is ir's (ir object precedes color)
        // ir block
        const auto ir_pos = json.find("\"ir\":");
        const auto color_pos = json.find("\"color\":");
        if (ir_pos == std::string::npos || color_pos == std::string::npos || color_pos <= ir_pos)
          continue;
        const std::string ir_json = json.substr(ir_pos, color_pos - ir_pos);
        const std::string color_json = json.substr(color_pos);
        ir.fx = float(jsonNumber(ir_json, "fx"));
        ir.fy = float(jsonNumber(ir_json, "fy"));
        ir.cx = float(jsonNumber(ir_json, "cx"));
        ir.cy = float(jsonNumber(ir_json, "cy"));
        ir.k1 = float(jsonNumber(ir_json, "k1"));
        ir.k2 = float(jsonNumber(ir_json, "k2"));
        ir.k3 = float(jsonNumber(ir_json, "k3"));
        ir.p1 = float(jsonNumber(ir_json, "p1"));
        ir.p2 = float(jsonNumber(ir_json, "p2"));
        auto& c = calib->color;
        c.fx = float(jsonNumber(color_json, "fx"));
        c.fy = float(jsonNumber(color_json, "fy"));
        c.cx = float(jsonNumber(color_json, "cx"));
        c.cy = float(jsonNumber(color_json, "cy"));
        c.shift_d = float(jsonNumber(color_json, "shift_d"));
        c.shift_m = float(jsonNumber(color_json, "shift_m"));
        c.mx_x3y0 = float(jsonNumber(color_json, "mx_x3y0"));
        c.mx_x0y3 = float(jsonNumber(color_json, "mx_x0y3"));
        c.mx_x2y1 = float(jsonNumber(color_json, "mx_x2y1"));
        c.mx_x1y2 = float(jsonNumber(color_json, "mx_x1y2"));
        c.mx_x2y0 = float(jsonNumber(color_json, "mx_x2y0"));
        c.mx_x0y2 = float(jsonNumber(color_json, "mx_x0y2"));
        c.mx_x1y1 = float(jsonNumber(color_json, "mx_x1y1"));
        c.mx_x1y0 = float(jsonNumber(color_json, "mx_x1y0"));
        c.mx_x0y1 = float(jsonNumber(color_json, "mx_x0y1"));
        c.mx_x0y0 = float(jsonNumber(color_json, "mx_x0y0"));
        c.my_x3y0 = float(jsonNumber(color_json, "my_x3y0"));
        c.my_x0y3 = float(jsonNumber(color_json, "my_x0y3"));
        c.my_x2y1 = float(jsonNumber(color_json, "my_x2y1"));
        c.my_x1y2 = float(jsonNumber(color_json, "my_x1y2"));
        c.my_x2y0 = float(jsonNumber(color_json, "my_x2y0"));
        c.my_x0y2 = float(jsonNumber(color_json, "my_x0y2"));
        c.my_x1y1 = float(jsonNumber(color_json, "my_x1y1"));
        c.my_x1y0 = float(jsonNumber(color_json, "my_x1y0"));
        c.my_x0y1 = float(jsonNumber(color_json, "my_x0y1"));
        c.my_x0y0 = float(jsonNumber(color_json, "my_x0y0"));
        have_calib = true;
      }
    }
  }
  if (!have_calib) {
    // A take without calibration is damaged but still replayable; the
    // consumer sees a zeroed blob and unprojection will be visibly wrong —
    // never silently substituted.
    std::fprintf(stderr, "[replay] warning: take has no /calibration message\n");
  }
  impl_->calib = std::move(calib);
  impl_->rebuildViewFrom(0);
  impl_->next_depth_index = 0;
  return true;
}

void TakeReader::close() {
  impl_->it.reset();
  impl_->view.reset();
  impl_->reader.close();
  impl_->calib.reset();
  impl_->calibration_json.clear();
  impl_->depth_times.clear();
  impl_->color_scratch.clear();
  impl_->next_depth_index = 0;
}

std::shared_ptr<const CalibrationBlob> TakeReader::calibration() const { return impl_->calib; }

const std::string& TakeReader::calibrationJson() const { return impl_->calibration_json; }

size_t TakeReader::depthFrameCount() const { return impl_->depth_times.size(); }

uint64_t TakeReader::depthLogTime(size_t index) const { return impl_->depth_times[index]; }

bool TakeReader::seekToDepthFrame(size_t index, const Callbacks& callbacks) {
  if (index >= impl_->depth_times.size()) return false;
  const uint64_t target = impl_->depth_times[index];

  // Deliver the latest color at-or-before the target so post-seek pairing
  // matches continuous-play pairing (bounded look-back: one color period at
  // the sensor's slowest documented color rate, plus margin).
  constexpr uint64_t kColorLookbackNs = 250'000'000ull;
  const uint64_t lookback_start = target > kColorLookbackNs ? target - kColorLookbackNs : 0;
  {
    mcap::ReadMessageOptions options;
    options.startTime = lookback_start;
    options.endTime = target;  // exclusive of the depth frame itself
    auto view = impl_->reader.readMessages([](const mcap::Status&) {}, options);
    std::optional<ColorMsg> latest;
    for (const auto& msg : view) {
      if (msg.channel->topic == "/color_jpeg") {
        // Copy header + remember payload via immediate dispatch of only the
        // final match: buffer the bytes (payloads die with the iterator).
        WireHeader header;
        if (msg.message.dataSize < sizeof(header)) continue;
        std::memcpy(&header, msg.message.data, sizeof(header));
        impl_->color_scratch.assign(
            reinterpret_cast<const uint8_t*>(msg.message.data) + sizeof(header),
            reinterpret_cast<const uint8_t*>(msg.message.data) + msg.message.dataSize);
        latest = ColorMsg{header.seq,
                          header.t_device,
                          header.gap_before,
                          header.t_host_ns,
                          impl_->color_scratch.data(),
                          header.payload_len};
      }
    }
    if (latest && callbacks.on_color) callbacks.on_color(*latest);
  }

  impl_->rebuildViewFrom(target);
  impl_->next_depth_index = index;
  return true;
}

std::optional<size_t> TakeReader::pump(const Callbacks& callbacks) {
  if (!impl_->it) return std::nullopt;
  auto& it = *impl_->it;
  while (it != impl_->view->end()) {
    const bool was_depth = impl_->dispatch(*it, callbacks);
    ++it;
    if (was_depth) {
      // Deliver an immediately-following IR record for the same frame (the
      // recorder writes them adjacently) before returning.
      if (it != impl_->view->end() && (*it).channel->topic == "/ir") {
        impl_->dispatch(*it, callbacks);
        ++it;
      }
      return impl_->next_depth_index++;
    }
  }
  return std::nullopt;
}

}  // namespace kstudio
