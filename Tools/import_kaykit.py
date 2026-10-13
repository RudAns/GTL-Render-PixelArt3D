"""Import the KayKit (CC0) characters, animations and props, headless:

  UnrealEditor-Cmd.exe PixelArt3D.uproject -run=pythonscript -script="<abs path>/Tools/import_kaykit.py"

Sources live in SourceAssets/KayKit (copied from the free packs, see the License files there).
Creates /Game/KayKit/{Characters,Anims,Props,Textures}. Textures are palette strips, so they are
imported with nearest filtering and no mips: a texel on the model is a flat swatch of the strip.
"""
import glob
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "SourceAssets", "KayKit")
ROOT = "/Game/KayKit"
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

# KayKit units: props are authored ~100x the character FBX. Characters come out 2.5 m tall at x100;
# one scale for the whole pack keeps it consistent and puts a character at ~160 cm (26 texels at 16/m).
PACK_SCALE = 0.63
CHAR_SCALE = 100.0 * PACK_SCALE

# The legacy FBX importer honours FbxImportUI (skeleton reuse, anim-only imports).
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")

CHARACTERS = ["Knight", "Barbarian", "Mage", "Ranger", "Rogue", "Rogue_Hooded"]
ADVENTURER_PROPS = ["sword_1handed", "sword_2handed", "shield_round", "shield_badge_color", "staff", "axe_2handed",
                    "mug_full", "spellbook_closed", "quiver", "crossbow_2handed"]
HALLOWEEN_PROPS = ["arch", "arch_gate", "bench", "candle_triple", "coffin", "coffin_decorated", "crypt", "fence",
                   "fence_broken", "fence_gate", "fence_pillar", "fence_seperate", "grave_A", "grave_B",
                   "gravemarker_A", "gravestone", "lantern_standing", "pillar", "plaque_candles", "post_lantern",
                   "pumpkin_orange", "pumpkin_orange_jackolantern", "pumpkin_yellow", "pumpkin_yellow_small",
                   "shrine_candles", "skull", "tree_dead_large", "tree_dead_medium", "tree_pine_orange_large",
                   "tree_pine_orange_medium", "tree_pine_yellow_large", "tree_pine_yellow_medium", "tree_pine_yellow_small",
                   "path_A", "path_B", "path_C", "path_D", "floor_dirt_small", "floor_dirt", "floor_dirt_grave", "plaque", "grave_A_destroyed", "bone_B", "bone_C", "lantern_hanging", "post_skull", "pumpkin_orange_small",
                   "tree_pine_orange_small", "candle_melted", "lantern_hanging", "bone_A", "ribcage", "tree_dead_small"]


def log(*a):
    unreal.log("[import_kaykit] " + " ".join(str(x) for x in a))


def task(filename, dest, name=None, options=None):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", filename)
    t.set_editor_property("destination_path", dest)
    if name:
        t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    if options:
        t.set_editor_property("options", options)
    return t


def import_textures():
    files = glob.glob(os.path.join(SRC, "Characters", "*.png")) + glob.glob(os.path.join(SRC, "Halloween", "*.png"))
    AT.import_asset_tasks([task(f, f"{ROOT}/Textures") for f in files])
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        tex = unreal.load_asset(f"{ROOT}/Textures/{name}")
        if not tex:
            log("missing texture", name)
            continue
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        EAL.save_loaded_asset(tex)
        log("texture", name)


def base_ui():
    ui = unreal.FbxImportUI()
    # without this an automated import guesses the type and turns the anim-only FBX into a mesh
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    return ui


def import_characters():
    skeleton = None
    for c in CHARACTERS:
        ui = base_ui()
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("import_animations", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        ui.set_editor_property("create_physics_asset", False)
        if skeleton:
            ui.set_editor_property("skeleton", skeleton)
        sk = ui.get_editor_property("skeletal_mesh_import_data")
        sk.set_editor_property("import_morph_targets", False)
        sk.set_editor_property("import_uniform_scale", CHAR_SCALE)
        AT.import_asset_tasks([task(os.path.join(SRC, "Characters", c + ".fbx"), f"{ROOT}/Characters", c, ui)])
        mesh = unreal.load_asset(f"{ROOT}/Characters/{c}")
        if not mesh:
            log("FAILED character", c)
            continue
        if skeleton is None:
            skeleton = mesh.get_editor_property("skeleton")
            log("skeleton", skeleton.get_path_name())
        b = mesh.get_bounds()
        log("character", c, "bounds extent", b.box_extent, "origin", b.origin, "materials", len(mesh.materials))
    return skeleton


def import_anims(skeleton):
    for f in glob.glob(os.path.join(SRC, "Animations", "*.fbx")):
        ui = base_ui()
        ui.set_editor_property("import_mesh", False)
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("import_animations", True)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
        ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_ANIMATION)
        ui.set_editor_property("skeleton", skeleton)
        ad = ui.get_editor_property("anim_sequence_import_data")
        ad.set_editor_property("import_bone_tracks", True)
        ad.set_editor_property("remove_redundant_keys", False)
        ad.set_editor_property("import_uniform_scale", CHAR_SCALE)
        AT.import_asset_tasks([task(f, f"{ROOT}/Anims", None, ui)])
    for p in EAL.list_assets(f"{ROOT}/Anims", recursive=False):
        a = unreal.load_asset(p)
        if isinstance(a, unreal.AnimSequence):
            log("anim", p, "length", a.get_play_length())


def import_props(folder, names, dest):
    tasks = []
    for n in names:
        ui = base_ui()
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_as_skeletal", False)
        ui.set_editor_property("import_animations", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        sm = ui.get_editor_property("static_mesh_import_data")
        sm.set_editor_property("combine_meshes", True)
        sm.set_editor_property("auto_generate_collision", False)
        sm.set_editor_property("import_uniform_scale", PACK_SCALE)
        tasks.append(task(os.path.join(SRC, folder, n + ".fbx"), dest, n, ui))
    AT.import_asset_tasks(tasks)
    for n in names:
        m = unreal.load_asset(f"{dest}/{n}")
        if not m:
            log("FAILED prop", n)
            continue
        b = m.get_bounds()
        log("prop", n, "extent", b.box_extent, "origin", b.origin)


def main():
    # for a clean reimport delete Content/KayKit on disk first; replace_existing covers the rest
    import_textures()
    skeleton = import_characters()
    if skeleton:
        import_anims(skeleton)
    import_props("Props", ADVENTURER_PROPS, f"{ROOT}/Props")
    import_props("Halloween", HALLOWEEN_PROPS, f"{ROOT}/Halloween")
    # an FBX with several takes yields several sequences, but the task only saves the last one
    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log("done")


main()
