# Fable Discovery and Architecture Goal

Copy the block below into a fresh interactive Fable/Claude session launched from
`/home/caden/projects/kinect`.

```text
/goal

Lead an interactive, research-first product and architecture phase for the
clean-sheet Kinect Creative Studio in:

  /home/caden/projects/kinect

Do not implement the application in this goal.

Your job is to understand Caden's creative intent, ask focused questions, study
the current technical landscape, reason from the actual Kinect v2 and workstation
constraints, explore multiple representations and visual systems, and produce a
clear architecture plus an experiment-driven roadmap for Caden to approve.

This is not a request for a quick stack recommendation. Think deeply about the
point-cloud engine, temporal behavior, RGB-D fusion, observed versus inferred
human geometry, recording/replay, and the artistic control model. The result
should be strong enough that a later implementation goal can proceed without
inventing the product or architecture while coding.

HARD PHASE GATE

During this goal you may:

- read project files, mood-board images, source code, papers, official
  documentation, repositories, model cards, licenses, and benchmark reports;
- browse the web and use current primary sources;
- inspect the current machine and run safe, read-only diagnostics using tools
  already installed;
- inspect the discarded prototype or local libfreenect2 checkout read-only;
- create and edit planning/research Markdown files inside this repository;
- create diagrams, comparison tables, and visual direction probes when the
  available tools support them.

During this goal you must not:

- write production source code;
- create an application scaffold, package manifest, shader, build system, FFI
  bridge, or test suite;
- install dependencies or modify system configuration;
- modify /home/caden/libfreenect2;
- modify /home/caden/projects/point-cloud-engine;
- turn a research question into an implementation because the answer seems
  obvious;
- execute the quarantined prompt at
  prompts/archive/FABLE_IMPLEMENTATION_GOAL_DRAFT.md;
- begin implementation after presenting the plan, even if Caden approves it in
  the same message. End at the approval gate and prepare a new implementation
  goal for a later session.

If a tiny prototype or benchmark would materially resolve an architecture
question, describe the proposed experiment, expected evidence, timebox, and
success criteria. Ask Caden for explicit approval before writing or running that
prototype. Read-only hardware probes using existing binaries are allowed, but
do not repeat disruptive tests without a reason.

READ FIRST

Read these files completely:

  /home/caden/projects/kinect/README.md
  /home/caden/projects/kinect/PRODUCT.md
  /home/caden/projects/kinect/docs/PROJECT_BRIEF.md
  /home/caden/projects/kinect/references/moodboard/README.md

Inspect all four mood-board images at full useful resolution:

  /home/caden/projects/kinect/references/moodboard/01-luminous-human-point-cloud.png
  /home/caden/projects/kinect/references/moodboard/02-dispersing-hands-point-cloud.png
  /home/caden/projects/kinect/references/moodboard/03-ethereal-flowing-profile.png
  /home/caden/projects/kinect/references/moodboard/04-dense-particle-profile.png

Do not read the quarantined implementation draft until you have independently
formed the research findings and credible architecture options. Near the
synthesis stage, you may audit it only as a completeness checklist:

  /home/caden/projects/kinect/prompts/archive/FABLE_IMPLEMENTATION_GOAL_DRAFT.md

It is not an accepted plan. Do not let its phase structure or technology
assumptions anchor the research. Expect to replace large parts of it.

Reference-only paths:

  /home/caden/libfreenect2
  /home/caden/projects/point-cloud-engine

The old point-cloud-engine was produced by a prior model and is not trusted.
Do not inherit its Rust/WGPU choice, architecture, milestones, or UI. It also
contains uncommitted work. Inspect only a narrowly useful fixture, diagnostic,
or contract and independently validate anything learned.

KNOWN PRODUCT INTENT

- Caden is the primary user.
- This is a local Linux creative instrument first; it may become cloneable.
- Live performance and recording are equal priorities.
- The same pipeline should accept live RGB-D and recorded takes.
- The backend, frame system, renderer, recording path, and visual quality matter
  before a polished UI.
- Direct dials/sliders/panels and presets are enough initially.
- The engine may be graph-shaped internally, but Caden does not need a visible
  node editor at the start.
- Point clouds are the first representation, not the permanent limit.
- A live mesh, tracked/inferred body, animation capture, hands, face, and later
  DCC export are all legitimate directions, but their order is unresolved.
- The mood board is a creative north star, not a request for four hardcoded
  effects.

CADEN'S CURRENT CREATIVE QUESTIONS

Treat these as the heart of the discovery, not side notes:

- How should depth become a point field or another representation?
- How can the human form feel alive instead of like a static grid of dots?
- Can arm motion leave a drifting volumetric after-trail?
- Should particles detach from the body, inherit limb velocity, curl through a
  flow field, decay, and later return or cohere?
- Can RGB contribute more than literal per-point color?
- What is the right kind of live mesh?
- A single Kinect observes mainly the facing surface. Should that honest
  half-body shell become the aesthetic?
- Could the unobserved back be filled with drifting particles rather than
  pretending it was measured?
- Could pose and RGB-D fit an inferred full human mesh, then sample that mesh as
  points while preserving the measured front?
- Could a slowly accumulated scan or learned body prior complete the unseen
  geometry?
- How should the system visually communicate the difference between measured,
  tracked, inferred, and purely artistic geometry?

Do not collapse these questions into one "use SMPL-X" answer. Several approaches
may coexist as representations or modes.

PHASE 1 - ORIENTATION, THEN INTERVIEW

First, orient yourself from the files and images. Reflect back a short,
specific statement of what you believe Caden is trying to make.

Then begin a real discovery interview before making architecture decisions.

Interview rules:

- Ask only 2-3 questions at a time.
- Use assert-then-confirm language when the existing brief strongly suggests an
  answer.
- Explain briefly why each question changes the design or architecture.
- Wait for Caden's answers before continuing.
- Ask follow-up rounds only for material gaps.
- Do not dump a 20-question questionnaire.
- Do not ask Caden to choose libraries or concurrency patterns. Ask about the
  desired experience, creative truth, workflow, and acceptable tradeoffs; you
  own the technical translation.

The first question bundle should focus on:

1. The capture/performance scene: full body versus torso/portrait, typical
   distance, whether Caden moves around or faces the sensor, and whether other
   people may enter.
2. The intended relationship between measured and inferred form: visually honest
   front shell, stylized completion, plausible full-body proxy, or modes that can
   move between them.
3. The feeling of motion: ghost/echo, fluid drift, explosive shedding, magnetic
   reassembly, filament/ribbon behavior, or another direction—and what should
   respond directly to the performer's motion.

Later question bundles should resolve only what remains important, such as:

- whether the first recordings are final volumetric artworks, reusable raw
  performances, animation/mocap sources, or all three with different outputs;
- whether a completed mesh needs to preserve Caden's body identity or merely
  provide a coherent humanoid volume;
- how much latency versus temporal smoothness is acceptable;
- whether turns/slow scan calibration sequences are acceptable before a live
  performance;
- whether the RGB look should be literal, monochrome, painterly, edge-driven,
  motion-driven, or selectable;
- how Caden expects to control looks during performance;
- output destinations and quality expectations;
- privacy/storage expectations for raw RGB recordings.

Do not begin the full research synthesis until the first interview round has
been answered.

PHASE 2 - RESEARCH PROGRAM

After the interview, research the following workstreams using current primary
sources wherever possible. Record source links, dates, licenses, hardware
requirements, whether code and weights are actually available, and whether
claims are documented, measured, or inferred.

1. Kinect v2 capture and calibration

- Current state and practical maintenance risk of libfreenect2 on modern Linux.
- RGB, depth, IR, native formats, registration, intrinsics/extrinsics, device
  timestamps, and achievable synchronization.
- Color decode options and the observed CPU packet-loss/VA-API failure on this
  machine.
- What should be preserved from the sensor versus converted for rendering.
- Failure modes, dropped-frame semantics, and calibration provenance.

2. Canonical RGB-D and geometry representation

- Correct metric unprojection from depth pixels with camera intrinsics.
- Registered color versus independent color-camera sampling.
- Stable source-pixel IDs and where they fail as surface identity when the body
  moves.
- Normals, confidence, discontinuity boundaries, body masks, and coordinate
  spaces.
- Data structures that can serve observed points, a depth surface, inferred
  geometry, temporal particles, recording, and later tracking without forcing
  them into one undifferentiated point array.

Propose explicit semantic layers, for example:

- ObservedSample / ObservedSurface: directly supported by sensor depth.
- TrackedSignal: body/hand/face landmarks with confidence and timestamps.
- InferredSurface: model-completed geometry with confidence/provenance.
- ArtisticParticle: emitted or simulated geometry with a lifecycle and source
  relationship.

The names may change, but the architecture must not confuse hallucinated or
stylized geometry with measured depth.

3. Temporal coherence and an "alive" point field

Compare techniques at increasing levels of fidelity and cost:

- screen-space feedback and motion trails;
- depth-aware reprojection of previous frames;
- optical flow on RGB combined with depth;
- scene flow or projective 3D correspondence;
- nearest-neighbor or surfel tracking;
- skeleton/limb velocity as a coarse motion field;
- persistent GPU particles emitted from the observed surface;
- curl noise, vector fields, attraction, drag, turbulence, decay, and collision
  around a body proxy or signed-distance field;
- body-local versus world-local particle memory;
- trails made from history samples versus free particles;
- how occlusion, disappearing depth, and reappearing limbs should behave.

For each, describe:

- what motion truth it preserves;
- latency and stability;
- visual failure modes;
- GPU/memory cost on the NVIDIA T550 4 GiB;
- how it responds to a fast arm swing;
- which parameters would make it creatively useful.

Develop at least three concrete visual-system concepts, such as:

- **Echo Body:** depth-aware historical shells that stretch and fade according
  to measured motion.
- **Shedding Field:** persistent particles emitted from fast-moving surfaces,
  inheriting velocity before curl/drag takes over.
- **Magnetic Completion:** measured front geometry remains crisp while inferred
  or purely artistic particles drift into the unobserved volume and are softly
  attracted toward a body proxy.

These are hypotheses, not required names. Improve or replace them after studying
the mood board and Caden's answers.

4. RGB plus depth as a creative signal

Research and propose uses beyond assigning RGB to points:

- calibrated color projection;
- luminance, hue, saturation, edges, texture, and semantic regions as emission
  or force masks;
- RGB optical flow fused with depth for motion vectors;
- chromatic echoes that separate over time;
- subject/background segmentation and depth matte cleanup;
- color-space choices, exposure, tone mapping, monochrome conversion, gradient
  maps, and selective color;
- sampling high-resolution RGB detail onto lower-resolution depth geometry;
- generative or painterly color treatment that remains temporally coherent;
- optional IR/depth confidence as a visual channel.

Separate reliable real-time techniques from speculative research.

5. Mesh and body-completion strategies

Do not search for one universal "best mesh tool." Compare at least these
representation classes:

A. Live observed depth-surface mesh

- Triangulate the Kinect depth grid.
- Reject edges across discontinuities and invalid samples.
- Estimate normals and stabilize temporally.
- Fast and honest, but front-facing and topologically unstable.

B. Static or slowly accumulated volumetric fusion

- TSDF/KinectFusion/Open3D-class approaches.
- Useful if Caden performs a scan or turns slowly before use.
- Analyze why ordinary rigid fusion breaks on a moving/deforming person.

C. Parametric full-human proxy

- Fit SMPL/SMPL-X-class geometry from pose, RGB, and preferably measured depth.
- Use measured depth to constrain the visible surface while a body prior fills
  the unseen back.
- Research temporal stability, identity/shape fitting, hands/face support,
  licenses, model availability, runtime, and how well it can run on this
  workstation.
- A proxy can drive particles or provide an attraction/collision volume even if
  it is not shown as a literal mesh.

D. Monocular or RGB-D human mesh recovery

- Evaluate current credible systems rather than relying on name recognition.
- Candidate families to verify include BLADE, SMPLer-X/SMPLest-X, OSX,
  PyMAF-X-class work, WHAM/GVHMR-style temporal recovery, RTMW3D/MMPose, and
  newer 2025-2026 alternatives.
- Verify code, weights, license, Linux setup, inference speed, VRAM, temporal
  consistency, close-range behavior, body/hand/face coverage, and whether true
  depth can be incorporated.

E. Dynamic non-rigid reconstruction and neural/volumetric humans

- Research DynamicFusion/DoubleFusion-class ideas and meaningful newer work.
- Include neural points, 3D Gaussian humans, implicit surfaces, or avatar models
  only where they map to this actual use case.
- Distinguish offline research demos from maintainable live components.

F. Multi-view head reconstruction

- Verify the actual inputs and availability of Google SHELLS and relevant newer
  methods.
- Explain whether a single moving Kinect/RGB camera can produce the calibrated
  multi-view evidence they require and under what motion/static assumptions.
- Do not promise animation-ready head reconstruction from a live single view.

Build a comparison matrix with:

- observed versus inferred geometry;
- input requirements;
- live versus offline suitability;
- topology stability;
- temporal stability;
- identity fidelity;
- hands/face support;
- public code and weights;
- license;
- Linux compatibility;
- expected FPS/latency and VRAM;
- integration complexity;
- biggest failure mode;
- best role in this product.

Recommend a layered strategy rather than prematurely choosing a single
representation. A likely shape to evaluate is:

- observed depth points/surface for immediate truth;
- a tracked parametric proxy for body-local coordinates and unseen volume;
- artistic particles bridging measured and inferred regions;
- higher-quality offline reconstruction/export as a later lane.

This is only a hypothesis. Challenge it.

6. Tracking and animation signals

- MediaPipe body, hands, face, and holistic options.
- MMPose/RTMW/RTMW3D and credible current alternatives.
- Depth-fusing image landmarks into metric 3D.
- Landmark confidence, occlusion, temporal filtering, and coordinate alignment.
- Skeleton conventions and later Blender/DCC retargeting.
- Whether tracking should be a core dependency, an optional provider process, or
  an offline analysis pass.

7. Point/particle engine and renderer architecture

Explore the best conceptual pipeline before selecting libraries:

  source RGB-D
    -> calibration/registration
    -> observed geometry
    -> optional tracking and inferred body
    -> motion/correspondence field
    -> representation and particle operators
    -> render and post-processing
    -> recorder/outputs

Reason about:

- CPU/GPU ownership;
- texture/buffer formats;
- compute scheduling;
- bounded queues and backpressure;
- latest-frame viewport versus loss-accounted recorder;
- temporal history and persistent simulation buffers;
- parameter schemas and preset reproducibility;
- deterministic playback;
- GPU debugging and profiling;
- extension seams that do not require a visible node editor;
- how one operation can consume measured depth, tracked signals, an inferred
  proxy, or artistic particles without type confusion.

8. Stack and process architecture

Compare credible implementation shapes from first principles:

- C++ native capture/core/render/UI;
- Rust core/rendering with a narrow C/C++ libfreenect2 bridge;
- a mixed native core plus separate optional ML worker;
- other shapes only if they materially improve this product.

Evaluate capture integration, memory ownership, shader/compute tooling,
profiling, recording libraries, UI/control integration, build reproducibility,
agent maintainability, and failure isolation.

Do not select a stack only from desk research if a bounded experiment is needed.
Instead, specify the smallest decision-resolving experiments to run after Caden
approves the research plan.

9. Recording, replay, and future animation

- Raw depth/color/IR/calibration/timestamp preservation.
- Storage and compression estimates at realistic durations.
- MCAP versus credible alternatives; do not mandate it without comparison.
- Crash/interruption recovery and indexes.
- Look/preset snapshots as non-destructive metadata.
- Tracking streams aligned to raw frames.
- Deterministic replay as the test harness for visual development.
- Later output options: rendered video, point caches, geometry caches, skeleton
  animation, Blender workflows, and what should remain explicitly deferred.

10. Control model and creative workflow

Plan direct controls first:

- source and playback;
- observed/inferred/artistic layer mix;
- point/surface geometry;
- emission and particle lifecycle;
- motion response;
- trails/history;
- forces and flow;
- color/RGB mapping;
- post-processing;
- camera/output;
- recording and diagnostics;
- presets and reset behavior.

Identify which controls should be exposed, derived, grouped, automatable, or
hidden as expert settings. The internal system may be graph-shaped, but do not
design a node-editor UI.

PHASE 3 - SYNTHESIS AND DECISION DOCUMENTS

After research and interview rounds, create:

  docs/discovery/README.md
  docs/discovery/00-user-brief.md
  docs/discovery/01-sensor-and-rgbd-foundation.md
  docs/discovery/02-living-point-field.md
  docs/discovery/03-mesh-and-body-completion.md
  docs/discovery/04-visual-system.md
  docs/discovery/05-architecture-options.md
  docs/discovery/06-recommended-architecture.md
  docs/discovery/07-decision-experiments.md
  docs/discovery/08-recording-and-outputs.md
  docs/discovery/09-implementation-roadmap.md
  docs/discovery/10-open-decisions.md
  docs/discovery/11-codex-review.md
  docs/discovery/research-sources.md

Use diagrams where relationships or frame flow are genuinely clearer than prose.

Required qualities:

- Separate verified facts, source claims, engineering inference, creative
  hypotheses, and unresolved questions.
- Cite primary sources near the claims they support.
- Mark source dates and possible staleness.
- Do not present paper results as performance on this workstation.
- Do not recommend a model without checking public availability and licensing.
- State what is measured geometry versus inferred or artistic geometry in every
  relevant architecture diagram.
- Include failure modes and downgrade paths, not only the ideal flow.
- Tie recommendations to the actual Kinect v2, Linux environment, and T550
  4 GiB GPU.
- Include estimates and measurement plans where exact numbers are unavailable.

The visual-system document must describe several distinct looks and how they are
constructed from reusable operations. For each look include:

- source layers;
- motion/correspondence method;
- particle lifecycle;
- force model;
- RGB/depth contribution;
- observed/inferred blend;
- post-processing;
- important controls;
- performance risk;
- likely failure mode.

The architecture-options document must seriously compare at least two credible
system architectures before recommending one.

The decision-experiments document must define the smallest approved future
spikes needed to settle uncertain choices. Each experiment needs:

- question;
- hypothesis;
- exact input and environment;
- implementation boundary;
- instrumentation;
- success/failure threshold;
- timebox;
- what decision the result unlocks.

The roadmap should be phased by validated capabilities, not arbitrary weeks:

1. capture/calibration truth;
2. deterministic replay fixture;
3. observed point and depth-surface rendering;
4. temporal/motion field;
5. persistent living particle layer;
6. recording under load;
7. tracked body coordinates;
8. inferred-body experiment;
9. animation/export research;
10. UI and performance polish.

Change this ordering if the research supports a better dependency structure.

PHASE 4 - ROOT POLISH AND INDEPENDENT CODEX REVIEW

Do not present the package to Caden immediately after writing the documents.
First make the repository root and discovery surface beautiful, then put the
entire package through an independent `gpt-5.6-sol` review.

Repository presentation gate:

- Keep the root intentional and minimal. A fresh collaborator should immediately
  understand what the product is, which prompt is active, which files are
  reference material, and what is not authorized.
- Make the root README an accurate, inviting entry point with a short vision,
  current phase, start-here order, and explicit approval/review status.
- Make `docs/discovery/README.md` a concise map of every discovery artifact,
  major decision, unresolved blocker, and recommended reading order.
- Keep prompt status clear through `prompts/README.md`; archived prompts must be
  visibly quarantined and never appear active.
- Use consistent, readable file names and headings.
- Remove or correct stale statements, duplicated routes, orphaned documents,
  broken relative links, missing images, temporary files, generated junk, and
  contradictory status language.
- Keep deep detail inside `docs/` instead of cluttering the root.
- Check Markdown structure, tables, diagrams, spelling, link targets, and
  mood-board asset integrity.
- Inspect the final tree and Git status. Do not commit unless Caden explicitly
  authorizes it, but leave a cleanly understandable uncommitted package.
- "Beautiful" here means coherent, calm, navigable, and trustworthy—not
  decorative README gimmicks, badges, or excessive formatting.

Internal consistency pass:

- Re-read every project and discovery document in the final reading order.
- Trace the recommended architecture, geometry semantics, data flow, experiment
  gates, roadmap, and open decisions across files.
- Resolve contradictions and stale assumptions before asking another model to
  review them.
- Confirm that no implementation source, scaffold, manifest, or dependency
  installation was introduced.

Independent review mechanics:

- Use the installed official `openai/codex-plugin-cc` integration from the main
  Claude/Fable session. Do not construct a raw `codex exec` command.
- The plugin inherits `gpt-5.6-sol` with reasoning effort `high` from the local
  Codex configuration. Do not override it with Spark or another model.
- Invoke `/codex:adversarial-review` with an explicit read-only focus telling
  Codex to inspect the complete repository root, PRODUCT/brief, mood board
  catalog, every `docs/discovery/` file, research sources, prompt routing, and
  the proposed approval/implementation gates.
- Make the scope self-contained. Codex sessions are fresh: give it the product
  objective, exact file list/read order, hardware constraints, non-goals, and
  the questions it must challenge.
- Require the result to identify:
  - P0/P1/P2/P3 findings;
  - unsupported or stale research claims;
  - architectural contradictions or hidden coupling;
  - weak capture, threading, memory, GPU, recording, or replay assumptions;
  - gaps in temporal correspondence and the "alive" particle design;
  - confusion between measured, tracked, inferred, and artistic geometry;
  - unrealistic mesh, body-completion, tracking, latency, or hardware claims;
  - missing licenses, public-code/weight checks, or provenance;
  - experiment gates that cannot actually decide the stated question;
  - roadmap ordering problems and unbounded scope;
  - visual-system ideas that are one-off effects rather than reusable
    operations;
  - repository-root, navigation, document-hygiene, and handoff-quality defects.
- The Codex pass is review-only. It must not edit files.
- Use `/codex:status` and `/codex:result` as needed. Confirm that the completed
  job used the intended `gpt-5.6-sol` path before accepting the review.

Review disposition:

- Record the review scope, model, findings, and Fable's disposition in
  `docs/discovery/11-codex-review.md`.
- Fix every valid P0 and P1 finding.
- Fix valid P2 findings unless a documented product tradeoff justifies
  deferring one.
- For rejected or deferred findings, write a concrete rationale and consequence;
  do not dismiss them with preference alone.
- Update all affected discovery documents, the index, roadmap, open decisions,
  and root routing together.

Focused re-review:

- After revisions, invoke `/codex:review` or another focused
  `/codex:adversarial-review` in read-only mode. Give Codex the original
  findings, dispositions, and changed files.
- Require Codex to verify that blocking findings are actually resolved and that
  the revisions did not introduce contradictions.
- Record the re-review verdict in `docs/discovery/11-codex-review.md`.
- Do not proceed to Caden's approval gate while Codex still reports a valid P0
  or P1 planning defect.
- If the official plugin is unavailable, the job runs with the wrong model, the
  reviewer cannot see the complete uncommitted planning package, or the review
  fails repeatedly, stop and tell Caden. Do not silently substitute a weaker
  reviewer or claim the gate passed.

PHASE 5 - FINAL APPROVAL GATE

Only after the root-polish gate, independent Codex review, revision pass, and
focused re-review are complete:

1. Give Caden a concise synthesis of the recommended architecture.
2. Show the most important alternatives rejected and why.
3. Present the proposed observed/tracked/inferred/artistic geometry model.
4. Present the recommended first two or three decision experiments.
5. Summarize the Codex findings, what changed, and any accepted residual risk.
6. Show the final repository/discovery reading order.
7. Identify the remaining choices only Caden can make.
8. Ask Caden to approve, revise, or reject the plan.
9. Stop. Do not implement.

Only after a later explicit approval should a new implementation prompt be
written. That later prompt should be derived from the approved discovery docs,
not from `prompts/archive/FABLE_IMPLEMENTATION_GOAL_DRAFT.md`.

DEFINITION OF DONE

This discovery goal is complete only when:

- Caden has participated in at least one focused interview round;
- the mood board has been translated into reusable visual behaviors rather than
  copied aesthetics;
- the point engine and temporal "alive" behavior have been deeply reasoned;
- RGB-D fusion roles are explicit;
- front-only capture, inferred backside, depth surface, parametric proxy, and
  offline reconstruction have been compared honestly;
- current mesh/tracking/reconstruction tools have been verified for
  availability, license, Linux support, hardware needs, and real-time relevance;
- at least two credible system architectures have been compared;
- a recommended architecture and semantic geometry model are documented;
- uncertainties are converted into bounded decision experiments;
- the recording/replay and future animation paths are represented;
- the phased roadmap has clear gates and non-goals;
- the root and discovery index are clean, navigable, internally consistent, and
  free of stale or orphaned planning artifacts;
- the complete package received a read-only `gpt-5.6-sol` adversarial review;
- all valid P0/P1 findings and appropriate P2 findings were resolved or
  explicitly dispositioned;
- a focused Codex re-review verified the blocking findings are closed;
- `docs/discovery/11-codex-review.md` records the review and re-review;
- Caden is shown the plan and asked for explicit approval;
- no implementation code has been written.
```
