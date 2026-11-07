# Copyright (c) 2026 nefariousjosiah. MIT License.
"""Converts the downloaded F-35 Blender model into animation-ready parts for Unreal.

Run headless (scripts inside the .blend are never executed):

  blender -b --disable-autoexec --factory-startup <model>.blend --python Tools/Blender/export_f35_parts.py -- <out_dir>

What it does:
  1. Scales the model to the F-35C's published 15.7 m length and moves the origin to
     the centre of gravity used by the launch physics.
  2. Removes parts that are wrong for a carrier launch: the F-35B lift fan and the
     deployed boarding ladder; closes the doors that are open in the source pose.
  3. Joins objects into rigid groups by what moves together (body, each control
     surface, each gear leg's fixed and sliding halves, each wheel).
  4. Bakes every vertex into the centre-of-gravity frame, so each group imports in
     place, and writes the hinge pivots and axes to rig.json.
  5. Exports one FBX for Unreal.

The source model and the export are third-party content: both stay in git-ignored
folders. Only this script is committed.
"""

import json
import math
import os
import sys

import bpy
import mathutils

TARGET_LENGTH_M = 15.7          # F-35C, published
GEAR_RATIO_NOSE = 0.12          # share of weight on the nose gear; sets the CG along the body

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT_DIR = os.path.abspath(argv[0] if argv else "ThirdPartySource/F35B/export")
os.makedirs(OUT_DIR, exist_ok=True)

objects = bpy.data.objects

# --- 1. Clean up the source pose -------------------------------------------------------
for name in ("fan", "ladder0", "ladder1", "ladder2", "ladder3"):
    if name in objects:
        bpy.data.objects.remove(objects[name], do_unlink=True)
for name in ("ladderDoor",):
    objects[name].rotation_euler.x = 0.0
for name in ("leftEngineDoor", "rightEngineDoor"):
    objects[name].rotation_euler.x = 0.0
bpy.context.view_layer.update()

# --- 2. Scale and centre-of-gravity frame ----------------------------------------------
meshes = [o for o in objects if o.type == "MESH"]
points = [o.matrix_world @ mathutils.Vector(c) for o in meshes for c in o.bound_box]
length = max(p.x for p in points) - min(p.x for p in points)
scale = TARGET_LENGTH_M / length

nose_axle = objects["noseWheel"].matrix_world.to_translation() * scale
main_axle = objects["leftWheel"].matrix_world.to_translation() * scale
cg_x = main_axle.x + GEAR_RATIO_NOSE * (nose_axle.x - main_axle.x)
cg = mathutils.Vector((cg_x, 0.0, 0.0))
to_cg = mathutils.Matrix.Translation(-cg) @ mathutils.Matrix.Scale(scale, 4)

# --- 3. Groups and their pivots ---------------------------------------------------------
# (group, member objects, pivot object, hinge axis as a local axis of the pivot object)
GROUPS = [
    ("StabilatorLeft", ["leftElevator"], "leftElevator", "X"),
    ("StabilatorRight", ["rightElevator"], "rightElevator", "X"),
    ("FlaperonLeft", ["leftFlaperon"], "leftFlaperon", "X"),
    ("FlaperonRight", ["rightFlaperon"], "rightFlaperon", "X"),
    ("RudderLeft", ["leftRudder"], "leftRudder", "Y"),
    ("RudderRight", ["rightRudder"], "rightRudder", "Y"),
    ("SlatLeft", ["leftSlat"], "leftSlat", "X"),
    ("SlatRight", ["rightSlat"], "rightSlat", "X"),
    ("NoseGearUpper", ["noseGear", "nosePiston", "noseStrutT"], "noseGear", None),
    ("NoseGearLower", ["noseSuspension", "noseStrutB"], "noseSuspension", "Z"),
    ("NoseWheel", ["noseWheel"], "noseWheel", "X"),
    ("LeftGearUpper", ["leftGear", "leftPiston", "leftStrutT"], "leftGear", None),
    ("LeftGearLower", ["leftSuspension", "leftStrutB"], "leftSuspension", "Z"),
    ("LeftWheel", ["leftWheel"], "leftWheel", "X"),
    ("RightGearUpper", ["rightGear", "rightPiston", "rightStrutT"], "rightGear", None),
    ("RightGearLower", ["rightSuspension", "rightStrutB"], "rightSuspension", "Z"),
    ("RightWheel", ["rightWheel"], "rightWheel", "X"),
    ("Canopy", ["canopy"], "canopy", None),
]
grouped = {name for _, members, _, _ in GROUPS for name in members}
body_members = [o.name for o in objects if o.type == "MESH" and o.name not in grouped]
GROUPS.insert(0, ("Body", body_members, None, None))

