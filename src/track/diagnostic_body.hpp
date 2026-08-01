#pragma once

#include "track/body_frame.hpp"

namespace kstudio::diagnostics {

/// Stable mixed-provenance body for renderer diagnostics. This is never a
/// provider result and must not be persisted as live tracking data.
TrackedBodyFrame makeTrackedBody();

}  // namespace kstudio::diagnostics
