# Product

## Register

product

## Users

Caden is the primary user for the foreseeable future. He will use the tool
locally on Linux as a creative technologist working with Kinect v2 depth data,
live visuals, recorded performances, character motion, and experimental
human representations. The repository may later become something other artists
can clone, but packaging for a broad audience is not a first-release constraint.

## Product Purpose

Create a responsive visual instrument in which live input and recorded takes are
equal-priority sources for the same real-time pipeline. The system should turn
RGB-D data into dynamic point clouds, depth-derived surfaces, and future tracked
or reconstructed human forms while providing enough control to craft a precise,
visually striking look.

Success means the capture, rendering, recording, and replay foundations are
stable and fast before interface polish or a node editor becomes the focus.

## Brand Personality

Creative, cinematic, instrument-like.

The supplied mood board favors luminous particles, human silhouettes, flowing
fields, spectral depth, high contrast, and forms that can dissolve or cohere.
It is a direction for visual possibility, not a requirement to imitate one
specific image.

## Anti-references

- The discarded `/home/caden/projects/point-cloud-engine` architecture.
- A UI-first demo hiding an unreliable or slow capture/rendering backend.
- A literal TouchDesigner clone.
- A node editor treated as the product before the underlying operators work.
- A point-cloud-only architecture that prevents depth meshes, tracking signals,
  or future representations from using the same frame pipeline.

## Design Principles

1. **Live is the truth test.** Every core decision must survive sustained,
   observable real-time use on the actual Kinect and workstation.
2. **Recording is a tap, not another mode.** Capturing a take must not change the
   visual pipeline or block the viewport.
3. **Raw data remains reusable.** Preserve synchronized sensor data,
   calibration, and timing so new looks and tracking models can be applied later.
4. **Creative range comes from composable operators.** Build a graph-shaped
   engine internally, but begin with direct controls and presets rather than a
   node-editor UI.
5. **The artwork owns the screen.** Controls and telemetry should help shape and
   trust the output, then get out of the way.
6. **Measured performance beats architectural fashion.** Select and retain
   technologies based on profiling, stability, and maintainability on the target
   machine.

## Accessibility & Inclusion

No specialized accessibility requirement has been stated for the personal
first release. The control surface should nevertheless support readable
contrast, scalable text and controls, keyboard operation, and state indicators
that do not rely on color alone. Reduced UI motion should be possible; the
rendered artwork may intentionally remain motion-heavy.
