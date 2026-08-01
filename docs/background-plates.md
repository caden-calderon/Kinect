# Background plates

A background plate is a scene-specific depth reference for removing static
observed geometry without imposing a far-distance cut. It is independent of a
take and a preset: loading either one never loads a plate implicitly.

## Capture and use

1. Set the `plate file` path in the studio panel (the default is
   `presets/background.plate`).
2. Click `capture background`, then leave the Kinect view while the status line
   advances from 0 to 60 frames.
3. The render thread computes the per-pixel median of valid depth samples,
   uploads it, enables `geometry.background_removal`, and saves it atomically.
4. Tune `geometry.background_epsilon_mm` (60 mm by default) if the static scene
   leaks through or removes geometry too aggressively.

Pixels valid in fewer than 20% of the 60 frames are stored as unknown and are
never subtracted. The comparison is strict:
`abs(current_depth - plate_depth) < epsilon_mm`.

Use the explicit `load plate`, `save plate`, and `clear plate` controls for
scene changes. `clear plate` only unloads the GPU plate; it does not delete the
file. A plate can also be loaded at startup:

```bash
prime-run build/src/kstudio --plate presets/background.plate
prime-run build/src/kstudio --take takes/example.mcap --plate presets/background.plate
```

## `.plate` format (version 1)

All integers are little-endian. The payload is row-major u16 depth in 0.1 mm
units, matching `DepthPlane`; zero means unknown.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 8 | magic `KSTPLT1\0` |
| 8 | 4 | format version (`1`) |
| 12 | 4 | header size (`40`) |
| 16 | 4 | width |
| 20 | 4 | height |
| 24 | 4 | units per millimeter (`10`) |
| 28 | 4 | reserved (`0`) |
| 32 | 8 | capture time, Unix seconds |
| 40 | `width * height * 2` | u16 depth payload |

The loader rejects version, unit, dimension, and exact payload-size mismatches.
Saves go to a sibling `.tmp` file first and publish with an atomic rename, so a
failed write does not damage the previous plate.
