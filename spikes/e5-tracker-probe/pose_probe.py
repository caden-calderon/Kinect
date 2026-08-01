#!/usr/bin/env python3
"""E5 tracker probe — MediaPipe pose on this machine.

AFK half: inference cost per frame (content-independent-ish) on recorded
clip frames. WITH-CADEN half: stability/dropout/reacquisition/truncation
metrics need a person in frame — run again on a clip with him in it.

Usage: .venv/bin/python pose_probe.py <clip_dir> [n]
"""
import hashlib
import json
import sys
import time
from pathlib import Path
from urllib import request

import cv2
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision


MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/pose_landmarker/"
    "pose_landmarker_lite/float16/1/pose_landmarker_lite.task"
)
MODEL_SHA256 = "59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_model(path: Path) -> None:
    if path.exists() and sha256(path) == MODEL_SHA256:
        return

    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_suffix(f"{path.suffix}.download")
    partial.unlink(missing_ok=True)
    print(f"downloading {MODEL_URL}", file=sys.stderr)
    request.urlretrieve(MODEL_URL, partial)
    actual_sha256 = sha256(partial)
    if actual_sha256 != MODEL_SHA256:
        partial.unlink(missing_ok=True)
        sys.exit(
            "downloaded pose model checksum mismatch: "
            f"expected {MODEL_SHA256}, got {actual_sha256}"
        )
    partial.replace(path)


def main() -> None:
    clip = Path(sys.argv[1])
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    files = sorted(clip.glob("clip_color_*.jpg"))[:limit]
    if not files:
        sys.exit(f"no clip_color_*.jpg in {clip}")

    # Model binaries stay out of Git; fetch the immutable v1 artifact on demand.
    model = Path("models/pose_landmarker_lite.task")
    ensure_model(model)

    options = vision.PoseLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=str(model)),
        running_mode=vision.RunningMode.VIDEO)
    landmarker = vision.PoseLandmarker.create_from_options(options)

    times_ms, detected = [], 0
    for i, f in enumerate(files):
        bgr = cv2.imread(str(f))
        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
        t0 = time.perf_counter()
        result = landmarker.detect_for_video(image, i * 33)
        times_ms.append((time.perf_counter() - t0) * 1000)
        if result.pose_landmarks:
            detected += 1

    times_ms.sort()
    n = len(times_ms)
    print(json.dumps({
        "frames": n,
        "model": "pose_landmarker_lite (CPU)",
        "inference_ms": {
            "p50": round(times_ms[n // 2], 1),
            "p95": round(times_ms[int(n * 0.95)], 1),
            "max": round(times_ms[-1], 1),
        },
        "implied_hz_at_p50": round(1000 / times_ms[n // 2], 1),
        "frames_with_pose": detected,
        "note": "empty-scene clip measures cost only; stability metrics need a person",
    }, indent=2))


if __name__ == "__main__":
    main()
