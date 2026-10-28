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
  M_TerragenRender        the terrain pass: one terrain atlas texel (TG_RenderAtlasTexelComposite, Custom node)
  M_TerragenRenderSky     the sky pass (Mode 0) and the reflection-sky pass (Mode 1), TG_RenderAtlasTexelSky
  RT_TerragenSkyOnly, RT_TerragenReflSky   defaults for the terrain pass's sky and reflection atlas inputs
  M_TerragenSkyDisplay    sky-sphere material: looks the view direction up in RT_TerragenSky
  DA_TerragenWorld_MountainTop   the world asset (UTerragenWorld) ATerragenSkybox.TerragenWorld points at
  TerragenSkyTest         a small test map with a floor, a player start and an ATerragenSkybox

Then it renders the atlas for MountainTop frames 1 and 400, times a full redraw, and exports
Out/ue/Atlas_GPU_0001.png and Atlas_GPU_0400.png for comparison with the CPU harness. Set the environment
variable TG_SETUP_NO_QUIT=1 to keep the editor open afterwards.

Other worlds: set TG_WORLD=<Name> to build another world from Tools/worlds.json (after Tools/build_world.py
has generated its shader include), next to the MountainTop assets and without touching them. For example
TG_WORLD=Prototype2World creates, with the world's name as a suffix:
  Textures/T_TerragenClouds_<World>   the cloud map (Tools/make_cloud_map.py), 8-bit greyscale read with Load
  M_TerragenRender_<World>            TG_RenderAtlasTexel with the CloudTex input
  M_TerragenSkyDisplay_<World>, RT_TerragenSky_<World>
  DA_TerragenWorld_<World>            the world asset; point any ATerragenSkybox at it to show this world
  TerragenSkyTest_<World>             test map whose ATerragenSkybox uses it
