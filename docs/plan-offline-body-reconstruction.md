# Plan — offline full-body reconstruction sidecar

**Status (2026-08-09): the local extractor and validator are implemented and
verified; no model checkout, checkpoint download, upload, RunPod pod, or paid
compute has been started.**

This is the offline quality lane Caden described: record a normal Kinect take,
recover a coherent full-body mesh with SAM 3D Body or a related model on rented
hardware, align it back into Kinect metric space, then render that inferred
mesh as a stable point cloud through the existing replay instrument.

It is deliberately separate from Spectral Backfill. Spectral Backfill is a
live `Layer::Artistic` volume. An offline recovered body is
`Layer::Inferred`: useful and potentially much more complete, but still not a
measurement of surfaces hidden from the camera.

## Candidate order

1. [SAM 3D Body](https://github.com/facebookresearch/sam-3d-body) is the first
   quality baseline. It predicts an MHR full-body mesh from a single image; the
   official checkpoint is gated and the repository/model use is governed by
   the SAM License.
2. [Fast SAM 3D Body](https://github.com/yangtiming/Fast-SAM-3D-Body) is the
   speed comparison on the same base representation. Its reported ~65 ms on an
   RTX 5090 is the authors' benchmark, not a measurement from this project.
3. [MHR](https://github.com/facebookresearch/MHR) is the common parametric body
   representation and topology contract. Its repository is Apache-2.0; that
   does not override the license of a SAM 3D Body checkpoint or derivative.
4. Clothing-oriented alternatives such as
   [ECON](https://econ.is.tuebingen.mpg.de/) or
   [SIFU](https://river-zhang.github.io/SIFU-projectpage/) are later comparison
   experiments only if the body-model silhouette is too generic. They add
   harder temporal correspondence and animation problems.

The first result should answer quality and temporal stability, not optimize
throughput. A faster implementation only wins after it reproduces an accepted
mesh on the same representative frames.

## Data flow

```text
local raw MCAP
  -> local source-addressed extraction bundle
  -> explicit opt-in upload of that bundle only
  -> pinned remote inference container
  -> raw model results + run manifest
  -> local metric alignment and temporal stabilization
  -> derived body sidecar keyed to source identity
  -> existing replay pipeline
  -> stable-topology inferred point cloud
```

The raw MCAP never leaves the local machine. Extraction reuses the production
take reader/assembler so the RGB/depth pairing is exactly the pairing replay
uses; it must not independently pair streams by nearest timestamp.

## Local extraction bundle v1

One immutable directory per source take:

```text
<take>.body-input-v1/
  manifest.json
  calibration.json
  frames/
    <frame_id>.depth.u16le
  rgb/
    <jpeg_sha256>.jpg
```

Each RGB file is the bit-exact device JPEG, never decoded or re-encoded. The
manifest preserves every per-depth-frame pairing while referencing one
portable `rgb/<sha256>.jpg` file for repeated color frames. This matters because
the Kinect commonly supplies 15 Hz color alongside 30 Hz depth. The depth
plane is the existing 512x424 row-major little-endian `u16` data in 0.1 mm
units.

`manifest.json` contains:

```json
{
  "schema": "kstudio.body-input.v1",
  "source_take_sha256": "...",
  "calibration_content_hash": "0000000000000000",
  "frame_count": 0,
  "frames": [
    {
      "frame_id": 0,
      "depth_seq": 0,
      "color_seq": 0,
      "t_device_depth": 0,
      "t_device_color": 0,
      "rgb_file": "rgb/<jpeg_sha256>.jpg",
      "depth_file": "frames/0.depth.u16le",
      "rgb_sha256": "...",
      "depth_sha256": "...",
      "color_age_ms": 0.0,
      "skew_ms": 0.0
    }
  ]
}
```

Frames without paired color are retained in the manifest with a null
`rgb_file` but are not submitted to an RGB-only model. This makes omissions
auditable instead of silently renumbering the inference sequence.

`build/src/kstudio-body-extract` is local and always read-only with respect to
the take. Dry run is the default and creates no directory:

```bash
build/src/kstudio-body-extract --take takes/<take>.mcap
build/src/kstudio-body-extract --take takes/<take>.mcap \
  --output takes/<take>.body-input-v1 --write
```

Write mode refuses an existing destination, writes to a same-parent temporary
directory, validates every path, size, and SHA-256, and only then publishes by
rename. It is restartable as a fresh transaction rather than resumable halfway
through an artifact. Synthetic/golden tests cover exact replay pairing,
bit-exact depth encoding, immutable publication, and one-byte tamper detection.

The read-only `e4-input.mcap` dry run on 2026-08-09 reported 603/603 depth
frames, 21 frames without paired color, 291 unique JPEGs, and a 483.66 MiB
portable bundle (249.68 MiB depth + 233.54 MiB deduplicated RGB). The take was
657.66 MiB; naively repeating paired JPEGs would have added 467.11 MiB instead.
Nothing was written.

## Remote result bundle v1

Remote inference writes a new directory; it never modifies the input bundle:

```text
<take>.body-result-v1/
  run.json
  index.jsonl
  raw/
    <frame_id>.npz
  preview/
    <frame_id>.jpg
```

`run.json` pins the input manifest hash, git commit, container image digest,
model/checkpoint identifiers and hashes, license acknowledgement, GPU type,
library versions, command, start/end time, and success/failure counts. Secrets
and signed checkpoint URLs are forbidden.

Each `index.jsonl` record repeats `frame_id`, `depth_seq`, and `color_seq`, then
records `ok`, provider confidence, detected body count, chosen body index,
provider-native camera values, raw payload path/hash, and any failure reason.
The `.npz` payload preserves provider-native arrays without pretending their
coordinates are Kinect metric coordinates. Exact keys are frozen only after a
real pinned inference result is inspected; inventing them before checkpoint
access would create a false contract.

## Canonical derived sidecar v1

Local post-processing writes the renderer-facing artifact:

```text
<take>.body-sidecar-v1/
  manifest.json
  topology.npz
  frames/
    <frame_id>.npz
```

The manifest uses schema `kstudio.inferred-body.v1` and binds to the source take
SHA-256, calibration hash, input manifest hash, remote run hash, topology ID,
vertex/joint counts, coordinate conventions, and derivation chain.

`topology.npz` contains the one canonical triangle index buffer and optional
stable point-sampling table. Per-frame payloads contain at minimum:

- source `frame_id`, `depth_seq`, and `color_seq` again;
- canonical-topology vertices in model space;
- a 4x4 `model_to_depth_cam` transform;
- joints in depth-camera meters when available;
- constant/slow shape state separated from per-frame pose state;
- confidence, reprojection error, depth-alignment error, valid observed-depth
  support count, temporal-discontinuity score, and status flags.

Coordinates follow the current project convention: right-handed depth-camera
meters, +X right, +Y up, -Z forward. A sidecar frame with failed or low-quality
alignment is marked invalid; replay falls back to observed plus artistic
layers rather than holding an unbounded stale mesh.

## Metric alignment and temporal stabilization

The single-image model's camera/scale is not accepted as Kinect world truth.
Local post-processing:

1. selects one consistent subject identity;
2. estimates one robust shape state over the clip instead of allowing body
   shape to breathe frame by frame;
3. uses projected mesh/joint correspondences and valid Kinect depth to solve a
   robust similarity initialization;
4. refines per-frame rigid/root placement against visible depth while keeping
   the parametric pose prior bounded;
5. rejects foreground occluders and high residuals rather than pulling the
   whole mesh toward them;
6. smooths pose/transform only in source-frame time and records the filter
   settings in the manifest.

This stage is where the Kinect adds real value: metric scale, root placement,
and visible-surface evidence. The model supplies topology and a prior for the
unseen body.

## Point-cloud rendering contract

Point sampling is generated once from canonical topology with stable triangle
IDs and barycentric coordinates. Every replay frame evaluates those same
samples on the deformed mesh, so points do not boil due to resampling. Mesh
points remain `Layer::Inferred`; particles emitted from them are
`Layer::Artistic`.

The renderer must expose observed-only, inferred-only diagnostic, and composed
views. The composed default keeps measured Kinect points brighter and sharper,
with the inferred backside cooler/dimmer, matching the provenance vocabulary
used by Spectral Backfill.

## Privacy, spend, and deletion gate

- Cloud processing is opt-in per take. RGB frames are sensitive biometric
  imagery; the extraction preview must show exactly what will upload.
- Upload only the minimal bundle, never `takes/`, journals, unrelated frames,
  or the repository.
- Use a fresh RunPod network volume or container workspace with a named job ID;
  download and hash results before deletion.
- Set an explicit maximum runtime/spend and automatic shutdown. Current GPU
  pricing must be checked at launch rather than copied into the contract.
- Keep a deletion manifest for remote input/result paths and verify deletion;
  local derived products remain linked to the source take for review/delete.

Before any paid run, Caden must choose the representative take, approve the
exact upload preview and model license, and authorize the spend cap. A sensible
first gate is 5-10 seconds sampled at low cadence plus several deliberately
hard frames (arm toward camera, profile, crossed limbs), not a whole session.

## Implementation order

1. **Done 2026-08-09:** add the read-only extractor and manifest validator with
   synthetic/golden tests; inspect its dry-run report locally.
2. Choose one short source take with Caden and freeze the upload manifest.
3. Obtain/checkpoint access and pin a local container recipe without renting a
   GPU yet.
4. Run 5-10 representative frames remotely under a small explicit spend cap.
5. Inspect raw outputs before freezing `.npz` provider keys.
6. Implement Kinect metric alignment and the canonical sidecar validator.
7. Add stable-topology point sampling and replay ingestion behind an explicit
   inferred-layer toggle.
8. Only then compare full SAM 3D Body with Fast SAM 3D Body or a
   clothing-oriented alternative.
