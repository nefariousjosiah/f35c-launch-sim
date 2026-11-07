# Copyright (c) 2026 nefariousjosiah. MIT License.
"""Imports the generated textures and builds the deck and effect materials.

  UnrealEditor-Cmd.exe F35CLaunch.uproject -run=pythonscript -script=Tools/Editor/build_deck_and_effects.py

Textures come from Tools/Textures/generate_textures.py (procedural, our own work).

  M_Deck       flight-deck non-skid: tiled grit normal and mottling, plus a deck-wide
               mask of rubber, oil, traffic wear and exhaust heat (Docs/Reference.md:
               "flat dark-grey non-skid with patchy wear", oil at the hookup point,
               heat-discoloured deck ahead of the jet blast deflector).
  M_HeatHaze   refraction-only exhaust shimmer (Pixel Normal Offset), driven by thrust.
  M_SteamPuff  lit translucent catapult steam for the instanced puff simulation.
"""

import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "SourceArt", "Generated")
TEXTURES = "/Game/Textures"
MATERIALS = "/Game/Materials"

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
TS = unreal.TextureCompressionSettings


# --- Textures --------------------------------------------------------------------------
def import_texture(name, compression, tiling=True):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SOURCE, name + ".png")
    task.destination_path = TEXTURES
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{TEXTURES}/{name}")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", compression)
    if not tiling:
        texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    assets.save_loaded_asset(texture)
    return texture


grit_n = import_texture("T_NonSkid_N", TS.TC_NORMALMAP)
grit_d = import_texture("T_NonSkid_D", TS.TC_MASKS)
deck_mask = import_texture("T_DeckMask", TS.TC_MASKS, tiling=False)
haze_n = import_texture("T_HazeNoise_N", TS.TC_NORMALMAP)
puff = import_texture("T_SteamPuff", TS.TC_DEFAULT)
sea_n = import_texture("T_SeaNormal", TS.TC_NORMALMAP)


# --- Graph helpers -----------------------------------------------------------------------
class Graph:
    def __init__(self, name):
        path = f"{MATERIALS}/{name}"
        if assets.does_asset_exist(path):
            assets.delete_asset(path)
        self.m = tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
        self.y = 0

    def node(self, cls, **props):
        n = mel.create_material_expression(self.m, cls, -600, self.y)
        self.y += 140
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, b, pin="", out=""):
        mel.connect_material_expressions(a, out, b, pin)

    def scalar(self, name, value):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    def color(self, name, rgb):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                         default_value=unreal.LinearColor(*rgb, 1.0))

    def const(self, value):
        return self.node(unreal.MaterialExpressionConstant, r=value)

    def op(self, cls, a, b):
        n = self.node(cls)
        self.link(a, n, "A")
        self.link(b, n, "B")
        return n

    def mul(self, a, b): return self.op(unreal.MaterialExpressionMultiply, a, b)
    def add(self, a, b): return self.op(unreal.MaterialExpressionAdd, a, b)
    def div(self, a, b): return self.op(unreal.MaterialExpressionDivide, a, b)

    def lerp(self, a, b, alpha):
        n = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, n, "A")
        self.link(b, n, "B")
        self.link(alpha, n, "Alpha")
        return n

    def mask(self, src, r=False, g=False, b=False, a=False):
        n = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=a)
        self.link(src, n)
        return n

    def sample(self, texture, uv, sampler):
        n = self.node(unreal.MaterialExpressionTextureSample, texture=texture, sampler_type=sampler)
        self.link(uv, n, "UVs")
        return n

    def out(self, node, prop, pin=""):
        mel.connect_material_property(node, pin, prop)

    def set(self, **props):
        for k, v in props.items():
            try:
                self.m.set_editor_property(k, v)
            except Exception as error:  # property names drift between engine versions
                unreal.log_warning(f"{self.m.get_name()}: could not set {k}: {error}")

    def finish(self):
        mel.recompile_material(self.m)
        assets.save_loaded_asset(self.m)
        return self.m


MP = unreal.MaterialProperty
ST = unreal.MaterialSamplerType

# --- M_Deck --------------------------------------------------------------------------------
g = Graph("M_Deck")
world = g.node(unreal.MaterialExpressionWorldPosition)
world_xy = g.mask(world, r=True, g=True)
grit_uv = g.div(world_xy, g.const(100.0))            # grit tiles every 1 m
detail_uv = g.div(world_xy, g.const(400.0))          # mottling tiles every 4 m
local = g.node(unreal.MaterialExpressionLocalPosition)
local_xy = g.mask(local, r=True, g=True)
mask_uv = g.add(g.div(local_xy, g.const(100.0)), g.const(0.5))  # unit cube: -50..50 cm -> 0..1

