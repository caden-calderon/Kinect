#!/usr/bin/env python3
"""MediaPipe adapter for Kinect Studio's bounded pose protocol.

stdin/stdout are a binary transport owned by the C++ PoseProvider. Diagnostics
must go to stderr so a log message can never corrupt the protocol stream.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path
from typing import BinaryIO

import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision


REQUEST_MAGIC = int.from_bytes(b"KPRQ", "little")
RESULT_MAGIC = int.from_bytes(b"KPRS", "little")
PROTOCOL_VERSION = 1
JOINT_COUNT = 33
MAXIMUM_JPEG_BYTES = 16 * 1024 * 1024
REQUEST = struct.Struct("<IIQQQQQ")
RESULT = struct.Struct("<IIQQQQQfIII")
LANDMARK = struct.Struct("<ffffffff")


def read_exact(stream: BinaryIO, size: int) -> bytes | None:
    data = bytearray(size)
    view = memoryview(data)
    offset = 0
    while offset < size:
        chunk = stream.readinto(view[offset:])
        if chunk is None:
            continue
        if chunk == 0:
            if offset == 0:
                return None
            raise EOFError(f"request ended after {offset} of {size} bytes")
        offset += chunk
    return bytes(data)


def empty_landmarks() -> list[tuple[float, ...]]:
    return [(0.0,) * 8 for _ in range(JOINT_COUNT)]


def adapt_landmarks(result: vision.PoseLandmarkerResult) -> tuple[bool, list[tuple[float, ...]]]:
    if not result.pose_landmarks or not result.pose_world_landmarks:
        return False, empty_landmarks()
    image_landmarks = result.pose_landmarks[0]
    world_landmarks = result.pose_world_landmarks[0]
    if len(image_landmarks) != JOINT_COUNT or len(world_landmarks) != JOINT_COUNT:
        raise RuntimeError(
            f"model returned {len(image_landmarks)} image and "
            f"{len(world_landmarks)} world landmarks"
        )

    adapted: list[tuple[float, ...]] = []
    for image, world in zip(image_landmarks, world_landmarks, strict=True):
        visibility = min(max(float(image.visibility or 0.0), 0.0), 1.0)
        presence = min(max(float(image.presence or 0.0), 0.0), 1.0)
        adapted.append(
            (
                float(image.x),
                float(image.y),
                float(image.z),
                float(world.x),
                float(world.y),
                float(world.z),
                visibility,
                presence,
            )
        )
    return True, adapted


def run(model_path: Path) -> None:
    options = vision.PoseLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=str(model_path)),
        running_mode=vision.RunningMode.VIDEO,
        num_poses=1,
    )
    input_stream = sys.stdin.buffer
    output_stream = sys.stdout.buffer
    last_timestamp_ms = -1

    with vision.PoseLandmarker.create_from_options(options) as landmarker:
        while True:
            request_bytes = read_exact(input_stream, REQUEST.size)
            if request_bytes is None:
                return
            (
                magic,
                version,
                frame_id,
                depth_seq,
                color_seq,
                capture_ns,
                payload_bytes,
            ) = REQUEST.unpack(request_bytes)
            if magic != REQUEST_MAGIC or version != PROTOCOL_VERSION:
                raise RuntimeError("unsupported pose request protocol")
            if payload_bytes <= 0 or payload_bytes > MAXIMUM_JPEG_BYTES:
                raise RuntimeError(f"JPEG payload is outside bounds: {payload_bytes}")

            jpeg = read_exact(input_stream, payload_bytes)
            if jpeg is None:
                raise EOFError("request ended before its JPEG payload")
            bgr = cv2.imdecode(np.frombuffer(jpeg, dtype=np.uint8), cv2.IMREAD_COLOR)
            if bgr is None:
                raise RuntimeError(f"frame {frame_id} contains an invalid JPEG")
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
            image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

            # Replay seeks and loops can move source timestamps backwards. The
            # model's temporal API only needs a monotonic call timestamp; the
            # original source timestamp remains untouched in the wire result.
            timestamp_ms = max(last_timestamp_ms + 1, capture_ns // 1_000_000)
            last_timestamp_ms = timestamp_ms
            inference_started = time.perf_counter_ns()
            pose_result = landmarker.detect_for_video(image, timestamp_ms)
            inference_ms = (time.perf_counter_ns() - inference_started) * 1e-6
            detected, landmarks = adapt_landmarks(pose_result)
            result_ns = time.monotonic_ns()

            output_stream.write(
                RESULT.pack(
                    RESULT_MAGIC,
                    PROTOCOL_VERSION,
                    frame_id,
                    depth_seq,
                    color_seq,
                    capture_ns,
                    result_ns,
                    inference_ms,
                    int(detected),
                    JOINT_COUNT,
                    0,
                )
            )
            output_stream.write(b"".join(LANDMARK.pack(*joint) for joint in landmarks))
            output_stream.flush()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    args = parser.parse_args()
    if not args.model.is_file():
        parser.error(f"pose model not found: {args.model}")
    run(args.model)


if __name__ == "__main__":
    try:
        main()
    except (EOFError, OSError, RuntimeError, ValueError) as error:
        print(f"pose provider failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
