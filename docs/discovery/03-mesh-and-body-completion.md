# 03 — Mesh and Body Completion

Honest comparison of representation classes for the human form, from
directly measured to fully inferred, verified against availability,
license, Linux support, and the actual T550 4 GiB workstation
(2026-07-29). Labels: [measured] [source] [inference] [verify]
[hypothesis].

## 0. The semantic geometry model (proposed core)

Four layers, distinct in type, provenance, and rendering rights:

| Layer | Contents | Provenance | May be presented as |
| --- | --- | --- | --- |
| `ObservedSample` / `ObservedSurface` | unprojected depth points; triangulated depth-surface | sensor measurement | truth |
| `TrackedSignal` | body/hand/face landmarks + confidence + timestamps | ML on RGB(+depth) | annotation/driver, never geometry-truth |
| `InferredSurface` | model-completed geometry (parametric proxy, fitted mesh) + per-vertex confidence & provenance | model + prior | stylized/completed, visually distinguishable on demand |
| `ArtisticParticle` | emitted/simulated matter with lifecycle + source anchor | simulation | art, always |

Rules **[proposed architecture]**: every buffer carries its layer tag;
operators declare which layers they consume/emit; nothing may silently
promote inferred or artistic geometry into observed. Looks may *blend*
layers freely — the architecture just refuses to *confuse* them. Names may
change; the separation may not.

**Layer tag ≠ full provenance.** Most buffers are *derived*: temporally
filtered surfaces, registered color, transformed echo shells, jittered
samples, and estimated normals are products of measured data, not direct
measurements. Provenance therefore has two parts: the **layer tag** (which
of the four families it belongs to) and a **derivation chain** (ordered
tags such as `filtered`, `registered`, `reprojected`, `jittered`,
`normal-estimated`, plus source frame IDs). "Observed" means *derived only
from sensor measurement* — it never implies "unprocessed." Anything whose
chain includes a model or simulation step leaves the observed family by
construction.

## A. Live observed depth-surface mesh

Triangulate the 512×424 depth grid on the GPU; reject edges across
discontinuities/invalid samples; estimate normals; temporally stabilize.

- Fast (~1–2 ms class **[inference]**), zero dependencies, fully honest,
  shares the whole point pipeline.
- Front-facing shell only; topology churns at silhouettes; normals noisy at
  range. Named **depth-surface mode**, never "mesh of the person".
- Verdict: **core, early**. The only mesh in the launch path.

## B. Static / slowly accumulated volumetric fusion (TSDF)

