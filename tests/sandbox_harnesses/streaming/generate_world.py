#!/usr/bin/env python3
"""Generate the Gate 6 streaming fixture. Usage: python generate_world.py [project_dir]."""

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

CELL = 250
GRID = 64
TIERS = ("proxy", "mid", "high")
FACADE_SIZE = {"proxy": 64, "mid": 256, "high": 512}
COLORS = {
    "Field": (0.18, 0.33, 0.15),
    "Paving": (0.33, 0.34, 0.34),
    "Road": (0.11, 0.12, 0.14),
    "Stripe": (0.83, 0.83, 0.81),
    "Probe": (1, 1, 0),
    "RoofProbe": (1, 0, 0),
    "Wall": (0.72, 0.73, 0.76),
    "Metal": (0.34, 0.43, 0.48),
    "Grove": (0.12, 0.27, 0.14),
}


@dataclass(frozen=True)
class Box:
    name: str
    material: str
    center: tuple
    size: tuple


def write_asset(path, data):
    # An interrupted generation can resume. Never truncate existing or hand-edited assets.
    try:
        out = path.open("xb")
    except FileExistsError:
        if path.read_bytes() != data:
            raise ValueError(f"Existing asset differs at {path}. Generate into a fresh project directory.") from None
        return
    with out:
        out.write(data)


def write_text(path, content):
    write_asset(path, content.encode("utf-8"))