and exports Out/ue/<World>/Atlas_GPU_0001.png and timing.txt. Worlds on the same terrain share its height
texture, and the noise texture is shared by all; those are only imported if missing.
"""
import json
import os

import unreal

DEST = '/Game/MyStuff/Skybox/Terragen'
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
ROOT = os.path.join(PROJECT, 'ClimbingGameAssets', 'TerragenShader')
ATLAS_SIZE = 2048       # ATerragenSkybox's TerrainAtlasSize default
SKY_ATLAS_SIZE = 1024   # its SkyAtlasSize
REFL_ATLAS_SIZE = 512   # and its ReflectionAtlasSize

# Per world (Tools/worlds.json): the Terragen camera of frame 1 (the actor's TerragenStart), the heading along
# Unreal +X, the sun for the test map's light and the frames gpu_check renders (GenSkyboxAnimation.tgs raises
# the camera 0.067 units per frame).
with open(os.path.join(ROOT, 'Tools', 'worlds.json'), encoding='utf-8') as _f:
    WORLDS = {k: v for k, v in json.load(_f).items() if not k.startswith('_')}
WORLD = os.environ.get('TG_WORLD', 'MountainTop')
if WORLD not in WORLDS:
    raise SystemExit(f'TG_WORLD={WORLD}: unknown world; one of {", ".join(WORLDS)}')
SETTINGS = WORLDS[WORLD]
# MountainTop's assets came first and have no suffix.
SUFFIX = '' if WORLD == 'MountainTop' else f'_{WORLD}'
OUT = os.path.join(ROOT, 'Out', 'ue') if not SUFFIX else os.path.join(ROOT, 'Out', 'ue', WORLD)
HEADING = SETTINGS['heading']   # HeadingAtPlusX and AtlasHeading
TERRAIN = os.path.splitext(os.path.basename(SETTINGS['ter']))[0]
# The first terrain's texture kept the name of the first world that used it.
HEIGHT_TEXTURE = {'prototype2_v2': 'T_TerragenHeight_MountainTop'}.get(TERRAIN, f'T_TerragenHeight_{TERRAIN}')

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
    # Exact values: no sRGB, uncompressed greyscale (G16 for 16-bit sources, G8 for 8-bit), no mips, no streaming.
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = f'{DEST}/Textures'
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    tools.import_asset_tasks([task])
    tex = unreal.load_asset(f'{DEST}/Textures/{name}')
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


def shared_texture(src, name):
    """A texture the MountainTop assets also use: a sky-world run only imports it when it is missing."""
    path = f'{DEST}/Textures/{name}'
    if SUFFIX and EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    return import_texture(src, name)


def texture_object_param(mat, name, tex, x, y, sampler=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE):
    node = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('texture', tex)
    node.set_editor_property('sampler_type', sampler)
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


def camera_param(mat, x, y, name='CameraPos'):
    cam = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    cam.set_editor_property('parameter_name', name)
    cam.set_editor_property('default_value', unreal.LinearColor(*SETTINGS['start'], 0.0))
    return cam


def finish_material(mat, desc, code, names, sources):
    custom = custom_node(
        mat, -450, 0, desc, code, unreal.CustomMaterialOutputType.CMOT_FLOAT4, names,
        ['/TerragenShaders/TerragenUE.ush', f'/TerragenShaders/Worlds/{WORLD}.ush'])
    for src, pin in zip(sources, names):
        MEL.connect_material_expressions(src, '', custom, pin)
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_render_material(height, noise, clouds, sky_rt, refl_rt):
    """The terrain pass: terrain from TG_RenderAtlasTexelTerrain, rays that miss it filled from the sky atlas, water
    reflections of the sky read from the reflection atlas."""
    mat = get_or_create(f'M_TerragenRender{SUFFIX}', DEST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    names = ['UV', 'HeightTex', 'NoiseTex', 'CameraPos', 'AtlasHeading', 'FaceSize', 'StartDistance', 'TerrainClearance',
             'SkyAtlas', 'SkyFaceSize', 'ReflSky', 'ReflFaceSize', 'ReflCameraPos', 'Projection']
    sources = [
        MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -200),
        texture_object_param(mat, 'HeightTex', height, -900, -60),
        texture_object_param(mat, 'NoiseTex', noise, -900, 120),
        camera_param(mat, -900, 300),
        scalar_param(mat, 'AtlasHeading', HEADING, -900, 520),
        scalar_param(mat, 'FaceSize', ATLAS_SIZE / 4, -900, 620),
        scalar_param(mat, 'StartDistance', 0.0, -900, 720),
        # Terragen units; negative = off. ATerragenSkybox sets it from TerrainClearance (cm).
        scalar_param(mat, 'TerrainClearance', 0.1, -900, 820),
        # ATerragenSkybox sets both to its sky render target; the asset is only the default.
        texture_object_param(mat, 'SkyAtlas', sky_rt, -900, 920, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),
        scalar_param(mat, 'SkyFaceSize', SKY_ATLAS_SIZE / 4, -900, 1120),
        # Read with Load, so the sampler type only has to suit the texture. ReflFaceSize 0 (the default, for the
        # material on its own) computes reflections exactly; the actor sets the size of its reflection atlas.
        texture_object_param(mat, 'ReflSky', refl_rt, -900, 1220, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),
        scalar_param(mat, 'ReflFaceSize', 0.0, -900, 1420),
        camera_param(mat, -900, 1520, 'ReflCameraPos'),
        # The terrain atlas's layout, 0 cube cross or 1 sphere map; the actor sets it from its Projection.
        scalar_param(mat, 'Projection', 0.0, -900, 1620),
    ]
    cloud_arg = 'NoiseTex'   # never read when the world has no sky and no cloud shadows (TerragenUE.ush)
    if clouds is not None:
        # The actor doesn't set CloudTex; the parameter's default is the world's cloud map.
        names.append('CloudTex')
        sources.append(texture_object_param(mat, 'CloudTex', clouds, -900, 1720))
        cloud_arg = 'CloudTex'
    code = (f'return TG_RenderAtlasTexelComposite(TGWorld_{WORLD}(), HeightTex, NoiseTex, {cloud_arg}, SkyAtlas, '
            'SkyAtlasSampler, ReflSky, UV, CameraPos.xyz, ReflCameraPos.xyz, AtlasHeading, FaceSize, SkyFaceSize, '
            'ReflFaceSize, StartDistance, TerrainClearance, Projection);')
    return finish_material(mat, 'Terragen terrain texel', code, names, sources)


def build_sky_material(height, clouds):
    """The sky pass: only the sky (clouds, atmosphere, sun), at its own resolution and quality. Mode 1 is the
    reflection-sky pass instead (linear colour, for the actor's floating-point reflection atlas)."""
    mat = get_or_create(f'M_TerragenRenderSky{SUFFIX}', DEST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    names = ['UV', 'HeightTex', 'CameraPos', 'AtlasHeading', 'FaceSize', 'TerrainClearance', 'CloudQuality',
             'AtmosQuality', 'StartDistance', 'Mode']
    sources = [
        MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -200),
        texture_object_param(mat, 'HeightTex', height, -900, -60),
        camera_param(mat, -900, 300),
        scalar_param(mat, 'AtlasHeading', HEADING, -900, 520),
        scalar_param(mat, 'FaceSize', SKY_ATLAS_SIZE / 4, -900, 620),
        scalar_param(mat, 'TerrainClearance', 0.1, -900, 820),
        scalar_param(mat, 'CloudQuality', 1.0, -900, 920),
        scalar_param(mat, 'AtmosQuality', 1.0, -900, 1020),
        scalar_param(mat, 'StartDistance', 0.0, -900, 1120),
        scalar_param(mat, 'Mode', 0.0, -900, 1220),
    ]
    cloud_arg = 'HeightTex'   # never read without the sky
    if clouds is not None:
        names.append('CloudTex')
        sources.append(texture_object_param(mat, 'CloudTex', clouds, -900, 1320))
        cloud_arg = 'CloudTex'
    code = (f'TGWorld W = TG_SkyQuality(TGWorld_{WORLD}(), CloudQuality, AtmosQuality);\n'
            f'return TG_RenderAtlasTexelSky(W, HeightTex, {cloud_arg}, UV, CameraPos.xyz, AtlasHeading, FaceSize, '
            'StartDistance, TerrainClearance, Mode);')
    return finish_material(mat, 'Terragen sky texel', code, names, sources)


def build_display_material(rt):
    mat = get_or_create(f'M_TerragenSkyDisplay{SUFFIX}', DEST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('is_sky', True)
    view = MEL.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -1100, 0)
    plus_x = scalar_param(mat, 'HeadingAtPlusX', HEADING, -1100, 120)
    heading = scalar_param(mat, 'AtlasHeading', HEADING, -1100, 220)
    face = scalar_param(mat, 'FaceSize', ATLAS_SIZE / 4, -1100, 320)
    projection = scalar_param(mat, 'Projection', 0.0, -1100, 420)
    lookup = custom_node(
        mat, -750, 0, 'Atlas UV for view direction',
        # CameraVector points from the pixel to the camera; the view direction is the opposite.
        'return TG_ProjectionUV(TG_FromUEDirection(-ViewDir, HeadingAtPlusX), AtlasHeading, FaceSize, Projection);',
        unreal.CustomMaterialOutputType.CMOT_FLOAT2,
        ['ViewDir', 'HeadingAtPlusX', 'AtlasHeading', 'FaceSize', 'Projection'],
        ['/TerragenShaders/TerragenUE.ush'])
    for src, pin in ((view, 'ViewDir'), (plus_x, 'HeadingAtPlusX'), (heading, 'AtlasHeading'), (face, 'FaceSize'),
                     (projection, 'Projection')):
        MEL.connect_material_expressions(src, '', lookup, pin)
    atlas = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -450, 0)
    atlas.set_editor_property('parameter_name', 'Atlas')
    atlas.set_editor_property('texture', rt)
    atlas.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    # The atlas has one mip, and its UVs jump at face edges and at the sphere map's seam. Sampling level 0
    # directly keeps screen derivatives (and anisotropic filtering along them) out of it.
    atlas.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
    atlas.set_editor_property('const_mip_value', 0)
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


