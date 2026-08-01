# Decision Experiments — Results Index

Specs: [../discovery/07-decision-experiments.md](../discovery/07-decision-experiments.md).
All run 2026-07-29 (implementation session 1) unless noted. Test durations
were deliberately shortened from the specs' timeboxes per Caden's
direction (takes are ≤20 s animation clips; long soaks buy nothing).

| Exp | Question | Verdict |
| --- | --- | --- |
| [E1](E1.md) | capture truth: 30 Hz RGB-D + tee + timing semantics | **PASS — closed 2026-07-30** (30 Hz depth, 0 drops, tee bit-verified, skew constant −10.875 ms; 15 Hz color = light starvation, and `setColorSemiAutoExposure(16 ms)` restores 30 Hz on demand — image usability then depends on scene lighting) |
| [E2](E2.md) | software flow within budget | **PASS — closed 2026-07-30**: DIS FAST is the flow term (3.4 ms p50, body FB-coverage 0.77 during fast swings, direction verified on overlays); ULTRAFAST demoted to preview/degradation tier (0.63) |
| [E3](E3.md) | MCAP under take load | **PASS** (5–6× real-time, seek p95 20 ms, kill-loss <1 s) + binding finding: stock FileWriter **aborts** on disk-full → recorder uses its own guarded writer |
| [E4](E4.md) | combined-load render budget | **PASS** (217k pts + 250–500k particles + bloom @60 Hz with 30–43% headroom; 1M reachable at fp≤2; VRAM 3.6 GiB free; shedding ladder demonstrated) — **Option N stack fully gated-in with E1** |
| [E5](E5.md) | live tracker reality | **PASS — closed 2026-07-30** (0 % swing dropout, sub-second reacquisition, graceful seated truncation) with two engineering conditions for the tracking layer: One Euro on end effectors (wrist σ 32 px raw), detection hysteresis at frame edges |
| E6 | capsules vs parametric proxy | **NOT RUN** — requires E5's stability pass + Caden judging side-by-side (later session; harness direction in discovery 07) |
| [E7](E7.md) | deterministic replay | **PASS** — pixel-bit-identical across process restarts (SSIM ≡ 1.0); seek converges in K = one particle lifecycle (147 fr) after fixing a real design flaw → phase-5 rule: **lifecycle anchored to absolute frame index** |

## The with-Caden checklist (everything blocked on a human)

1. ~~Lights on E1 re-run~~ **done 2026-07-30** — verdict in E1.md (15 Hz was light starvation; semi-auto exposure restores 30 Hz on demand).
2. ~~Arm-swing clip~~ **done 2026-07-30** — E2 closed (verdict in E2.md); the clip `out-clip-body` is also the phase-4 exit-test input.
3. ~~In-frame E5 stability~~ **done 2026-07-30** — E5 closed with two engineering conditions (E5.md).
4. **Judge the two looks** against the mood board: `./build/src/kstudio` (live) or `--take <take>`; presets `luminous-shell` / `dense-veil` buttons; Tab = clean output. This is the phase-3 exit gate.
5. Optional: `sudo pacman -S cuda` → rebuild fork with `-DENABLE_CUDA=ON` (OpenCL depth measured fine; CUDA is an optimization, not a blocker).
