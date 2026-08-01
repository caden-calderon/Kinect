# Subject framing, range, and camera controls

The performer should be able to move toward or away from the Kinect without
retuning a far plane. `geometry.auto_subject_range` is therefore on by default.

## What automatic range does

On each consumed source depth frame, a CPU-only tracker finds the nearest
substantial depth layer inside a centered focus region. Tiny nearer islands are
ignored, and the estimate is smoothed and movement-capped. The observed GPU
pass receives an effective range around that estimate:

- near = subject depth − `subject_near_margin_mm` (default 1200 mm);
- far = subject depth + `subject_far_margin_mm` (default 900 mm);
- both are clamped to the Kinect depth domain.

The point-depth fog focus follows the same estimate. Moving back therefore
does not make the performer disappear into a fixed fog range. Camera follow
uses the estimated subject depth rather than the mean depth of everything in
the room; admitting a distant wall can no longer move the orbit pivot behind
the performer.

`clip_near_mm`, `clip_far_mm`, and `points.focus_depth_mm` remain as manual
fallbacks. They are disabled in the UI while automatic range has a valid
estimate and become active when automatic range is off or has no lock.

This is deliberately a framing heuristic, not semantic person segmentation.
It assumes the intended subject occupies the central part of the frame. It can
exclude a wall at a different depth, but a chair, hand-held object, or another
person at the same depth can remain. Use a background plate for precise
subtraction in a fixed room. The planned tracked-body mask/proxy is the later
answer for a changing scene.

## Raw Kinect camera view

Press `C` or click **show Kinect camera** to switch the main viewport to the
paired 1920×1080 color frame. This view bypasses geometry and post effects. It
works identically for live input and replay, and screenshots capture whichever
view is currently shown. The optional mirror checkbox affects preview only;
recorded/source pixels are never changed.

For automated replay checks, start directly in this view with:

```bash
prime-run build/src/kstudio --take takes/example.mcap --camera-preview
```

## Camera navigation

- Left drag: orbit around the current pivot.
- Right or middle drag: pan the pivot in the camera plane. A manual pan sets
  `camera.follow` to zero so follow cannot immediately undo the move.
- Wheel: proportional dolly.
- `F` or **reset camera**: restore the default camera.

Pitch is limited to 88 degrees, preventing an accidental pole crossing from
turning the view upside-down. The camera is still represented as an orbit
camera, but its pivot is freely movable; this gives predictable art-tool
orbit/pan/dolly behavior without introducing a second camera model.

## Close-range depth expectations

`geometry.speckle_min_neighbors` defaults to one coherent neighbor. It removes
isolated flying depth pixels while retaining one-pixel-wide connected details.
No missing sample is filled or presented as measured geometry.

This cleans the observed shell, but it cannot increase the Kinect's 512×424
depth resolution, see an occluded backside, or reconstruct fingers the sensor
did not measure. Limb continuity and unseen volume belong to the distinct
tracked/inferred layers: tracked joints first, then the E6 capsule proxy, with
hand-specific or fuller body inference only where the real take demonstrates
that capsules are insufficient.