grit = g.sample(grit_n, grit_uv, ST.SAMPLERTYPE_NORMAL)
detail = g.sample(grit_d, detail_uv, ST.SAMPLERTYPE_MASKS)
layers = g.sample(deck_mask, mask_uv, ST.SAMPLERTYPE_MASKS)
up = g.mask(g.node(unreal.MaterialExpressionVertexNormalWS), b=True)
top = g.node(unreal.MaterialExpressionSaturate)
g.link(g.mul(g.add(up, g.const(-0.7)), g.const(4.0)), top)  # 1 on the deck surface, 0 on the sides
rubber, oil, wear = (g.mul(g.mask(layers, r=True), top), g.mul(g.mask(layers, g=True), top),
                     g.mul(g.mask(layers, b=True), top))
heat = g.node(unreal.MaterialExpressionComponentMask, r=True)  # alpha comes from the sample's A pin
g.link(layers, heat, "", out="A")
heat = g.mul(heat, top)
mottle, rough_var = g.mask(detail, r=True), g.mask(detail, g=True)

# Base: near-black non-skid with tonal mottling, then wear, heat, rubber and oil on top.
base = g.color("NonSkidColor", (0.030, 0.031, 0.033))
mottled = g.mul(base, g.add(g.mul(mottle, g.scalar("MottleAmount", 0.5)), g.const(0.75)))
worn = g.lerp(mottled, g.color("WearColor", (0.060, 0.061, 0.063)), g.mul(wear, g.scalar("WearAmount", 0.55)))
heated = g.lerp(worn, g.color("HeatColor", (0.050, 0.038, 0.026)), g.mul(heat, g.scalar("HeatAmount", 0.7)))
rubbered = g.lerp(heated, g.color("RubberColor", (0.011, 0.011, 0.011)), rubber)
oiled = g.lerp(rubbered, g.color("OilColor", (0.013, 0.012, 0.011)), g.mul(oil, g.const(0.85)))
g.out(oiled, MP.MP_BASE_COLOR)

rough = g.mul(g.scalar("Roughness", 0.9), g.add(g.mul(rough_var, g.const(0.3)), g.const(0.85)))
rough = g.lerp(rough, g.const(0.95), g.mul(wear, g.const(0.4)))
rough = g.lerp(rough, g.const(0.72), rubber)                     # rubber is a little smoother
rough = g.lerp(rough, g.const(0.30), oil)                        # oil films shine
g.out(rough, MP.MP_ROUGHNESS)
g.out(g.const(0.4), MP.MP_SPECULAR)

flat = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
filled = g.mul(g.lerp(g.scalar("GritStrength", 1.0), g.const(0.35), g.add(rubber, oil)), top)   # grime fills the grit
g.out(g.lerp(flat, grit, filled), MP.MP_NORMAL)
g.finish()

# --- M_HeatHaze ----------------------------------------------------------------------------
h = Graph("M_HeatHaze")
h.set(blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, shading_model=unreal.MaterialShadingModel.MSM_UNLIT,
      two_sided=True, refraction_method=unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET,
      has_pixel_animation=True)
uv = h.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=0.7, v_tiling=1.1)
fast = h.node(unreal.MaterialExpressionPanner, speed_x=0.15, speed_y=-1.6)   # streaming aft
h.link(uv, fast, "Coordinate")
slow = h.node(unreal.MaterialExpressionPanner, speed_x=-0.07, speed_y=-0.6)
uv2 = h.node(unreal.MaterialExpressionTextureCoordinate, u_tiling=0.45, v_tiling=0.6)
h.link(uv2, slow, "Coordinate")
n1 = h.sample(haze_n, fast, ST.SAMPLERTYPE_NORMAL)
n2 = h.sample(haze_n, slow, ST.SAMPLERTYPE_NORMAL)
noise_normal = h.add(h.mask(n1, r=True, g=True), h.mask(n2, r=True, g=True))
# Fade at silhouettes (Fresnel) and where the sheet meets other geometry (depth fade).
fresnel = h.node(unreal.MaterialExpressionFresnel, exponent=2.0)
h.link(h.node(unreal.MaterialExpressionVertexNormalWS), fresnel, "Normal")  # not the output normal (circular)
facing = h.op(unreal.MaterialExpressionSubtract, h.const(1.0), fresnel)
depth = h.node(unreal.MaterialExpressionDepthFade)
h.link(h.const(1.0), depth, "InOpacity")
h.link(h.const(80.0), depth, "FadeDistance")
strength = h.mul(h.mul(h.scalar("Intensity", 1.0), facing), depth)
offset = h.mul(noise_normal, strength)
normal = h.node(unreal.MaterialExpressionAppendVector)
h.link(offset, normal, "A")
h.link(h.const(1.0), normal, "B")
h.out(normal, MP.MP_NORMAL)
h.out(h.add(h.const(1.0), h.mul(strength, h.scalar("RefractionScale", 0.004))), MP.MP_REFRACTION)
h.out(h.const(0.0), MP.MP_OPACITY)
h.out(h.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0, 0, 0, 1)), MP.MP_EMISSIVE_COLOR)
h.finish()