KinectFusion-class integration; Open3D's tensor `VoxelBlockGrid` is
maintained, MIT-licensed, GPU-accelerated, integrating an RGB-D pair in
2–4 ms on their reference hardware **[source:
[Open3D docs](https://www.open3d.org/docs/release/tutorial/t_reconstruction_system/integration.html),
checked 2026-07-29]**.

- Rigid fusion assumes a static scene: a breathing, shifting human violates
  it — surfaces smear and double **[source: KinectFusion literature;
  inference]**. And the obvious ritual is *also* not rigid: a subject
  rotating in front of a fixed camera deforms between views exactly like
  any other motion, so naive turn-and-fuse produces the same smearing.
  Honest options for the optional scan ritual, in ascending effort:
  (a) piecewise capture — hold N static poses, fuse each segment rigidly,
  register the segments (tolerates small pose drift; produces a usable
  coarse shell, not a clean scan); (b) move the *camera* around a
  stationary subject (awkward with a powered Kinect, but geometrically
  correct with camera-pose tracking); (c) non-rigid fusion methods (class
  E — research-grade, not a lane we build on). The ritual lane plans for
  (a) and treats its output as a coarse attractor/identity reference, not
  scan-quality geometry.
- Verdict: **optional enrichment lane**; never in the live loop.

## C. Parametric full-human proxy (SMPL-X-class)

Fit a body-model mesh from tracked pose (+shape prior); measured depth can
constrain the visible front.

- SMPL-X model license: **non-commercial** research/education/artistic
  only; commercial via Meshcapade **[source:
  [SMPL-X model license](https://smpl-x.is.tue.mpg.de/modellicense.html)]**.
  Caden's personal artistic use fits; distributing/commercializing the tool
  with bundled weights does not. This is a *product-posture* decision →
  [10-open-decisions.md](10-open-decisions.md).
- A permissively-licensed alternative appeared in 2026: Meta's **Momentum
  Human Rig (MHR)** parametric representation is public under
  **Apache-2.0** **[source:
  [facebookresearch/MHR](https://github.com/facebookresearch/MHR), checked
  2026-07-29]** — which makes MHR the default candidate for the proxy lane
  and confines the SMPL-X license question to cases where SMPL-X-specific
  tooling is genuinely needed.
- Runtime: fitting from landmarks at 5–15 Hz in a sidecar is an
  **unmeasured planning estimate [inference]**. E6 deliberately excludes
  parametric fitting (it tests whether capsules suffice), so this number
  is *not* verified by E6: if E6 concludes capsules are insufficient, a
  dedicated follow-up spike (E6b, defined then) measures fitting runtime
  and 4 GiB fit *before* the proxy lane is adopted. The proxy is consumed
  as an *attractor/collision/coordinate volume* (capsules or coarse SDF),
  which tolerates low rate + smoothing far better than a rendered mesh
  would.
- Verdict: **the keystone inferred layer** — but consumed as a volume/frame
  system first, a visible surface second.

## D. Monocular / RGB-D human mesh recovery (HMR)

Verified 2026-07-29:

| System | What | Code/weights | License | Realistic role here |
| --- | --- | --- | --- | --- |
| [SAM 3D Body](https://github.com/facebookresearch/sam-3d-body) (Meta, CVPR 2026) | promptable full-body+hands+feet HMR on MHR | code public; checkpoints gated on HF | **SAM License covers the repo's software *and* models** [source: repo LICENSE]; MHR itself Apache-2.0 | highest-quality offline per-take fitting |
| [Fast-SAM-3D-Body](https://github.com/yangtiming/Fast-SAM-3D-Body) | training-free 10.9× speedup of above; ~65 ms/frame on RTX 5090 [source] | public, auto-downloads | MIT (code; base weights still SAM License) | T550 est. several-hundred ms/frame → low-Hz provider or offline [inference] |
| [SMPLer-X](https://github.com/SMPLCap/SMPLer-X) / [SMPLest-X](https://github.com/MotrixLab/SMPLest-X) | scaled expressive HMR | public | **S-Lab License, non-commercial** [source: repo LICENSEs]; SMPL-X deps also non-commercial | offline alternative; S-variant ~36 fps on **V100** — T550 is far weaker |
| [GVHMR](https://github.com/zju3dv/GVHMR) | world-grounded temporal HMR (SigAsia 2024/TPAMI 2026) | public | **ZJU academic, non-commercial; derivatives must stay open-source; commercial use requires contacting ZJU** [source: LICENSE] | offline takes; strong temporal stability |
| [BLADE](https://github.com/NVlabs/blade) (NVIDIA, CVPR 2025) | close-range HMR solving perspective/Tz — matches the portrait use case | public | **NVIDIA license restricted to academic purposes — personal artistic use is *not* clearly permitted; unusable here without explicit permission** [source: repo LICENSE.md] | reference only unless permission obtained |
| [RTMW3D / rtmpose3d](https://github.com/open-mmlab/mmpose/tree/main/projects/rtmpose3d) (MMPose) | real-time whole-body 3D pose (not mesh) | public | **Apache-2.0** | the *live* tracked-signal provider candidate |

Common truths **[inference — assessed, not measured]**: live viability of
these mesh-recovery systems on a 4 GiB T550 is judged *unlikely* from
their published hardware baselines (V100/RTX-5090-class), and this
assessment is falsifiable — no experiment currently covers the smallest
variants, and one can be added if a live-HMR mode ever becomes a product
want. Until then they are treated as **offline/low-rate providers**
consuming *recorded takes* — which Caden's raw-take recording model makes
first-class. True-depth incorporation is rare in this family; depth is
better used to *correct/scale* fitted results than as model input.

## E. Dynamic non-rigid reconstruction & neural/volumetric humans

- DynamicFusion/DoubleFusion lineage: research demos, fragile tracking,
  no maintained tooling — not a component, an inspiration **[source/
  inference]**.
- 3D Gaussian human avatars: monocular video → animatable per-person
  avatar with a training stage. Representative published claims, each from
  its own paper's hardware (none from this workstation): **3DGS-Avatar**
  reports ~30 min training and 50+ fps rendering; **HuGS/Human Gaussian
  Splatting** (CVPR 2024) targets real-time rendering of animatable
  avatars; **RMAvatar** and **Mono-Splat** (preprint, 2025-12) are
  mesh-embedded/webcam-oriented variants **[source per system, checked
  2026-07-29]**. VRAM requirements are undocumented across the family;
  code is research-grade. → plausible **later identity lane** (matches
  Caden's "identity later"), explicitly deferred.

## F. Multi-view head reconstruction (SHELLS)

[SHELLS](https://syntec-research.github.io/SHELLS/) requires **calibrated
simultaneous multi-view input** (works from 2+ views), outputs 18k-vertex
semantically-corresponded head meshes at 0.08 s/frame; **no public code
found** (checked 2026-07-29) **[source]**. A single moving Kinect does not
satisfy its input model for dynamic performance (temporal views ≠
simultaneous calibrated views); a *static* head + slow orbit is a partial,
unpromisable exception. Verdict: **reference-only; not on any roadmap
path.**

## Comparison matrix

| | A depth-surface | B TSDF ritual | C parametric proxy | D HMR offline | E Gaussian avatar | F SHELLS |
| --- | --- | --- | --- | --- | --- | --- |
| Geometry provenance | observed | observed (accumulated) | inferred | inferred | inferred (trained) | inferred |
| Input needs | depth stream | slow-turn ritual | landmarks(+depth) | RGB(-D) takes | video + training | calib. multi-view |
| Live? | yes | no (ritual) | low-Hz live | no | render yes, fit no | no |
| Topology stability | churns | good (static) | perfect (fixed) | perfect (fixed) | n/a (splats) | good |
| Temporal stability | per-frame noise | n/a | smoothing-friendly | per-system | good | n/a |
| Identity fidelity | exact (front) | good | shape-prior generic→fitted | moderate | high | high (head) |
| Hands/face | as measured | as measured | SMPL-X/MHR yes | varies (3DB yes) | limited | face only |
| Code+weights public | n/a | yes (Open3D) | yes (restricted weights) | yes (mixed gating) | yes (research) | **no** |
| License posture | n/a | MIT | SMPL-X non-comm / MHR permissive | mixed, mostly non-comm | mixed | n/a |
| Linux on T550 4 GiB | trivial | yes | yes (sidecar) | offline only | training marginal | n/a |
| Biggest failure | edge churn | motion smear | pose-tracker dropouts | close-range/truncation (BLADE addresses it, but its academic-only license blocks use here) | fragile tooling | inputs unobtainable |
| Best role | **live honest mesh** | optional personal shell | **live inferred volume** | **offline quality lane** | later identity lane | none (reference) |

## Recommended layered strategy [hypothesis — challenged and retained]

1. Observed depth points + depth-surface: immediate truth, launch path.
2. Tracked landmarks (Apache-licensed live provider): body-local frames,
   bone velocities, emission targets.
3. Parametric proxy fit at low Hz in a sidecar: the *unseen-volume*
   provider — attractor fields, collision, body-local particle memory.
   Rendered as a surface only in explicitly-stylized modes.
4. Offline HMR on recorded takes: the quality/animation lane, applied to
   the negative (raw take) after the performance.
5. Gaussian-avatar identity work: parked until (1)–(4) are real.

Challenge honestly considered: *skip the proxy, do artistic completion
only from tracked capsules?* Cheaper, license-free — and in fact stage 2
alone already enables Magnetic Completion's attractor. The proxy earns its
place only when capsules feel too crude; the roadmap gates it behind that
finding (E6) instead of assuming it.
