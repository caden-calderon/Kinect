#!/usr/bin/env python3
"""E5 stability pass on the body clip: joint jitter on the held pose,
dropout during fast swings, reacquisition after full out-of-frame,
and portrait-truncation behavior."""
import json
from pathlib import Path

import cv2
import numpy as np
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

CLIP = Path("/home/caden/projects/kinect/spikes/e1-capture-probe/out-clip-body")
MODEL = Path("/home/caden/projects/kinect/spikes/e5-tracker-probe/models/pose_landmarker_lite.task")

# landmark ids: nose 0, shoulders 11/12, elbows 13/14, wrists 15/16, hips 23/24
UPPER = {"nose": 0, "l_shoulder": 11, "r_shoulder": 12, "l_elbow": 13,
         "r_elbow": 14, "l_wrist": 15, "r_wrist": 16}
HIPS = [23, 24]

files = sorted(CLIP.glob("clip_color_*.jpg"))
print(f"{len(files)} frames")

options = vision.PoseLandmarkerOptions(
    base_options=mp_python.BaseOptions(model_asset_path=str(MODEL)),
    running_mode=vision.RunningMode.VIDEO)
lm = vision.PoseLandmarker.create_from_options(options)

W, H = 1920, 1080
rows = []            # (detected, {name: (x_px, y_px, visibility)}, hip_vis, energy)
prev_gray = None
for i, f in enumerate(files):
    bgr = cv2.imread(str(f))
    small = cv2.resize(cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY), (512, 424))
    energy = float(np.mean(cv2.absdiff(small, prev_gray))) if prev_gray is not None else 0.0
    prev_gray = small
    image = mp.Image(image_format=mp.ImageFormat.SRGB,
                     data=cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB))
    res = lm.detect_for_video(image, i * 33)
    if res.pose_landmarks:
        pts = res.pose_landmarks[0]
        joints = {n: (pts[j].x * W, pts[j].y * H, pts[j].visibility)
                  for n, j in UPPER.items()}
        hip_vis = float(np.mean([pts[j].visibility for j in HIPS]))
        rows.append((True, joints, hip_vis, energy))
    else:
        rows.append((False, None, 0.0, energy))

det = np.array([r[0] for r in rows])
energy = np.array([r[3] for r in rows])
n = len(rows)
print(f"detection rate overall: {det.mean() * 100:.1f}%")

# --- held-pose jitter: most static 150-frame (5 s) window with detections ---
best_start, best_e = None, 1e9
for s in range(0, n - 150):
    if det[s:s + 150].all():
        e = energy[s:s + 150].mean()
        if e < best_e:
            best_e, best_start = e, s
if best_start is not None:
    win = rows[best_start:best_start + 150]
    sig = {}
    for name in UPPER:
        xs = np.array([r[1][name][0] for r in win])
        ys = np.array([r[1][name][1] for r in win])
        # detrend with a 15-frame moving average so slow sway doesn't count as jitter
        k = np.ones(15) / 15
        xs_j = xs - np.convolve(xs, k, mode="same")
        ys_j = ys - np.convolve(ys, k, mode="same")
        sig[name] = round(float(np.hypot(xs_j[8:-8].std(), ys_j[8:-8].std())), 2)
    print(f"held-pose window frames {best_start}..{best_start + 150} "
          f"(energy {best_e:.2f}), jitter sigma px @1080p: {json.dumps(sig)}")

# --- dropout during swings (top-quartile motion frames) ---
swing = energy > np.percentile(energy, 75)
print(f"swing frames: {int(swing.sum())}, dropout during swings: "
      f"{(1 - det[swing].mean()) * 100:.2f}%")

# --- absence runs -> reacquisition ---
runs, start = [], None
for i, d in enumerate(det):
    if not d and start is None:
        start = i
    elif d and start is not None:
        runs.append((start, i - 1))
        start = None
if start is not None:
    runs.append((start, n - 1))
print("no-detection runs (frames):", [(a, b, b - a + 1) for a, b in runs])

# --- truncation: portrait segment = frames where person detected and hips invisible ---
hip_vis = np.array([r[2] for r in rows])
trunc = det & (hip_vis < 0.5)
print(f"frames detected with hips invisible (truncated body): {int(trunc.sum())} "
      f"({trunc.sum() / max(det.sum(), 1) * 100:.1f}% of detected)")
if trunc.sum() > 30:
    idx = np.where(trunc)[0]
    seg = rows[idx[len(idx) // 2]]
    vis = {k: round(v[2], 2) for k, v in seg[1].items()}
    print("mid-truncation upper-body visibilities:", json.dumps(vis))
