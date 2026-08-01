#!/usr/bin/env python3
"""E2 quality pass on the body clip: FB-consistent coverage over the MOVING
(body) region during the fast-swing window, plus flow-overlay renders for
gross-direction judgment. Operationalizes "body mask" as pixels with
|flow| > 1.5 px (static room contributes ~0 flow, so movers == the body)."""
import sys
from pathlib import Path

import cv2
import numpy as np

CLIP = Path("/home/caden/projects/kinect/spikes/e1-capture-probe/out-clip-body")
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
SIZE = (512, 424)

files = sorted(CLIP.glob("clip_color_*.jpg"))
frames = [cv2.resize(cv2.imread(str(f), cv2.IMREAD_GRAYSCALE), SIZE,
                     interpolation=cv2.INTER_AREA) for f in files]
print(f"{len(frames)} color frames")

# 1. motion energy per frame -> find the swing window
energy = [0.0]
for i in range(1, len(frames)):
    energy.append(float(np.mean(cv2.absdiff(frames[i], frames[i - 1]))))
energy = np.array(energy)
thresh = np.percentile(energy, 75)
print(f"motion energy p50={np.median(energy):.2f} p75={thresh:.2f} max={energy.max():.2f}")

def fb_consistency_masked(fwd, bwd, mask, thresh_px=1.0):
    h, w = fwd.shape[:2]
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    bwd_at_end = cv2.remap(bwd, gx + fwd[..., 0], gy + fwd[..., 1],
                           cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    err = np.linalg.norm(fwd + bwd_at_end, axis=2)
    if mask.sum() == 0:
        return None, 0
    return float((err[mask] < thresh_px).mean()), int(mask.sum())

for preset_name, preset in [("ULTRAFAST", cv2.DISOPTICAL_FLOW_PRESET_ULTRAFAST),
                            ("FAST", cv2.DISOPTICAL_FLOW_PRESET_FAST)]:
    dis = cv2.DISOpticalFlow_create(preset)
    covs, masked_px = [], []
    high_idx = [i for i in range(1, len(frames)) if energy[i] > thresh]
    for i in high_idx:
        fwd = dis.calc(frames[i - 1], frames[i], None)
        bwd = dis.calc(frames[i], frames[i - 1], None)
        mag = np.linalg.norm(fwd, axis=2)
        mask = mag > 1.5
        cov, n = fb_consistency_masked(fwd, bwd, mask)
        if cov is not None and n > 500:  # ignore frames with almost no mover
            covs.append(cov)
            masked_px.append(n)
    covs = np.array(covs)
    if len(covs):
        print(f"DIS_{preset_name}: swing frames analyzed {len(covs)}, "
              f"body-mask px median {int(np.median(masked_px))}, "
              f"FB-consistent coverage mean {covs.mean():.3f} "
              f"p10 {np.percentile(covs, 10):.3f} min {covs.min():.3f} "
              f"frames>=0.70: {(covs >= 0.70).mean() * 100:.1f}%")

# 2. overlay renders at the highest-motion frames (ULTRAFAST, the cheap candidate)
dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_ULTRAFAST)
peaks = np.argsort(energy)[-8:]
step = 16
for k, i in enumerate(sorted(peaks)):
    if i < 1:
        continue
    fwd = dis.calc(frames[i - 1], frames[i], None)
    vis = cv2.cvtColor(frames[i], cv2.COLOR_GRAY2BGR)
    for y in range(step // 2, SIZE[1], step):
        for x in range(step // 2, SIZE[0], step):
            fx, fy = fwd[y, x]
            if fx * fx + fy * fy < 2.25:
                continue
            cv2.arrowedLine(vis, (x, y), (int(x + fx * 3), int(y + fy * 3)),
                            (0, 255, 0), 1, tipLength=0.35)
    cv2.imwrite(str(OUT / f"flow_peak_{k}_frame{i}.png"), vis)
print(f"overlays written to {OUT}")