# --- M_SteamPuff ---------------------------------------------------------------------------
s = Graph("M_SteamPuff")
s.set(blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, shading_model=unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
      two_sided=True, used_with_instanced_static_meshes=True, has_pixel_animation=True,
      translucency_lighting_mode=unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_PER_VERTEX_DIRECTIONAL)
tex = s.sample(puff, s.node(unreal.MaterialExpressionTextureCoordinate), ST.SAMPLERTYPE_LINEAR_COLOR)
fade = s.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=0)
soft = s.node(unreal.MaterialExpressionDepthFade)
s.link(s.const(1.0), soft, "InOpacity")
s.link(s.const(60.0), soft, "FadeDistance")
puff_alpha = s.node(unreal.MaterialExpressionComponentMask, r=True)  # from the sample's A pin
s.link(tex, puff_alpha, "", out="A")
opacity = s.mul(s.mul(s.mul(puff_alpha, fade), soft), s.scalar("Density", 0.55))
s.out(opacity, MP.MP_OPACITY)
steam_color = s.color("SteamColor", (0.75, 0.76, 0.78))
s.out(steam_color, MP.MP_BASE_COLOR)
# Forward scattering: real steam glows when back-lit; lit translucency alone renders it dark.
s.out(s.mul(steam_color, s.scalar("Scatter", 350.0)), MP.MP_EMISSIVE_COLOR)  # blending applies opacity
s.out(s.const(1.0), MP.MP_ROUGHNESS)
s.finish()

# --- M_Sea -----------------------------------------------------------------------------
# Deep water seen from deck height: dark, glossy, with wind waves at two scales drifting
# downwind, so the surface has texture and sun glints instead of a flat gradient.
w = Graph("M_Sea")
w.set(has_pixel_animation=True)
world_xy = w.mask(w.node(unreal.MaterialExpressionWorldPosition), r=True, g=True)
time = w.node(unreal.MaterialExpressionTime)
big_uv = w.add(w.div(world_xy, w.const(3200.0)), w.mul(time, w.const(0.010)))
small_uv = w.add(w.div(world_xy, w.const(650.0)), w.mul(time, w.const(0.035)))
big = w.sample(sea_n, big_uv, ST.SAMPLERTYPE_NORMAL)
small = w.sample(sea_n, small_uv, ST.SAMPLERTYPE_NORMAL)
# A swell layer with a ~300 m tile keeps distant water from averaging out to a flat fill.
swell_uv = w.add(w.div(world_xy, w.const(30000.0)), w.mul(time, w.const(0.0015)))
swell = w.sample(sea_n, swell_uv, ST.SAMPLERTYPE_NORMAL)
xy = w.add(w.add(w.mul(w.mask(big, r=True, g=True), w.scalar("BigWaves", 2.2)),
                 w.mul(w.mask(small, r=True, g=True), w.scalar("SmallWaves", 1.1))),
           w.mul(w.mask(swell, r=True, g=True), w.scalar("Swell", 1.6)))
normal = w.node(unreal.MaterialExpressionAppendVector)
w.link(xy, normal, "A")
w.link(w.const(1.0), normal, "B")
w.out(normal, MP.MP_NORMAL)
w.out(w.color("WaterColor", (0.010, 0.027, 0.042)), MP.MP_BASE_COLOR)
w.out(w.scalar("Roughness", 0.12), MP.MP_ROUGHNESS)
w.out(w.const(0.5), MP.MP_SPECULAR)
w.finish()

unreal.log("Built M_Deck, M_HeatHaze, M_SteamPuff, M_Sea")
