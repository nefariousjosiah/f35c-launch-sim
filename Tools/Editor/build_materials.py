# Copyright (c) 2026 nefariousjosiah. MIT License.
"""Creates the project's physically based materials.

  UnrealEditor-Cmd.exe F35CLaunch.uproject -run=pythonscript -script=Tools/Editor/build_materials.py

M_PBR is a small master material: base colour, roughness, metallic, specular and
emissive parameters, plus world-space "weathering" that varies colour and roughness
over metres so large surfaces never read as a flat CG fill. Every surface in the
scene is an instance of it, with values taken from Docs/Reference.md:

  - Airframe: FS 36170 "Have Glass Grey", linear albedo ~0.13, slightly warm, satin.
  - Deck: flat near-black non-skid (~FS 36081), very rough, patchy wear.
"""

import unreal

MATERIAL_DIR = "/Game/Materials"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary


def recreate(name, path, asset_class, factory):
    full = f"{path}/{name}"
    if assets.does_asset_exist(full):
        assets.delete_asset(full)
    return tools.create_asset(name, path, asset_class, factory)


def scalar(material, name, default, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def vector(material, name, default, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*default))
    return node


def build_master():
    m = recreate("M_PBR", MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())

    base = vector(m, "BaseColor", (0.5, 0.5, 0.5, 1.0), -900, -200)
    roughness = scalar(m, "Roughness", 0.5, -900, 100)
    metallic = scalar(m, "Metallic", 0.0, -400, 250)
    specular = scalar(m, "Specular", 0.5, -400, 350)
    emissive = vector(m, "Emissive", (0.0, 0.0, 0.0, 1.0), -400, 450)
    weathering = scalar(m, "Weathering", 0.0, -900, 300)
    scale = scalar(m, "WeatheringScale", 300.0, -1300, 0)  # cm per noise cell

    # World-space noise, two octaves: broad patches plus finer mottling.
    noise = mel.create_material_expression(m, unreal.MaterialExpressionNoise, -1100, 0)
    noise.set_editor_property("scale", 1.0)
    noise.set_editor_property("levels", 4)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)
    noise.set_editor_property("turbulence", True)
    world = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    divide = mel.create_material_expression(m, unreal.MaterialExpressionDivide, -1300, 100)
    mel.connect_material_expressions(world, "", divide, "A")
    mel.connect_material_expressions(scale, "", divide, "B")
    mel.connect_material_expressions(divide, "", noise, "Position")

    # Colour: base * lerp(1 - w, 1 + w, noise)
    one = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -900, 450)
    one.set_editor_property("r", 1.0)
    low = mel.create_material_expression(m, unreal.MaterialExpressionSubtract, -700, 350)
    mel.connect_material_expressions(one, "", low, "A")
    mel.connect_material_expressions(weathering, "", low, "B")
    high = mel.create_material_expression(m, unreal.MaterialExpressionAdd, -700, 450)
    mel.connect_material_expressions(one, "", high, "A")
    mel.connect_material_expressions(weathering, "", high, "B")
    factor = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -500, 100)
    mel.connect_material_expressions(low, "", factor, "A")
    mel.connect_material_expressions(high, "", factor, "B")
    mel.connect_material_expressions(noise, "", factor, "Alpha")
    tinted = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, -100)
    mel.connect_material_expressions(base, "", tinted, "A")
    mel.connect_material_expressions(factor, "", tinted, "B")

    # Roughness varies the other way: worn, darker patches are slightly glossier.
    rough_var = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 100)
    mel.connect_material_expressions(roughness, "", rough_var, "A")
    mel.connect_material_expressions(factor, "", rough_var, "B")
    rough_clamped = mel.create_material_expression(m, unreal.MaterialExpressionClamp, -150, 100)
    mel.connect_material_expressions(rough_var, "", rough_clamped, "")

    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough_clamped, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    assets.save_loaded_asset(m)
    return m


