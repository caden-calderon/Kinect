# Research Sources

All external claims in the discovery package trace here. Checked dates are
when this project verified the source; staleness risk is flagged. Status
codes: **A** = primary source read and it substantiates the cited claim
(for tools: code obtainable *and* license text read); **B** = public but
gated, partially verified, or single-source; **C** = paper/page only, no
usable release; **D** = documentation/issue-report evidence (supports a
claim about behavior, not an adoptable artifact); **J** = engineering
judgment by this package, no external source claimed.

## Sensor and capture

| Source | What it supports | Checked | Status / staleness |
| --- | --- | --- | --- |
| [OpenKinect/libfreenect2](https://github.com/OpenKinect/libfreenect2) | driver capability, decode paths, maintenance posture; API docs warn USB stability/error handling not production-verified | 2026-07-29 | A; maintenance-mode — treat as self-owned fork |
| [libfreenect2 API docs](https://openkinect.github.io/libfreenect2/) | streams, native formats, registration, calibration structs | 2026-07-29 | A; frozen protocol, low staleness. Color 15 Hz low-light behavior **confirmed on this unit by E1** (exposure pegged, 66.66 ms cadence — [E1](../experiments/E1.md)) |
| Issues [#1026](https://github.com/OpenKinect/libfreenect2/issues/1026), [#748](https://github.com/OpenKinect/libfreenect2/issues/748), [#1140](https://github.com/OpenKinect/libfreenect2/issues/1140), [#1202](https://github.com/OpenKinect/libfreenect2/issues/1202) | VA-API failure class; modern-toolchain build friction | 2026-07-29 | D (issue reports) |
| [kinect2_ros2 patched fork](https://gitioc.upc.edu/labs/kinect2_ros2) | modern GCC/CUDA buildability evidence | 2026-07-29 | B; single-source report |
| Local machine probes (lsusb, nvidia-smi, CMake caches, git logs) | §01 [measured] facts | 2026-07-29 | measured, this machine |
| ToF characterization literature (Sarbolandi et al. 2015; Lachat et al. 2015) | Kinect v2 noise/flying-pixel behavior | prior knowledge, sensor unchanged | C-class citations; old but sensor is frozen hardware |
| Device-JPEG bitrate (10–20 MB/s), Zstd depth ratio (2–3×) | storage math in 08 §2 | — | **J (estimates)**; measured by E1/E3 before use |
| HDF5 mid-write fragility (08 §3) | container comparison | — | J (engineering reasoning about non-journaled writes without SWMR); MCAP comparison does not hinge on it |

## Motion and flow

| Source | What it supports | Checked | Status |
| --- | --- | --- | --- |
| [NVOFA Application Note](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-application-note/index.html) | NVOFA present on Turing **except TU117** → absent on this T550; hardware flow ruled out | 2026-07-29 | A (vendor doc is explicit) |
| [OpenCV DISOpticalFlow docs](https://docs.opencv.org/4.x/de/d4f/classcv_1_1DISOpticalFlow.html) | DIS is the designed-for-speed CPU flow in OpenCV; preset tiers exist | 2026-07-29 | A for existence/design intent; **cost on this CPU is J → E2** |
| [OpenCV NvidiaOpticalFlow_2_0](https://docs.opencv.org/4.x/db/d70/classcv_1_1cuda_1_1NvidiaOpticalFlow__2__0.html) | what NVOFA integration would have looked like (moot on this GPU) | 2026-07-29 | D (retained for provenance of the ruled-out path) |

## Body models, HMR, tracking

| Source | What it supports | Checked | Status |
| --- | --- | --- | --- |
| [SMPL-X model license](https://smpl-x.is.tue.mpg.de/modellicense.html), [vchoutas/smplx](https://github.com/vchoutas/smplx) | non-commercial (research/education/non-commercial-artistic) terms; Meshcapade commercial route | 2026-07-29 | A (weights gated by registration) |
| [facebookresearch/MHR](https://github.com/facebookresearch/MHR) | Momentum Human Rig public under **Apache-2.0** → default proxy-lane candidate | 2026-07-29 | A |
| [facebookresearch/sam-3d-body](https://github.com/facebookresearch/sam-3d-body) + repo LICENSE, [HF checkpoint](https://huggingface.co/facebook/sam-3d-body-dinov3), [arXiv 2602.15989](https://arxiv.org/abs/2602.15989) | 3DB capability; **SAM License covers the repo's software and models** (not merely weights); checkpoints gated | 2026-07-29 | B (gated access) |
| [yangtiming/Fast-SAM-3D-Body](https://github.com/yangtiming/Fast-SAM-3D-Body), [arXiv 2603.15603](https://arxiv.org/pdf/2603.15603) | ~65 ms/frame on **RTX 5090** (their hardware, never this machine's); MIT code over SAM-licensed base | 2026-07-29 | B |
| [SMPLer-X LICENSE](https://github.com/SMPLCap/SMPLer-X), [SMPLest-X LICENSE](https://github.com/MotrixLab/SMPLest-X) | **S-Lab License, non-commercial** for both; FPS numbers are V100 | 2026-07-29 | A (license read via review pass) |
| [zju3dv/GVHMR LICENSE](https://github.com/zju3dv/GVHMR) | ZJU academic non-commercial; **derivatives must remain open-source; commercial requires contacting ZJU** | 2026-07-29 | A (license read) |
| [NVlabs/blade LICENSE](https://github.com/NVlabs/blade), [arXiv 2412.08640](https://arxiv.org/abs/2412.08640) | close-range HMR; **license restricted to academic purposes — personal artistic use not clearly permitted; treated as unusable without permission** | 2026-07-29 | A (license read); reference-only here |
| [MMPose rtmpose3d](https://github.com/open-mmlab/mmpose/tree/main/projects/rtmpose3d), [RTMW paper](https://arxiv.org/pdf/2407.08634) | RTMW3D capability; **code** Apache-2.0 | 2026-07-29 | B — code license read; **model weights, training datasets, and runtime deps not separately cleared; clear before adoption (roadmap phase 7 entry)** |
| [MediaPipe pose landmarker docs](https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/python), [holistic](https://ai.google.dev/edge/mediapipe/solutions/vision/holistic_landmarker), [LICENSE](https://github.com/google-ai-edge/mediapipe/blob/master/LICENSE) | maintained Tasks API, Linux/Python; **code** Apache-2.0 | 2026-07-29 (docs dated 2026-05) | B — same weights/deps caveat as MMPose |
| [SHELLS project page](https://syntec-research.github.io/SHELLS/) | calibrated multi-view input; no code found | 2026-07-29 | C |
| 3DGS avatar family — per-system attribution in 03 §E: [3DGS-Avatar](https://arxiv.org/html/2312.09228v3) (~30 min train, 50+ fps claims), [HuGS CVPR24](https://openaccess.thecvf.com/content/CVPR2024/papers/Moreau_Human_Gaussian_Splatting_Real-time_Rendering_of_Animatable_Avatars_CVPR_2024_paper.pdf), [RMAvatar](https://rm-avatar.github.io/), Mono-Splat (preprint 2025-12) | later identity lane plausibility; every number is its own paper's hardware | 2026-07-29 | C→B; research-grade |

## Reconstruction and recording

| Source | What it supports | Checked | Status |
| --- | --- | --- | --- |
| [Open3D TSDF/VoxelBlockGrid docs](https://www.open3d.org/docs/release/tutorial/t_reconstruction_system/integration.html) | GPU TSDF 2–4 ms/frame (their hw); MIT | 2026-07-29 | A |
| [mcap.dev](https://mcap.dev/), [foxglove/mcap](https://github.com/foxglove/mcap), [recovery CLI](https://mcap.dev/guides/cli) | format, official libs, conformance CI, recovery tooling, MIT | 2026-07-29 | A |
| [MCAP as ROS 2 / Isaac ROS default](https://foxglove.dev/blog/nvidia-announces-mcap-as-the-default-logging-format-for-isaac-ros-3-0) | ecosystem adoption | 2026-07-29 | D (vendor blog) |

## Unsourced-by-design (engineering judgment, labeled J)

The stack comparison in [05](05-architecture-options.md) — GL 4.6
sufficiency for additive point looks, CUDA↔GL interop maturity, PRIME
behavior expectations, Dear ImGui / egui suitability, tooling-ecosystem
depth (Nsight/RenderDoc vs. wgpu tooling) — is engineering judgment by
this package informed by ecosystem familiarity, not sourced claims. It is
presented as [inference] in 05/06 and is exactly what experiments E1/E4
exist to pressure-test on this machine. Where such judgment turns out
wrong, the experiment gates — not the prose — are the protection.

## Epistemic ground rules used

Paper/vendor performance numbers are **never** presented as this
workstation's; anything that must hold on the T550 is either [measured]
already or routed to an experiment in
[07-decision-experiments.md](07-decision-experiments.md). Licenses marked
unread are treated as restrictive until read; as of the Codex review pass
(2026-07-29), every license row above has been read except where a row
says otherwise explicitly.
