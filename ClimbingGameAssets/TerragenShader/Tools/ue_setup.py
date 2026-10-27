"""Create (or update) the Unreal assets for the Terragen sky, then render it once on the GPU as a check.

Runs inside the Unreal Editor with Python enabled; nothing needs to be enabled in the project for it:

  UnrealEditor.exe <path>/Prototype1.uproject -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
      -ExecutePythonScript="<path>/ClimbingGameAssets/TerragenShader/Tools/ue_setup.py" -RenderOffscreen -unattended
      -ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorLoadingSavingSettings]:RestoreOpenAssetTabsOnRestart=NeverRestore

(The last option stops the editor reopening last session's asset tabs; an offscreen editor crashed while
closing a restored Skeletal Mesh editor on exit.)

Creates in /Game/MyStuff/Skybox/Terragen:
  Textures/T_TerragenHeight_MountainTop, Textures/T_TerragenNoise  (16-bit greyscale, read exactly with Load)
  RT_TerragenSky          2048x2048 RGBA8 linear: the six faces in the old skybox cross layout
  M_TerragenRender        draws one atlas texel with TG_RenderAtlasTexel (Custom node)
  M_TerragenSkyDisplay    sky-sphere material: looks the view direction up in RT_TerragenSky
  TerragenSkyTest         a small test map with a floor, a player start and an ATerragenSkybox

Then it renders the atlas for MountainTop frames 1 and 400, times a full redraw, and exports
Out/ue/Atlas_GPU_0001.png and Atlas_GPU_0400.png for comparison with the CPU harness. Set the environment
variable TG_SETUP_NO_QUIT=1 to keep the editor open afterwards.
"""
import os

import unreal

DEST = '/Game/MyStuff/Skybox/Terragen'
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
ROOT = os.path.join(PROJECT, 'ClimbingGameAssets', 'TerragenShader')
OUT = os.path.join(ROOT, 'Out', 'ue')
ATLAS_SIZE = 2048

tools = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log(f'[TerragenSetup] {msg}')


def get_or_create(name, path, cls, factory):
    full = f'{path}/{name}'
    if EAL.does_asset_exist(full):
        return unreal.load_asset(full)
    return tools.create_asset(name, path, cls, factory)


def import_texture(src, name):
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = f'{DEST}/Textures'
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    tools.import_asset_tasks([task])
    tex = unreal.load_asset(f'{DEST}/Textures/{name}')
    # Exact 16-bit values: no sRGB, no compression beyond G16, no mips, no streaming.
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_GRAYSCALE)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    EAL.save_loaded_asset(tex)
    log(f'{name}: {tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()}')
    return tex


def texture_object_param(mat, name, tex, x, y):
    node = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('texture', tex)
    node.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    return node


def scalar_param(mat, name, value, x, y):
    node = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', value)
    return node


def custom_node(mat, x, y, desc, code, output_type, inputs, includes):
    node = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property('description', desc)
    node.set_editor_property('code', code)
    node.set_editor_property('output_type', output_type)
    node.set_editor_property('include_file_paths', includes)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    node.set_editor_property('inputs', pins)
    return node


