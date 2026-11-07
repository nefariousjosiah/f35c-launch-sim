# Copyright (c) 2026 nefariousjosiah. MIT License.
"""Builds /Game/Maps/LaunchTest from scratch: sun, sky, haze, placeholder sea,
the catapult deck, the launch actor and a deck-level camera.

The level is generated rather than hand-edited so that every lighting and camera
value lives in reviewable text. Run it headless:

  UnrealEditor-Cmd.exe F35CLaunch.uproject -run=pythonscript -script=Tools/Editor/build_launch_level.py

Values follow Docs/RenderSettings.md: a ~110,000 lux sun with the real sun's
0.545 degree disc, Sky Atmosphere driving the marine haze, and a physical camera
exposed at about EV 15 (sunny 16) with a 180 degree shutter.
"""

import unreal

LEVEL_PATH = "/Game/Maps/LaunchTest"
DECK_HEIGHT_CM = 1800.0  # flight deck above the waterline (EnvironmentParams.deckHeight)

assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()


def spawn(actor_class, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), label=None):
    """rotation is (pitch, yaw, roll) in degrees."""
    pitch, yaw, roll = rotation
    actor = actors.spawn_actor_from_class(
        actor_class, unreal.Vector(*location), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
    if label:
        actor.set_actor_label(label)
    return actor


def set_settings(obj, prop, values):
    """Sets override flags and values on a post-process settings struct."""
    settings = obj.get_editor_property(prop)
    for key, value in values.items():
        settings.set_editor_property(f"override_{key}", True)
        settings.set_editor_property(key, value)
    obj.set_editor_property(prop, settings)


def build():
    if assets.does_asset_exist(LEVEL_PATH):
        assets.delete_asset(LEVEL_PATH)
    levels.new_level(LEVEL_PATH)

    # Sun: early afternoon off San Diego in November, ~35 degrees high, from the aft starboard quarter.
    # The light travels forward and to port, so the camera on the starboard quarter sees the lit side.
    sun = spawn(unreal.DirectionalLight, (0.0, 0.0, 10000.0), (-35.0, -40.0, 0.0), "Sun")
    light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    light.set_editor_property("intensity", 110000.0)  # lux
    light.set_editor_property("light_source_angle", 0.545)  # the real sun's angular diameter
    light.set_editor_property("atmosphere_sun_light", True)

    atmosphere = spawn(unreal.SkyAtmosphere, label="SkyAtmosphere")
    sky_component = atmosphere.get_component_by_class(unreal.SkyAtmosphereComponent)
    # Marine haze: more aerosol scattering and stronger aerial perspective than the default clear sky.
    sky_component.set_editor_property("mie_scattering_scale", 0.006)
    sky_component.set_editor_property("aerial_pespective_view_distance_scale", 1.6)

    sky_light = spawn(unreal.SkyLight, label="SkyLight")
    sky_light_component = sky_light.get_component_by_class(unreal.SkyLightComponent)
    sky_light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_light_component.set_editor_property("real_time_capture", True)

    fog = spawn(unreal.ExponentialHeightFog, (0.0, 0.0, -DECK_HEIGHT_CM), label="MarineHaze")
    fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    # Thin low marine layer on top of the Sky Atmosphere's own aerial perspective. (A black
    # inscattering colour, as sometimes recommended, darkened the horizon here.)
    fog_component.set_editor_property("fog_density", 0.0015)
    fog_component.set_editor_property("fog_height_falloff", 0.08)

    spawn(unreal.VolumetricCloud, label="Clouds")

    # Placeholder sea until the FFT ocean is in: a large flat plane at the waterline.
    sea = spawn(unreal.StaticMeshActor, (0.0, 0.0, -DECK_HEIGHT_CM), label="SeaPlaceholder")
    sea_mesh = sea.get_component_by_class(unreal.StaticMeshComponent)
    sea_mesh.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane.Plane"))
    sea_mesh.set_material(0, unreal.load_asset("/Game/Materials/MI_SeaPlaceholder"))  # from build_materials.py
    # 300 km square: past the ~16 km horizon from deck height, so the plane's edge never shows.
    sea.set_actor_scale3d(unreal.Vector(300000.0, 300000.0, 1.0))

    # Global post-process: physical camera exposure, filmic tonemapper defaults.
    volume = spawn(unreal.PostProcessVolume, label="PostProcess")
    volume.set_editor_property("unbound", True)
    set_settings(volume, "settings", {
        "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
        "auto_exposure_apply_physical_camera_exposure": True,
        "motion_blur_amount": 0.5,  # 180 degree shutter
    })

    deck = spawn(unreal.load_class(None, "/Script/F35CLaunch.CatapultDeck"), label="CatapultDeck")

    # Deck-level camera on the aft starboard quarter, roughly where a Navy photographer stands:
    # ~10 m aft of the jet, 26 m to starboard, forward of the jet blast deflector.
    camera = spawn(unreal.CineCameraActor, (-1600.0, 2600.0, 170.0), (0.0, -125.0, 0.0), "DeckCamera")
    camera_component = camera.get_cine_camera_component()
    camera_component.set_editor_property("current_focal_length", 35.0)
    camera_component.set_editor_property("current_aperture", 11.0)  # f/11, 1/250 s, ISO 100 -> EV100 ~ 15
    # Focus and exposure are set by the launch actor at play (manual focus on the jet).
    set_settings(camera_component, "post_process_settings", {
        "camera_iso": 100.0,
        "camera_shutter_speed": 250.0,
    })

    launch = spawn(unreal.load_class(None, "/Script/F35CLaunch.F35CLaunchActor"), label="F35CLaunch")
    launch.set_editor_property("deck", deck)
    launch.set_editor_property("view_camera", camera)

    levels.save_current_level()
    unreal.log(f"Built {LEVEL_PATH}")


build()
