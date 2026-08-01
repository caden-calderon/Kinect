#!/usr/bin/env python3
"""E1 IR-at-silhouette analysis (grounds discovery 04 §1's [verify]).

Question: does IR intensity work as a confidence proxy that isolates
depth-discontinuity/flying-pixel samples, rather than merely correlating
with edges?

Method, per depth+IR snapshot pair from the probe:
  - valid mask: depth > 0
  - discontinuity mask: local depth range (3x3 valid neighbors) > 100 mm
  - interior mask: valid, not discontinuity
  - report IR intensity distributions for both populations + separability
"""
from pathlib import Path
import re
import struct
import sys


def read_pgm16(path: Path) -> tuple[int, int, list[int]]:
    data = path.read_bytes()
    m = re.match(rb"P5\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m:
        raise ValueError(f"not a P5 PGM: {path}")
    w, h, maxval = int(m.group(1)), int(m.group(2)), int(m.group(3))
    assert maxval == 65535
    px = data[m.end():]
    vals = list(struct.unpack(f">{w*h}H", px[: w * h * 2]))
    return w, h, vals


def pctile(sorted_vals: list[int], q: float) -> float:
    if not sorted_vals:
        return float("nan")
    return sorted_vals[min(len(sorted_vals) - 1, int(q * len(sorted_vals)))]


def analyze(depth_path: Path, ir_path: Path) -> None:
    w, h, depth = read_pgm16(depth_path)
    _, _, ir = read_pgm16(ir_path)

    edge_ir, interior_ir, invalid_ir = [], [], []
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            i = y * w + x
            d = depth[i]
            if d == 0:
                invalid_ir.append(ir[i])
                continue
            neigh = [depth[(y + dy) * w + x + dx]
                     for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                     if depth[(y + dy) * w + x + dx] > 0]
            # depth snapshots are in mm (float mm clamped to u16)
            if max(neigh) - min(neigh) > 100:
                edge_ir.append(ir[i])
            else:
                interior_ir.append(ir[i])

    edge_ir.sort()
    interior_ir.sort()
    invalid_ir.sort()

    def row(name: str, v: list[int]) -> str:
        return (f"  {name:9s} n={len(v):7d}  p25={pctile(v, .25):7.0f}  "
                f"p50={pctile(v, .50):7.0f}  p75={pctile(v, .75):7.0f}")

    print(f"{depth_path.name} + {ir_path.name}:")
    print(row("edge", edge_ir))
    print(row("interior", interior_ir))
    print(row("invalid", invalid_ir))
    if edge_ir and interior_ir:
        # separability: fraction of edge pixels below the interior 25th pctile
        thr = pctile(interior_ir, 0.25)
        frac = sum(1 for v in edge_ir if v < thr) / len(edge_ir)
        print(f"  edge pixels below interior-p25 IR: {frac:.1%} "
              f"(>50% would support 'IR dims at discontinuities')")


def main() -> None:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "out-soak-dim")
    depths = sorted(out.glob("depth_*.pgm"))
    irs = sorted(out.glob("ir_*.pgm"))
    if not depths or not irs:
        sys.exit(f"no snapshots in {out}")
    for d, i in zip(depths, irs):
        analyze(d, i)


if __name__ == "__main__":
    main()