def build_render_material(height, noise):
    mat = get_or_create('M_TerragenRender', DEST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -200)
    ht = texture_object_param(mat, 'HeightTex', height, -900, -60)
    nt = texture_object_param(mat, 'NoiseTex', noise, -900, 120)
    cam = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, 300)
    cam.set_editor_property('parameter_name', 'CameraPos')
    cam.set_editor_property('default_value', unreal.LinearColor(164.0, 223.0, 45.647, 0.0))
    heading = scalar_param(mat, 'AtlasHeading', 18.0, -900, 520)
    face = scalar_param(mat, 'FaceSize', ATLAS_SIZE / 4, -900, 620)
    start = scalar_param(mat, 'StartDistance', 0.0, -900, 720)
    # Terragen units; negative = off. ATerragenSkybox sets it from TerrainClearance (cm).
    clearance = scalar_param(mat, 'TerrainClearance', 0.1, -900, 820)
    custom = custom_node(
        mat, -450, 0, 'Terragen atlas texel',
        'return TG_RenderAtlasTexel(TGWorld_MountainTop(), HeightTex, NoiseTex, UV, CameraPos.xyz, AtlasHeading, FaceSize, StartDistance, TerrainClearance);',
        unreal.CustomMaterialOutputType.CMOT_FLOAT4,
        ['UV', 'HeightTex', 'NoiseTex', 'CameraPos', 'AtlasHeading', 'FaceSize', 'StartDistance', 'TerrainClearance'],
        ['/TerragenShaders/TerragenUE.ush', '/TerragenShaders/Worlds/MountainTop.ush'])
    for src, pin in ((uv, 'UV'), (ht, 'HeightTex'), (nt, 'NoiseTex'), (cam, 'CameraPos'),
                     (heading, 'AtlasHeading'), (face, 'FaceSize'), (start, 'StartDistance'),
                     (clearance, 'TerrainClearance')):
        MEL.connect_material_expressions(src, '', custom, pin)
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_display_material(rt):
    mat = get_or_create('M_TerragenSkyDisplay', DEST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('is_sky', True)
    view = MEL.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -1100, 0)
    plus_x = scalar_param(mat, 'HeadingAtPlusX', 18.0, -1100, 120)
    heading = scalar_param(mat, 'AtlasHeading', 18.0, -1100, 220)
    face = scalar_param(mat, 'FaceSize', ATLAS_SIZE / 4, -1100, 320)
    lookup = custom_node(
        mat, -750, 0, 'Atlas UV for view direction',
        # CameraVector points from the pixel to the camera; the view direction is the opposite.
        'return TG_AtlasUV(TG_FromUEDirection(-ViewDir, HeadingAtPlusX), AtlasHeading, FaceSize);',
        unreal.CustomMaterialOutputType.CMOT_FLOAT2,
        ['ViewDir', 'HeadingAtPlusX', 'AtlasHeading', 'FaceSize'],
        ['/TerragenShaders/TerragenUE.ush'])
    for src, pin in ((view, 'ViewDir'), (plus_x, 'HeadingAtPlusX'), (heading, 'AtlasHeading'), (face, 'FaceSize')):
        MEL.connect_material_expressions(src, '', lookup, pin)
    atlas = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -450, 0)
    atlas.set_editor_property('parameter_name', 'Atlas')
    atlas.set_editor_property('texture', rt)
    atlas.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    MEL.connect_material_expressions(lookup, '', atlas, 'UVs')
    decode = custom_node(
        mat, -150, 0, 'Display to linear',
        # The atlas holds display values; decoding as sRGB matches sampling the old sRGB skybox textures.
        'return TG_SRGBToLinear(C.rgb);',
        unreal.CustomMaterialOutputType.CMOT_FLOAT3, ['C'], ['/TerragenShaders/TerragenUE.ush'])
    MEL.connect_material_expressions(atlas, '', decode, 'C')
    MEL.connect_material_property(decode, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_render_target():
    rt = get_or_create('RT_TerragenSky', DEST, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property('size_x', ATLAS_SIZE)
    rt.set_editor_property('size_y', ATLAS_SIZE)
    rt.set_editor_property('render_target_format', unreal.TextureRenderTargetFormat.RTF_RGBA8)
    rt.set_editor_property('clear_color', unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    rt.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    rt.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    EAL.save_loaded_asset(rt)
    return rt


def build_test_map():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    level_path = f'{DEST}/TerragenSkyTest'
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    sky = actors.spawn_actor_from_class(unreal.TerragenSkybox, unreal.Vector(0, 0, 0))
    floor = actors.spawn_actor_from_object(unreal.load_asset('/Engine/BasicShapes/Plane'), unreal.Vector(0, 0, 0))
    floor.set_actor_scale3d(unreal.Vector(200, 200, 1))
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 120))
    # The Terragen sun is at heading 260, altitude 43. With HeadingAtPlusX = 18 that is Unreal yaw 242; the
    # light points away from the sun: yaw 62, pitch -43.
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, -43, 62))
    sun.set_actor_label('Sun (Terragen heading 260, altitude 43)')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.EditorLoadingAndSavingUtils.save_map(world, level_path)
    log(f'saved {level_path}')
    return sky


