#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 noire342
"""Fit new luminous inserts to the eight slots of the extracted Hoven drill."""
import argparse
import math
import struct
import zlib
from pathlib import Path

from build_model_pack import accessor_values, read_glb, write_glb


SLOT_CENTERS = (5.05, 13.1, 21.15, 29.5)
HALF_WIDTHS = (1.15, 1.38, 1.20, 1.40)
TOP, BOTTOM = 106.7, 117.3


def clip(vertices, axis, boundary, direction):
    if not vertices:
        return []
    result = []
    previous = vertices[-1]
    for current in vertices:
        a = (previous[axis] - boundary) * direction
        b = (current[axis] - boundary) * direction
        if (a < 0) != (b < 0):
            weight = a / (a - b)
            result.append(tuple(p + (q - p) * weight for p, q in zip(previous, current)))
        if b >= 0:
            result.append(current)
        previous = current
    return result


def normal(a, b, c):
    u = tuple(y - x for x, y in zip(a, b))
    v = tuple(y - x for x, y in zip(a, c))
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    return tuple(x / math.hypot(*n) for x in n)


def fitted_slots(source, scale):
    document, raw = read_glb(source)
    primitive, = document["meshes"][0]["primitives"]
    attributes = primitive["attributes"]
    positions = accessor_values(document, raw, attributes["POSITION"])
    uvs = accessor_values(document, raw, attributes["TEXCOORD_0"])
    # These are the two faces of the vent band between its gold trim.
    # Pin the atlas boundary so a different source mesh cannot get a loose glow.
    indices = [x[0] for x in accessor_values(document, raw, primitive["indices"])]
    band = indices[1116 * 3:1138 * 3]
    if len(band) != 66 or set(band) != set(range(1029, 1048)):
        raise ValueError("Unexpected Hoven vent band topology")

    points, normals, texture = [], [], []
    for middle, half_width in zip(SLOT_CENTERS, HALF_WIDTHS):
        left, right = middle - half_width, middle + half_width
        sides = set()
        for a, b, c in zip(band[::3], band[1::3], band[2::3]):
            facing = normal(positions[a], positions[b], positions[c])
            vertices = [(uvs[i][0] * 128, uvs[i][1] * 128, *positions[i]) for i in (a, b, c)]
            for axis, edge, direction in ((0, left, 1), (0, right, -1), (1, TOP, 1), (1, BOTTOM, -1)):
                vertices = clip(vertices, axis, edge, direction)
            if len(vertices) < 3:
                continue
            sides.add(facing[0] > 0)
            for i in range(1, len(vertices) - 1):
                for u, v, *point in (vertices[0], vertices[i], vertices[i + 1]):
                    # Slightly above the band to prevent z-fighting at grazing angles.
                    points.append(tuple(x * scale + n * .0015 for x, n in zip(point, facing)))
                    normals.append(facing)
                    texture.append(((u - left) / (right - left), (v - TOP) / (BOTTOM - TOP)))
        if len(sides) != 2:
            raise ValueError("A vent slot is missing from one side of the housing")
    return points, normals, texture


def glow_mask():
    width, height = 32, 64
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            u, v = abs((2 * x + 1) / width - 1), abs((2 * y + 1) / height - 1)
            # Rounded, soft edge with a dark lip, and a wider hot center.
            edge = min(1, max(0, (1 - (u**4 + v**6)**.25) / .40))
            alpha = round(255 * edge * edge * (3 - 2 * edge))
            raw.extend((255, 255, 255, alpha))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    header = struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="moby_164.glb from Hoven")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    points, normals, texture = fitted_slots(args.source, .08294187486171722)
    payload = bytearray()
    views, accessors = [], []
    for vectors, fmt, kind in ((points, "<3f", "VEC3"), (normals, "<3f", "VEC3"), (texture, "<2f", "VEC2")):
        start = len(payload)
        payload.extend(b"".join(struct.pack(fmt, *vector) for vector in vectors))
        index = len(views)
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(payload) - start})
        accessors.append({"bufferView": index, "componentType": 5126, "count": len(vectors), "type": kind})
    accessors[0].update(min=list(map(min, zip(*points))), max=list(map(max, zip(*points))))
    model = {
        "asset": {"version": "2.0"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "meshes": [{"name": "Heat in the vent slots", "primitives": [
            {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "material": 0}
        ]}],
        "images": [{"uri": "mask.png"}],
        "samplers": [{"wrapS": 33071, "wrapT": 33071}],
        "textures": [{"sampler": 0, "source": 0}],
        "materials": [{"alphaMode": "BLEND", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "bufferViews": views, "accessors": accessors,
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "slots.glb").write_bytes(write_glb(model, payload))
    (args.output / "mask.png").write_bytes(glow_mask())


if __name__ == "__main__":
    main()