def build_render_target(name=None, size=ATLAS_SIZE, fmt=unreal.TextureRenderTargetFormat.RTF_RGBA8):
    rt = get_or_create(name or f'RT_TerragenSky{SUFFIX}', DEST, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property('size_x', size)
    rt.set_editor_property('size_y', size)
    rt.set_editor_property('render_target_format', fmt)
    rt.set_editor_property('clear_color', unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    rt.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    rt.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    EAL.save_loaded_asset(rt)
    return rt


def build_world_asset(render_mat, sky_mat, display_mat):
    """DA_TerragenWorld_<World>: what ATerragenSkybox needs to show this world."""
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.TerragenWorld)
    asset = get_or_create(f'DA_TerragenWorld_{WORLD}', DEST, unreal.TerragenWorld, factory)
    asset.set_editor_property('display_name', unreal.Text(SETTINGS.get('display_name', WORLD)))
    asset.set_editor_property('render_material', render_mat)
    asset.set_editor_property('sky_render_material', sky_mat)
    asset.set_editor_property('display_material', display_mat)
    asset.set_editor_property('terragen_start', unreal.Vector(*SETTINGS['start']))
    asset.set_editor_property('heading_at_plus_x', HEADING)
    asset.set_editor_property('atlas_heading', HEADING)
    asset.set_editor_property('sun_heading', SETTINGS['sun'][0])
    asset.set_editor_property('sun_altitude', SETTINGS['sun'][1])
    EAL.save_loaded_asset(asset)
    return asset


def build_test_map(world_asset):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    level_path = f'{DEST}/TerragenSkyTest{SUFFIX}'
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    sky = actors.spawn_actor_from_class(unreal.TerragenSkybox, unreal.Vector(0, 0, 0))
    floor = actors.spawn_actor_from_object(unreal.load_asset('/Engine/BasicShapes/Plane'), unreal.Vector(0, 0, 0))
    floor.set_actor_scale3d(unreal.Vector(200, 200, 1))
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 120))
    # A Terragen heading H is Unreal yaw H - HeadingAtPlusX; the light points away from the sun. MountainTop's
    # sun (heading 260, altitude 43) gives yaw 62, pitch -43.
    sun_heading, sun_altitude = SETTINGS['sun']
    yaw = (sun_heading - HEADING) % 360.0 - 180.0
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500),
                                        unreal.Rotator(0, -sun_altitude, yaw))
    sun.set_actor_label(f'Sun (Terragen heading {sun_heading:g}, altitude {sun_altitude:g})')
    # The actor creates its material instances and render targets on first use, so setting the world (which
    # also turns the light to its sun) before that is enough.
    sky.set_editor_property('sun_light', sun)
    sky.set_terragen_world(world_asset)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.EditorLoadingAndSavingUtils.save_map(world, level_path)
    log(f'saved {level_path}')
    return sky