def gpu_check(sky, rt):
    os.makedirs(OUT, exist_ok=True)
    unreal.TerragenSkybox.finish_compilation()
    for frame in (1, 400):
        start = sky.get_editor_property('terragen_start')
        sky.set_editor_property('terragen_start', unreal.Vector(start.x, start.y, 45.647 + (frame - 1) * 0.067))
        unreal.RenderingLibrary.clear_render_target2d(sky, rt, unreal.LinearColor(0, 0, 0, 1))
        sky.redraw_from_actor_location()
        name = f'Atlas_GPU_{frame:04d}.png'
        unreal.RenderingLibrary.export_render_target(sky, rt, OUT, name)
        # A few raw texels, to check the PNG export against what the render target holds.
        probes = [(1280, 1200), (700, 1700), (1300, 1000)]
        raw = [sky_pixel(sky, rt, x, y) for x, y in probes]
        log(f'frame {frame}: exported {os.path.join(OUT, name)}; raw texels {raw}')
    sky.set_editor_property('terragen_start', unreal.Vector(164.0, 223.0, 45.647))

    # Following the player: 300 m along Unreal +X, and 1 km below the actor (under the Terragen ground, so
    # the camera is lifted to the clearance above it). The Terragen positions go to follow.txt for the
    # CPU comparison.
    with open(os.path.join(OUT, 'follow.txt'), 'w') as f:
        for label, offset in (('side', unreal.Vector(30000.0, 0.0, 0.0)), ('below', unreal.Vector(0.0, 0.0, -100000.0))):
            where = sky.get_actor_location() + offset
            tg = sky.to_terragen(where)
            unreal.RenderingLibrary.clear_render_target2d(sky, rt, unreal.LinearColor(0, 0, 0, 1))
            sky.redraw_from(where)
            unreal.RenderingLibrary.export_render_target(sky, rt, OUT, f'Atlas_GPU_{label}.png')
            f.write(f'{label} {tg.x:.6f} {tg.y:.6f} {tg.z:.6f}\n')
            log(f'{label}: Unreal {where} -> Terragen ({tg.x:.4f}, {tg.y:.4f}, {tg.z:.4f})')

    sky.redraw_from_actor_location()
    ms = sky.measure_redraw_milliseconds(4)
    log(f'full six-face redraw: {ms:.1f} ms')
    with open(os.path.join(OUT, 'timing.txt'), 'w') as f:
        f.write(f'{ms:.2f}\n')


def sky_pixel(ctx, rt, x, y):
    c = unreal.RenderingLibrary.read_render_target_pixel(ctx, rt, x, y)
    return (c.r, c.g, c.b)


def main():
    height = import_texture(os.path.join(ROOT, 'Textures', 'prototype2_v2_height.png'), 'T_TerragenHeight_MountainTop')
    noise = import_texture(os.path.join(ROOT, 'Textures', 'noise128.png'), 'T_TerragenNoise')
    rt = build_render_target()
    build_render_material(height, noise)
    build_display_material(rt)
    sky = build_test_map()
    gpu_check(sky, rt)
    log('done')


try:
    main()
except Exception as e:  # keep the failure in the log, then still quit
    unreal.log_error(f'[TerragenSetup] failed: {e!r}')
    raise
finally:
    if os.environ.get('TG_SETUP_NO_QUIT') != '1':
        unreal.SystemLibrary.quit_editor()