rig = {"source_scale": scale, "cg_model_m": list(cg), "length_m": TARGET_LENGTH_M, "parts": {}}
for group, members, pivot_name, axis_name in GROUPS:
    entry = {"members": members}
    if pivot_name:
        pivot_world = objects[pivot_name].matrix_world
        entry["pivot"] = list(to_cg @ pivot_world.to_translation())
        if axis_name:
            axis = pivot_world.to_3x3().normalized().col["XYZ".index(axis_name)]
            entry["axis"] = list(axis.normalized())
    rig["parts"][group] = entry

# Wheel radii and contact geometry for the physics (centre-of-gravity frame, metres).
for wheel in ("noseWheel", "leftWheel", "rightWheel"):
    o = objects[wheel]
    pts = [to_cg @ (o.matrix_world @ mathutils.Vector(c)) for c in o.bound_box]
    radius = 0.5 * (max(p.z for p in pts) - min(p.z for p in pts))
    rig.setdefault("wheels", {})[wheel] = {"axle": list(to_cg @ o.matrix_world.to_translation()), "radius": radius}
for strut in ("noseGear", "leftGear", "rightGear"):
    rig.setdefault("strut_tops", {})[strut] = list(to_cg @ objects[strut].matrix_world.to_translation())

# --- 4. Bake vertices into the CG frame and join groups -------------------------------
for o in list(objects):
    if o.type != "MESH":
        continue
    world = o.matrix_world.copy()
    o.parent = None
    o.data = o.data.copy()  # single user before transforming
    o.data.transform(to_cg @ world)
    o.matrix_world = mathutils.Matrix.Identity(4)

bpy.ops.object.select_all(action="DESELECT")
exported = []
for group, members, _, _ in GROUPS:
    present = [objects[m] for m in members if m in objects]
    if not present:
        continue
    for o in present:
        o.select_set(True)
    bpy.context.view_layer.objects.active = present[0]
    if len(present) > 1:
        bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = group
    joined.data.name = group
    joined.select_set(False)
    exported.append(joined)

# Anything not exported (empties, cameras) is removed so the FBX holds only the parts.
for o in list(objects):
    if o not in exported:
        bpy.data.objects.remove(o, do_unlink=True)

# Smooth shading on curved surfaces, hard edges kept where faces meet at > 35 degrees
# (the source is flat-shaded, which makes its low polygon count obvious).
for o in exported:
    o.data.shade_smooth()
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35.0), keep_sharp_edges=True)
    o.select_set(False)

# --- 5. Export --------------------------------------------------------------------------
fbx_path = os.path.join(OUT_DIR, "F35_Parts.fbx")
bpy.ops.export_scene.fbx(
    filepath=fbx_path,
    use_selection=False,
    object_types={"MESH"},
    apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_UNITS",
    axis_forward="X",
    axis_up="Z",
    use_mesh_modifiers=True,
    mesh_smooth_type="OFF",  # export the custom normals from smooth-by-angle
    use_tspace=True,
    add_leaf_bones=False,
    bake_anim=False,
)
with open(os.path.join(OUT_DIR, "rig.json"), "w") as f:
    json.dump(rig, f, indent=2)

tris = {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in exported}
print("EXPORTED", fbx_path, "scale", round(scale, 4), "cg", [round(v, 3) for v in cg])
print("TRIS", sum(tris.values()), tris)
print("WHEELS", json.dumps(rig["wheels"]))
print("STRUT_TOPS", json.dumps(rig["strut_tops"]))