def gpu_check(sky):
    os.makedirs(OUT, exist_ok=True)
    unreal.TerragenSkybox.finish_compilation()
    x0, y0, z0 = SETTINGS['start']
    for frame in SETTINGS['frames']:
        sky.set_editor_property('terragen_start', unreal.Vector(x0, y0, z0 + (frame - 1) * 0.067))
        sky.redraw_from_actor_location()
        rt = sky.get_atlas()
        name = f'Atlas_GPU_{frame:04d}.png'
        unreal.RenderingLibrary.export_render_target(sky, rt, OUT, name)
        unreal.RenderingLibrary.export_render_target(sky, sky.get_sky_atlas(), OUT, f'SkyAtlas_GPU_{frame:04d}.png')
        # A few raw texels, to check the PNG export against what the render target holds.
        probes = [(1280, 1200), (700, 1700), (1300, 1000)]
        raw = [sky_pixel(sky, rt, x, y) for x, y in probes]
        log(f'frame {frame}: exported {os.path.join(OUT, name)}; raw texels {raw}')
    sky.set_editor_property('terragen_start', unreal.Vector(x0, y0, z0))

    # Following the player: 300 m along Unreal +X, and 1 km below the actor (under the Terragen ground, so
    # the camera is lifted to the clearance above it). The Terragen positions go to follow.txt for the
    # CPU comparison.
    with open(os.path.join(OUT, 'follow.txt'), 'w') as f:
        for label, offset in (('side', unreal.Vector(30000.0, 0.0, 0.0)), ('below', unreal.Vector(0.0, 0.0, -100000.0))):
            where = sky.get_actor_location() + offset
            tg = sky.to_terragen(where)
            sky.redraw_from(where)
            unreal.RenderingLibrary.export_render_target(sky, sky.get_atlas(), OUT, f'Atlas_GPU_{label}.png')
            f.write(f'{label} {tg.x:.6f} {tg.y:.6f} {tg.z:.6f}\n')
            log(f'{label}: Unreal {where} -> Terragen ({tg.x:.4f}, {tg.y:.4f}, {tg.z:.4f})')

    sky.redraw_from_actor_location()
    # A player looking straight ahead (Unreal +X) with a 90-degree, 16:9 view.
    ahead = (sky.get_actor_location(), unreal.Rotator(0, 0, 0), 90.0, 16.0 / 9.0)
    timings = {
        'full': sky.measure_redraw_milliseconds(4),
        'sky_pass': sky.measure_pass_milliseconds(True, 4),
        'reflection_pass': sky.measure_reflection_milliseconds(4),
        'terrain_pass': sky.measure_pass_milliseconds(False, 4),
        'visible_tiles_16x9_fov90': sky.measure_visible_milliseconds(*ahead, 4),
    }
    # The actor's defaults are CloudQuality 0.5 and AtmosQuality 1 (the world's own); other settings for comparison.
    for cloud, atmos in ((1.0, 1.0), (0.5, 0.5), (0.25, 1.0)):
        sky.set_editor_property('cloud_quality', cloud)
        sky.set_editor_property('atmos_quality', atmos)
        timings[f'sky_pass_cloud_{cloud}_atmos_{atmos}'] = sky.measure_pass_milliseconds(True, 4)
    sky.set_editor_property('cloud_quality', 0.5)
    sky.set_editor_property('atmos_quality', 1.0)
    # Reflections computed exactly in the terrain pass, and a sharper 4096 terrain atlas (8 tiles per face side
    # keeps the tiles at 128 pixels), each on a temporary actor.
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for label, props in (('exact_reflections', {'reflection_atlas_size': 0}),
                         ('atlas_4096', {'terrain_atlas_size': 4096, 'tiles_per_face_side': 8}),
                         ('sphere_map', {'projection': unreal.TerragenProjection.SPHERE})):
        temp = actors.spawn_actor_from_class(unreal.TerragenSkybox, sky.get_actor_location())
        for prop in ('render_material', 'sky_render_material', 'display_material', 'terragen_start',
                     'heading_at_plus_x', 'atlas_heading'):
            temp.set_editor_property(prop, sky.get_editor_property(prop))
        for prop, value in props.items():
            temp.set_editor_property(prop, value)
        temp.redraw_from_actor_location()
        timings[f'{label}_terrain_pass'] = temp.measure_pass_milliseconds(False, 4)
        timings[f'{label}_visible_tiles_16x9_fov90'] = temp.measure_visible_milliseconds(*ahead, 4)
        actors.destroy_actor(temp)
    with open(os.path.join(OUT, 'timing.txt'), 'w') as f:
        for name, ms in timings.items():
            f.write(f'{name} {ms:.2f}\n')
            log(f'{name}: {ms:.1f} ms')


