# Copyright (c) 2026 nefariousjosiah. MIT License.
"""Imports the converted F-35 parts (from Tools/Blender/export_f35_parts.py).

  UnrealEditor-Cmd.exe F35CLaunch.uproject -run=pythonscript -script=Tools/Editor/import_f35_model.py

Each part was baked into the centre-of-gravity frame in Blender, so they import in
place and assemble with zero offsets; rig.json (copied next to them) holds the
hinge pivots and axes the launch actor animates around. Everything lands in
Content/ThirdParty/F35B, which git ignores: the model is third-party content.
"""

import os
import shutil

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "ThirdPartySource", "F35B", "export")
DEST = "/Game/ThirdParty/F35B"

task = unreal.AssetImportTask()
task.filename = os.path.join(SOURCE, "F35_Parts.fbx")
task.destination_path = DEST
task.automated = True
task.replace_existing = True
task.save = True

options = unreal.FbxImportUI()
options.import_mesh = True
options.import_as_skeletal = False
options.import_materials = False
options.import_textures = False
options.import_animations = False
options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
mesh_data = options.static_mesh_import_data
mesh_data.combine_meshes = False
mesh_data.transform_vertex_to_absolute = True
mesh_data.convert_scene = True
mesh_data.force_front_x_axis = False
mesh_data.auto_generate_collision = False
mesh_data.generate_lightmap_u_vs = False
mesh_data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
task.options = options

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

content_dir = os.path.join(PROJECT, "Content", "ThirdParty", "F35B")
shutil.copyfile(os.path.join(SOURCE, "rig.json"), os.path.join(content_dir, "rig.json"))

# Report what arrived, with bounds, so the axis mapping can be checked.
for path in sorted(task.imported_object_paths):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.StaticMesh):
        box = asset.get_bounding_box()
        unreal.log(f"IMPORTED {asset.get_name()} min={box.min} max={box.max} slots="
                   f"{[s.material_slot_name for s in asset.static_materials]}")
