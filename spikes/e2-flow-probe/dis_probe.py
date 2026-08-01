#!/usr/bin/env python3
"""E2 flow probe — cost + preset sweep for software optical flow at 512x424.

Full E2 quality metrics (FB-consistent coverage over the *body mask* during
fast arm swings) require a clip with a person in it; on a static clip this
script still measures the machine-property numbers (ms/frame per preset)
and the FB-consistency machinery, and reports scene motion so nobody
mistakes a static-scene run for a quality verdict.

Usage: dis_probe.py <clip_dir> [n_frames]
  clip_dir: E1 probe -clip output (clip_color_*.jpg at 1080p)
"""
import json
import sys
import time
from pathlib import Path

import cv2
import numpy as np

SIZE = (512, 424)  # depth raster proportions (cost target from discovery 02)


def load_frames(clip_dir: Path, limit: int) -> list[np.ndarray]:
    files = sorted(clip_dir.glob("clip_color_*.jpg"))[:limit]
    if len(files) < 10:
        sys.exit(f"need >=10 frames, found {len(files)} in {clip_dir}")
    frames = []
    for f in files:
        img = cv2.imread(str(f), cv2.IMREAD_GRAYSCALE)
        frames.append(cv2.resize(img, SIZE, interpolation=cv2.INTER_AREA))
    return frames


def fb_consistency(fwd: np.ndarray, bwd: np.ndarray, thresh_px: float = 1.0) -> float:
    """Fraction of pixels whose forward flow, warped back, cancels within thresh."""
    h, w = fwd.shape[:2]
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    end_x, end_y = gx + fwd[..., 0], gy + fwd[..., 1]
    bwd_at_end = cv2.remap(bwd, end_x, end_y, cv2.INTER_LINEAR,
                           borderMode=cv2.BORDER_REPLICATE)
    err = np.linalg.norm(fwd + bwd_at_end, axis=2)
    return float((err < thresh_px).mean())


def run_source(name: str, make, frames: list[np.ndarray]) -> dict:
    flow_obj = make()
    times, fb_cov, mags = [], [], []
    for i in range(1, len(frames)):
        a, b = frames[i - 1], frames[i]
        t0 = time.perf_counter()
        fwd = flow_obj.calc(a, b, None)
        times.append((time.perf_counter() - t0) * 1000)
        bwd = flow_obj.calc(b, a, None)
        fb_cov.append(fb_consistency(fwd, bwd))
        mags.append(float(np.median(np.linalg.norm(fwd, axis=2))))
    times.sort()
    return {
        "source": name,
        "ms_per_frame_p50": round(times[len(times) // 2], 2),
        "ms_per_frame_p95": round(times[int(len(times) * 0.95)], 2),
        "fb_consistent_coverage_mean": round(float(np.mean(fb_cov)), 4),
        "median_flow_px": round(float(np.median(mags)), 3),
    }


def main() -> None:
    clip_dir = Path(sys.argv[1])
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    frames = load_frames(clip_dir, limit)

    sources = [
        ("DIS_ULTRAFAST", lambda: cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_ULTRAFAST)),
        ("DIS_FAST", lambda: cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_FAST)),
        ("DIS_MEDIUM", lambda: cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)),
        ("Farneback_CPU", lambda: cv2.FarnebackOpticalFlow_create()),
    ]
    results = [run_source(name, make, frames) for name, make in sources]

    cuda_devices = 0
    try:
        cuda_devices = cv2.cuda.getCudaEnabledDeviceCount()
    except Exception:
        pass

    scene_motion = max(r["median_flow_px"] for r in results)
    print(json.dumps({
        "clip": str(clip_dir),
        "frames": len(frames),
        "resolution": list(SIZE),
        "opencv": cv2.__version__,
        "opencv_cuda_devices": cuda_devices,
        "static_scene_warning": scene_motion < 0.5,
        "results": results,
    }, indent=2))


if __name__ == "__main__":
    main()
