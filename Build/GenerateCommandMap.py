"""Replace Boot's contents with the milestone-two command arena in Unreal Editor.

Run after compiling CoopRTSEditor (from the project root):
  "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/CoopRTS.uproject" \
    -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="$PWD/Build/GenerateCommandMap.py" \
    -unattended -nullrhi -nosplash

The editor actor factory constructs the NavMeshBoundsVolume's real brush. Its
measured bounds, not assumed default brush dimensions, determine arena scale.
Recast uses Dynamic generation so the saved map also navigates in cooked games
without relying on an asynchronous editor navigation bake finishing before save.
Match actors (ArenaBounds, HQs, sectors) come from Build/MatchLayout.py; floor,
walls and navigation bounds are sized from the placed ArenaBounds.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import MatchLayout


assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
material_library = unreal.MaterialEditingLibrary


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def spawn(actor_class, label, location, rotation=unreal.Rotator()):
    actor = require(
        actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation),
        "Could not spawn " + label,
    )
    actor.set_actor_label(label)
    return actor


# Keep generated asset paths stable across reruns and packaged references.
material_path = "/Game/Materials/M_CommandUnit"
if assets.does_asset_exist(material_path):
    # Preserve the material graph: the native unit CDO can hold it in the root set.
    material = require(assets.load_asset(material_path), "Could not load unit material")
else:
    material = require(tools.create_asset(
        "M_CommandUnit", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew()
    ), "Could not create unit material")
    color = material_library.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property("parameter_name", "TeamColor")
    color.set_editor_property("default_value", unreal.LinearColor(0.04, 0.50, 1.0, 1.0))
    require(material_library.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR),
            "Could not connect team color")
    roughness = material_library.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 180)
    roughness.set_editor_property("r", 0.85)
    require(material_library.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS),
            "Could not connect roughness")
    material_library.recompile_material(material)
    require(assets.save_loaded_asset(material), "Could not save unit material")


def colored_material(name, rgb):
    path = "/Game/Materials/" + name
    instance = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(
        name, "/Game/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
    )
    require(instance, "Could not create " + name)
    material_library.set_material_instance_parent(instance, material)
    color_value = unreal.LinearColor(*rgb, 1.0)
    # UE 5.8.3's setter returns false unconditionally; verify the actual value.
    material_library.set_material_instance_vector_parameter_value(instance, "TeamColor", color_value)
    actual = material_library.get_material_instance_vector_parameter_value(instance, "TeamColor")
    require(all(abs(getattr(actual, channel) - getattr(color_value, channel)) < 0.0001
                for channel in ("r", "g", "b", "a")), "Could not set color for " + name)
    require(assets.save_loaded_asset(instance), "Could not save " + name)
    return instance


floor_material = colored_material("MI_CommandFloor", (0.09, 0.12, 0.15))
obstacle_material = colored_material("MI_CommandObstacle", (0.48, 0.24, 0.10))
wall_material = colored_material("MI_CommandBoundary", (0.20, 0.25, 0.30))
team_colors = ((0.04, 0.50, 1.0), (1.0, 0.30, 0.04), (0.15, 0.85, 0.25),
               (0.65, 0.15, 1.0), (1.0, 0.80, 0.04))
team_materials = [colored_material("MI_CommandTeam" + str(index), rgb)
                  for index, rgb in enumerate(team_colors)]

map_path = "/Game/Maps/Boot"
if assets.does_asset_exist(map_path):
    require(levels.load_level(map_path), "Could not load Boot for replacement")
    for actor in actors.get_all_level_actors():
        # The world's settings and default builder brush belong to the world.
        if isinstance(actor, unreal.WorldSettings):
            continue
        if isinstance(actor, unreal.Brush) and not isinstance(actor, unreal.Volume):
            continue
        require(actors.destroy_actor(actor), "Could not remove old actor " + actor.get_name())
else:
    require(levels.new_level(map_path, False), "Could not create Boot")

cube = require(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"), "Engine cube is missing")
cylinder = require(unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder"), "Engine cylinder is missing")
blocking_footprints = []


def primitive(label, location, scale, surface, mesh=cube, collision=True):
    if collision and label != "ArenaFloor":
        blocking_footprints.append((location[0], location[1], scale[0] * 100, scale[1] * 100, 0))
    actor = spawn(unreal.StaticMeshActor, label, location)
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_material(0, surface)
    component.set_collision_profile_name("BlockAll" if collision else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


capture_anchors = {}
arena_x, arena_y = MatchLayout.place(spawn, capture_anchors)
primitive("ArenaFloor", (0, 0, -50), (arena_x / 50, arena_y / 50, 1), floor_material)
primitive("CentralObstacle", (0, 0, 300), (12, 20, 6), obstacle_material)
# Keep characters on the playable floor as well as bounding orders: 50 cm walls whose inner face is 50 cm inside the arena.
primitive("BoundaryNorth", (0, arena_y - 25, 60), (arena_x / 50, 0.5, 1.2), wall_material)
primitive("BoundarySouth", (0, -(arena_y - 25), 60), (arena_x / 50, 0.5, 1.2), wall_material)
primitive("BoundaryEast", (arena_x - 25, 0, 60), (0.5, arena_y / 50, 1.2), wall_material)
primitive("BoundaryWest", (-(arena_x - 25), 0, 60), (0.5, arena_y / 50, 1.2), wall_material)
for index, y in enumerate((0, -850, 850, -1700, 1700)):
    primitive("ArmyHome" + str(index), (-1800, y, 2), (5, 5, 0.04), team_materials[index], cylinder, False)
regions = MatchLayout.region_plan((arena_x, arena_y))
deposits = MatchLayout.deposit_plan(regions, (arena_x, arena_y),
                                   lambda point: MatchLayout.clear_of_blockers(point, blocking_footprints))
MatchLayout.place_regions(spawn, regions, deposits, capture_anchors)
spawn(unreal.PlayerStart, "CommanderStart", (-1800, 0, 150))

sun = spawn(unreal.DirectionalLight, "ArenaSun", (0, 0, 3000), unreal.Rotator(-55, -30, 0))
sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun_component.set_editor_property("intensity", 5.0)
sun_component.set_editor_property("atmosphere_sun_light", True)
spawn(unreal.SkyAtmosphere, "ArenaAtmosphere", (0, 0, 0))
sky = spawn(unreal.SkyLight, "ArenaSky", (0, 0, 2000))
sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_component.set_editor_property("intensity", 1.0)
sky_component.set_editor_property("real_time_capture", True)

bounds = spawn(unreal.NavMeshBoundsVolume, "ArenaNavigationBounds", (0, 0, 150))
_, extent = bounds.get_actor_bounds(False)
require(min(extent.x, extent.y, extent.z) > 0.0,
        "Editor factory did not construct a navigation brush; cannot scale an empty volume")
bounds.set_actor_scale3d(unreal.Vector(arena_x / extent.x, arena_y / extent.y, 450 / extent.z))
_, arena_extent = bounds.get_actor_bounds(False)
require(abs(arena_extent.x - arena_x) < 1 and abs(arena_extent.y - arena_y) < 1,
        "Navigation bounds did not resize to the arena")

nav_meshes = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.RecastNavMesh)]
if not nav_meshes:
    nav_meshes = [spawn(unreal.RecastNavMesh, "RecastNavMesh-Default", (0, 0, 0))]
for nav_mesh in nav_meshes:
    nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    nav_mesh.set_editor_property("force_rebuild_on_load", True)

world = editor.get_editor_world()
world.get_world_settings().set_editor_property(
    "default_game_mode", require(unreal.load_class(None, "/Script/CoopRTS.CommandGameMode"),
                                 "Build CoopRTSEditor before generating the map")
)
require(levels.save_current_level(), "Could not save command arena")
unreal.log("COMMAND_ARENA_GENERATED /Game/Maps/Boot bounds=+/-%d floor_z=0 navigation=Dynamic" % arena_x)