def facade_texture(path, size, seed):
    """Standard uncompressed RGBA8 DDS. Each level has its own texture RID at runtime."""
    # DDS_HEADER with DDSD_PITCH, DDPF_RGB | DDPF_ALPHAPIXELS and DDSCAPS_TEXTURE.
    header = [124, 0x100F, size, size, size * 4, 0, 0, *([0] * 11)]
    header += [32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000, 0x1000, 0, 0, 0, 0]
    masonry = bytes((137 + seed % 68, 137 + seed % 73, 145 + seed % 51, 255))
    glazing = bytes((63 + seed % 31, 100 + seed % 52, 126 + seed % 55, 255))
    period = size // 8
    window_row = (masonry * (period // 4) + glazing * (period // 2) + masonry * (period // 4)) * 8
    course = masonry * size
    rows = [window_row if period // 4 <= y % period < period * 3 // 4 else course for y in range(size)]
    write_asset(path, b"DDS " + struct.pack("<31I", *header) + b"".join(rows))


def block_geometry(seed, dense, road_side):
    # The streaming server anchors scenes at the minimum x,z corner of each cell.
    terrain = Box("Terrain", "Paving" if dense else "Field", (125, -0.5, 125), (CELL, 1, CELL))
    ground = [terrain]
    if road_side:
        z = 5 if road_side == "south" else 245
        ground.append(Box("Highway", "Road", (125, 0.015, z), (CELL, 0.03, 10)))
    if road_side == "south":
        ground.append(Box("PavementProbe", "Probe", (125, 0.07, 0), (12, 0.02, 10)))

    buildings = []
    details = []
    if dense:
        # Two local streets separate four blocks in each town tile.
        ground.append(Box("LocalStreet", "Road", (125, 0.012, 125), (CELL, 0.024, 9)))
        ground.append(Box("CrossStreet", "Road", (83, 0.014, 125), (8, 0.028, CELL)))
        middle_x = 125 if road_side == "south" else 126
        for column, x in enumerate((30, middle_x, 215)):
            for row, z in enumerate((35, 86, 166, 216)):
                index = column * 4 + row
                height = 7 + (seed * 11 + index * 7) % 27
                width = 18 + (seed + index * 9) % 24
                depth = 16 + (seed * 3 + index * 13) % 17
                if road_side == "south" and (column, row) == (1, 0):
                    height, width, depth = 18, 20, 20
                    ground.append(Box("RoofProbe", "RoofProbe", (125, 18, 35), (20, 0.05, 20)))
                else:
                    details.append(Box(f"Plant{index}", "Metal", (x, height + 0.8, z), (5, 1.6, 4)))
                buildings.append(Box(f"Building{index}", "Wall", (x, height / 2, z), (width, height, depth)))
    else:
        for i, (x, z) in enumerate(((44, 56), (58, 65), (180, 160), (191, 172))):
            buildings.append(Box(f"Windbreak{i}", "Grove", (x, 3, z), (11, 6, 9)))
        details.append(Box("FarmShed", "Metal", (54, 2, 81), (12, 4, 8)))
    if road_side == "south":
        for i in range(8):
            if i != 4:
                details.append(Box(f"LaneStripe{i}", "Stripe", (10 + 31 * i, 0.045, 0), (8, 0.02, 0.18)))
    return ground, buildings, details


def batch_scene(path, boxes, *, atlas=None, merge=False):
    """Merge proxy buildings into one MultiMesh draw. Keep the measured markings addressable."""
    lines = ['[gd_scene format=3]', '', '[ext_resource type="Shader" path="res://hlod.gdshader" id="shader"]']
    if atlas:
        lines.append(f'[ext_resource type="Texture2D" path="{atlas}" id="atlas"]')
    materials = sorted({box.material for box in boxes})
    for material in materials:
        rgb = ", ".join(map(str, COLORS[material]))
        lines += [
            "",
            f'[sub_resource type="ShaderMaterial" id="mat_{material}"]',
            'shader = ExtResource("shader")',
            f'shader_parameter/albedo = Color({rgb}, 1)',
        ]
        if material == "Wall" and atlas:
            lines.append('shader_parameter/albedo_texture = ExtResource("atlas")')
        if material in ("Probe", "RoofProbe"):
            lines.append("shader_parameter/unlit = true")
        lines += [
            "",
            f'[sub_resource type="BoxMesh" id="mesh_{material}"]',
            f'material = SubResource("mat_{material}")',
            'size = Vector3(1, 1, 1)',
        ]

    nodes = ['', '[node name="Batch" type="Node3D"]']
    for material in materials:
        group = [box for box in boxes if box.material == material]
        if merge and len(group) > 1:
            transforms = []
            for box in group:
                x, y, z = box.center
                w, h, d = box.size
                transforms.extend((w, 0, 0, x, 0, h, 0, y, 0, 0, d, z))
            lines += [
                "",
                f'[sub_resource type="MultiMesh" id="merged_{material}"]',
                "transform_format = 1",
                f'mesh = SubResource("mesh_{material}")',
                f"instance_count = {len(group)}",
                f"buffer = PackedFloat32Array({', '.join(map(str, transforms))})",
            ]
            nodes += [
                "",
                f'[node name="{material}" type="MultiMeshInstance3D" parent="."]',
                f'multimesh = SubResource("merged_{material}")',
            ]
        else:
            for box in group:
                groups = ' groups=["gate6_probe"]' if box.material == "Probe" else ""
                nodes += [
                    "",
                    f'[node name="{box.name}" type="MeshInstance3D" parent="."{groups}]',
                    f"position = Vector3{box.center}",
                    f"scale = Vector3{box.size}",
                    f'mesh = SubResource("mesh_{material}")',
                ]
    write_text(path, "\n".join(lines + nodes) + "\n")


def collision_scene(path, boxes):
    # Shape dimensions are in meters. Jolt sees unit transforms on all collision shapes.
    lines = ["[gd_scene format=3]"]
    nodes = ['', '[node name="Solid" type="StaticBody3D"]']
    for box in boxes:
        lines += ["", f'[sub_resource type="BoxShape3D" id="{box.name}"]', f"size = Vector3{box.size}"]
        nodes += [
            "",
            f'[node name="{box.name}" type="CollisionShape3D" parent="."]',
            f"position = Vector3{box.center}",
            f'shape = SubResource("{box.name}")',
        ]
    write_text(path, "\n".join(lines + nodes) + "\n")


def make_block(directory, seed, dense, road_side):
    directory.mkdir(exist_ok=True)
    ground, buildings, details = block_geometry(seed, dense, road_side)
    collision_scene(directory / "collision.tscn", [ground[0], *buildings])
    prefix = f"res://world/{directory.name}/"
    for tier in TIERS:
        atlas = None
        if dense:
            texture = f"{tier}.dds"
            facade_texture(directory / texture, FACADE_SIZE[tier], seed)
            atlas = prefix + texture
        # Bound an atomic SceneTree attach to at most six objects in the detailed tiers.
        if tier == "proxy":
            batch_scene(directory / "proxy.tscn", ground + buildings, atlas=atlas, merge=True)
            continue
        boxes = ground + buildings
        if tier == "high":
            boxes += details
        references = []
        batches = []
        for offset in range(0, len(boxes), 6):
            filename = f"{tier}_{offset // 6}.tscn"
            batch_scene(directory / filename, boxes[offset : offset + 6], atlas=atlas)
            references.append(f'[ext_resource type="PackedScene" path="{prefix}{filename}" id="b{offset}"]')
            batches.append(f'ExtResource("b{offset}")')
        write_text(
            directory / f"{tier}.tres",
            '[gd_resource type="CellSceneBundle" format=3]\n\n'
            + "\n".join(references)
            + f"\n\n[resource]\nbatches = Array[PackedScene]([{', '.join(batches)}])\n",
        )


def generate_world(world):
    world.mkdir(exist_ok=True)
    lines = [
        '[gd_resource type="WorldPartitionManifest" format=3]',
        "",
        '[sub_resource type="WorldPartitionLayer" id="district"]',
        'layer_id = &"district"',
        f"cell_size = {CELL}.0",
        "load_radius = 450.0",
        "evict_radius = 650.0",
    ]
    cell_refs = []
    layouts = set()
    for z in range(GRID):
        for x in range(GRID):
            dense = 16 <= x < 46 and 30 <= z < 34
            side = {31: "north", 32: "south"}.get(z, "")
            tile = f"town_{x}_{z}" if dense else "rural_" + (side or "field")
            if tile not in layouts:
                make_block(world / tile, x + z * GRID if dense else 0, dense, side)
                layouts.add(tile)
            cell_id = f"cell_{x}_{z}"
            prefix = f"res://world/{tile}/"
            lines += [
                "",
                f'[sub_resource type="WorldPartitionCell" id="{cell_id}"]',
                f'cell_id = &"{cell_id}"',
                'layer = &"district"',
                f"grid_coords = Vector2i({x}, {z})",
                f'hlod_impostor_path = "{prefix}proxy.tscn"',
                f'mid_mesh_path = "{prefix}mid.tres"',
                f'high_scene_path = "{prefix}high.tres"',
                f'collision_bundle_path = "{prefix}collision.tscn"',
            ]
            cell_refs.append(f'SubResource("{cell_id}")')
    lines += [
        "",
        "[resource]",
        'layers = Array[WorldPartitionLayer]([SubResource("district")])',
        f"cells = Array[WorldPartitionCell]([{', '.join(cell_refs)}])",
    ]
    write_text(world / "manifest.tres", "\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project_dir", nargs="?", type=Path, default=Path(__file__).resolve().parent)
    project = parser.parse_args().project_dir.resolve()
    if not project.is_dir():
        parser.error(f"Project directory does not exist: {project}")
    world = project / "world"
    try:
        generate_world(world)
    except ValueError as err:
        parser.error(str(err))
    print(f"{world / 'manifest.tres'}: {GRID**2} cells, 120 textured town blocks, 3 shared rural layouts.")


if __name__ == "__main__":
    main()
