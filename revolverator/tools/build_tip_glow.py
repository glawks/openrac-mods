#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 noire342
"""Fit four additive bands to the Hoven drill head, from the point to the housing."""
import argparse
import math
import struct
import zlib
from collections import defaultdict
from pathlib import Path

from build_model_pack import accessor_values, read_glb, write_glb
from build_vent_glow import clip, normal


CLASS164_METRES = .08294187486171722
HEAD_JOINT = 35
SURFACE_OFFSET = .0015
BAND_EDGES = (0, .23, .48, .73, 1)
BAND_COUNT = len(BAND_EDGES) - 1
BLEND_WIDTH = .075
SAMPLES = 128


def head_geometry(source):
    document, raw = read_glb(source)
    primitive, = document["meshes"][0]["primitives"]
    attributes = primitive["attributes"]
    points, joints, weights = (
        accessor_values(document, raw, attributes[name])
        for name in ("POSITION", "JOINTS_0", "WEIGHTS_0")
    )
    head = set()
    for index, (joint_row, weight_row) in enumerate(zip(joints, weights)):
        influences = {joint for joint, weight in zip(joint_row, weight_row) if weight > 0}
        if HEAD_JOINT in influences:
            if influences != {HEAD_JOINT} or points[index][1] > -4.70:
                raise ValueError("Expected the drill head to be rigid and outside the housing")
            head.add(index)

    faces = []
    indices = [index for index, in accessor_values(document, raw, primitive["indices"])]
    for triangle in zip(indices[::3], indices[1::3], indices[2::3], strict=True):
        if head.issuperset(triangle):
            faces.append(tuple(points[index] for index in triangle))
        elif head.intersection(triangle):
            raise ValueError("A triangle connects the drill head and housing")
    if len(head) != 208 or len(faces) != 202:
        raise ValueError("Unexpected Hoven drill head topology")
    return faces


def glow_surface(faces):
    # Use the same offset wherever faces meet, including split UV seams and hard edges.
    directions = defaultdict(list)
    for face in faces:
        facing = normal(*face)
        for point in face:
            directions[point].append(facing)
    tip = min(point[1] for point in directions)
    base = max(point[1] for point in directions)
    vertices = {}
    for point, normals in directions.items():
        direction = tuple(map(sum, zip(*normals)))
        scale = SURFACE_OFFSET / math.hypot(*direction)
        offset = tuple(x * CLASS164_METRES + n * scale for x, n in zip(point, direction))
        vertices[point] = ((point[1] - tip) / (base - tip), *offset)
    return [[vertices[point] for point in face] for face in faces]


def band_limits(band):
    start, end = BAND_EDGES[band:band + 2]
    return max(0, start - BLEND_WIDTH), min(1, end + BLEND_WIDTH)


def fitted_band(surface, band):
    start, end = band_limits(band)
    points, normals, texture = [], [], []
    for face in surface:
        vertices = clip(clip(face, 0, start, 1), 0, end, -1)
        # Clipped vertices can coincide with an original edge. Remove them before triangulation.
        vertices = list(dict.fromkeys(vertices))
        if len(vertices) < 3:
            continue
        facing = normal(*(vertex[1:] for vertex in face))
        for i in range(1, len(vertices) - 1):
            for axial, *point in (vertices[0], vertices[i], vertices[i + 1]):
                texel = (axial - start) / (end - start) * (SAMPLES - 1)
                u = (band * SAMPLES + .5 + texel) / (BAND_COUNT * SAMPLES)
                points.append(point)
                normals.append(facing)
                texture.append((u, .5))
    if not points:
        raise ValueError(f"No triangles in drill heat band {band + 1}")
    return points, normals, texture


def smoothstep(start, end, value):
    t = min(1, max(0, (value - start) / (end - start)))
    return t * t * (3 - 2 * t)


def band_alpha(band, position):
    begin, end = BAND_EDGES[band:band + 2]
    # Neighbouring windows have complementary opacities across the same interval.
    leading = smoothstep(begin - BLEND_WIDTH, begin + BLEND_WIDTH, position) if band else 1
    if band == BAND_COUNT - 1:
        trailing = 1 - smoothstep(1 - BLEND_WIDTH, 1, position)
    else:
        trailing = 1 - smoothstep(end - BLEND_WIDTH, end + BLEND_WIDTH, position)
    return leading * trailing


def glow_mask():
    # Four horizontal profiles, with UVs clamped to the sample centres of their own band.
    pixels = bytearray()
    for band in range(BAND_COUNT):
        start, end = band_limits(band)
        for sample in range(SAMPLES):
            alpha = band_alpha(band, start + (end - start) * sample / (SAMPLES - 1))
            pixels.extend((255, 255, 255, round(255 * alpha)))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    header = struct.pack(">2I5B", BAND_COUNT * SAMPLES, 1, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"\0" + pixels)) + chunk(b"IEND", b"")


def band_model(band, points, normals, texture):
    payload = bytearray()
    views, accessors = [], []
    for vectors, fmt, kind in ((points, "<3f", "VEC3"), (normals, "<3f", "VEC3"), (texture, "<2f", "VEC2")):
        start = len(payload)
        payload.extend(b"".join(struct.pack(fmt, *vector) for vector in vectors))
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(payload) - start})
        accessors.append({"bufferView": len(views) - 1, "componentType": 5126, "count": len(vectors), "type": kind})
    packed_positions = list(struct.iter_unpack("<3f", payload[:views[0]["byteLength"]]))
    accessors[0].update(min=list(map(min, zip(*packed_positions))), max=list(map(max, zip(*packed_positions))))
    model = {
        "asset": {"version": "2.0"}, "scene": 0,
        "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "meshes": [{"name": f"Drill heat band {band + 1}", "primitives": [
            {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "material": 0}
        ]}],
        "images": [{"uri": "mask.png"}],
        "samplers": [{"wrapS": 33071, "wrapT": 33071}],
        "textures": [{"sampler": 0, "source": 0}],
        "materials": [{"alphaMode": "BLEND", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "bufferViews": views, "accessors": accessors,
    }
    return write_glb(model, payload)


def build_tip_glow(source: Path, output: Path):
    surface = glow_surface(head_geometry(source))
    bands = [band_model(band, *fitted_band(surface, band)) for band in range(BAND_COUNT)]
    output.mkdir(parents=True, exist_ok=True)
    for band, model in enumerate(bands, 1):
        (output / f"band-{band}.glb").write_bytes(model)
    (output / "mask.png").write_bytes(glow_mask())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="moby_164.glb from Hoven")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build_tip_glow(args.source, args.output)


if __name__ == "__main__":
    main()