def sky_pixel(ctx, rt, x, y):
    c = unreal.RenderingLibrary.read_render_target_pixel(ctx, rt, x, y)
    return (c.r, c.g, c.b)


def main():
    log(f'world {WORLD}')
    # Both worlds use prototype2_v2.ter, so they share the height texture.
    height = shared_texture(os.path.join(ROOT, 'Textures', f'{TERRAIN}_height.png'), HEIGHT_TEXTURE)
    noise = shared_texture(os.path.join(ROOT, 'Textures', 'noise128.png'), 'T_TerragenNoise')
    clouds = None
    if SETTINGS['clouds']:
        clouds = import_texture(os.path.join(ROOT, 'Textures', f'{WORLD}_clouds.png'), f'T_TerragenClouds_{WORLD}')
    # The render target assets are only defaults for the materials' texture parameters; the actor makes its own.
    rt = build_render_target()
    sky_rt = build_render_target(f'RT_TerragenSkyOnly{SUFFIX}', SKY_ATLAS_SIZE)
    refl_rt = build_render_target(f'RT_TerragenReflSky{SUFFIX}', REFL_ATLAS_SIZE, unreal.TextureRenderTargetFormat.RTF_RGBA16F)
    render_mat = build_render_material(height, noise, clouds, sky_rt, refl_rt)
    sky_mat = build_sky_material(height, clouds)
    display_mat = build_display_material(rt)
    sky = build_test_map(build_world_asset(render_mat, sky_mat, display_mat))
    gpu_check(sky)
    log('done')


try:
    main()
except Exception as e:  # keep the failure in the log, then still quit
    unreal.log_error(f'[TerragenSetup] failed: {e!r}')
    raise
finally:
    if os.environ.get('TG_SETUP_NO_QUIT') != '1':
        unreal.SystemLibrary.quit_editor()