# name: base colour (linear), roughness, metallic, specular, weathering, weathering scale (cm), emissive
INSTANCES = {
    # Neutral rather than warm: the low sun is already warmed by the atmosphere.
    "MI_Airframe_FS36170": ((0.125, 0.128, 0.132), 0.52, 0.0, 0.5, 0.06, 120.0, None),
    "MI_DarkMetal": ((0.035, 0.035, 0.036), 0.40, 0.7, 0.5, 0.10, 40.0, None),
    "MI_GearWhite": ((0.62, 0.62, 0.60), 0.38, 0.0, 0.5, 0.06, 30.0, None),
    "MI_Tyre": ((0.018, 0.018, 0.018), 0.88, 0.0, 0.35, 0.10, 20.0, None),
    "MI_CanopyTint": ((0.015, 0.02, 0.028), 0.04, 0.35, 0.8, 0.0, 100.0, None),
    "MI_CockpitDark": ((0.025, 0.025, 0.027), 0.70, 0.0, 0.4, 0.0, 100.0, None),
    "MI_NavRed": ((0.3, 0.0, 0.0), 0.3, 0.0, 0.5, 0.0, 100.0, (8.0, 0.2, 0.1)),
    "MI_NavGreen": ((0.0, 0.3, 0.05), 0.3, 0.0, 0.5, 0.0, 100.0, (0.1, 8.0, 0.6)),
    "MI_WarningYellow": ((0.6, 0.45, 0.05), 0.5, 0.0, 0.5, 0.0, 100.0, None),
    "MI_NonSkid": ((0.030, 0.031, 0.033), 0.92, 0.0, 0.35, 0.35, 450.0, None),
    "MI_TrackSteel": ((0.10, 0.10, 0.10), 0.45, 0.85, 0.5, 0.25, 80.0, None),
    "MI_SlotShadow": ((0.004, 0.004, 0.004), 0.9, 0.0, 0.2, 0.0, 100.0, None),
    "MI_ShuttleSteel": ((0.06, 0.06, 0.065), 0.5, 0.8, 0.5, 0.15, 30.0, None),
    "MI_DeflectorGrey": ((0.08, 0.085, 0.09), 0.75, 0.0, 0.4, 0.3, 90.0, None),
    "MI_MarkingWhite": ((0.62, 0.62, 0.60), 0.80, 0.0, 0.4, 0.25, 150.0, None),
    "MI_MarkingRed": ((0.42, 0.03, 0.02), 0.80, 0.0, 0.4, 0.2, 150.0, None),
    "MI_MarkingYellow": ((0.62, 0.45, 0.04), 0.80, 0.0, 0.4, 0.2, 150.0, None),
    "MI_HullGrey": ((0.11, 0.115, 0.12), 0.65, 0.0, 0.5, 0.2, 400.0, None),
    "MI_SeaPlaceholder": ((0.004, 0.012, 0.018), 0.06, 0.0, 0.5, 0.0, 100.0, None),
}


def build_instances(master):
    for name, (color, rough, metal, spec, weather, wscale, emissive) in INSTANCES.items():
        mi = recreate(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, master)
        mel.set_material_instance_vector_parameter_value(mi, "BaseColor", unreal.LinearColor(*color, 1.0))
        mel.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
        mel.set_material_instance_scalar_parameter_value(mi, "Metallic", metal)
        mel.set_material_instance_scalar_parameter_value(mi, "Specular", spec)
        mel.set_material_instance_scalar_parameter_value(mi, "Weathering", weather)
        mel.set_material_instance_scalar_parameter_value(mi, "WeatheringScale", wscale)
        if emissive:
            mel.set_material_instance_vector_parameter_value(mi, "Emissive", unreal.LinearColor(*emissive, 1.0))
        assets.save_loaded_asset(mi)


master = build_master()
build_instances(master)
unreal.log(f"Built M_PBR and {len(INSTANCES)} instances")
