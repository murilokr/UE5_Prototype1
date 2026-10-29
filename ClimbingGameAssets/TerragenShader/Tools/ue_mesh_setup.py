"""Create the assets that shade ordinary meshes the Terragen way (Shaders/TerragenMesh.ush), check them on the GPU
and take test screenshots.

Runs inside the editor, with the command line of ue_setup.py (that script must have run first for the world):

  UnrealEditor.exe <path>/Prototype1.uproject -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
      -ExecutePythonScript="<path>/ClimbingGameAssets/TerragenShader/Tools/ue_mesh_setup.py" -RenderOffscreen -unattended
      -ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorLoadingSavingSettings]:RestoreOpenAssetTabsOnRestart=NeverRestore

TG_WORLD=<Name> picks the world, as for ue_setup.py (default MountainTop). Creates in /Game/MyStuff/Skybox/Terragen:
  MPC_Terragen                     placement the materials read; ATerragenSkybox writes it (shared by all worlds)
  Textures/T_TerragenWhiteCube     a constant white cubemap for the sky light (shared)
  M_TerragenMesh_<World>           lit: the world's surface layers as BaseColor and bumped Normal, no specular
  M_TerragenMeshUnlit_<World>      Terragen's own light (no mesh shadows); Standalone = 1 for use without the look
  M_TerragenSunLight_<World>       light function for the sun: the clouds' shadow at each lit point
  PP_TerragenLook_<World>          post-process replacing the tonemapper: atmosphere, tone curve, atlas texel grid
  MI_TerragenLookSharp_<World>     the same with Softness 0 (screen resolution)
  PP_TerragenSoften                after Unreal's tonemapper: only the atlas texel grid (prefiltered, jittered per
                                   texel), sky pixels left as they are (no world in it: one for all worlds)
  MI_TerragenSoftenPlain, MI_TerragenSoftenNoJitter   the same without prefilter and jitter / without jitter
  M_TerragenMeshProbe_<World>, M_TerragenMeshCheck_<World>   for this script's measurements
  TerragenMeshTest_<World>         test map: the skybox, lights driven by it, the look, a row of meshes
and fills DA_TerragenWorld_<World>'s light colours and mesh materials. Then:
  - measures the world's sun (through the atmosphere) and ambient at TerragenStart on the GPU
    (M_TerragenMeshProbe) -> mesh/probe.txt, which tg_mesh_test --gpu also checks
  - draws TG_MeshCheckTexel for all four modes -> mesh/mesh_gpu.txt, for
      Build\\tg_mesh_test[_<World>].exe <height.r16> <noise128.r16> [--clouds ...] --gpu Out/ue/<World>/mesh/mesh_gpu.txt
  - times the mesh shading and the look maths per pixel on the GPU -> mesh/timing.txt
  - measures how steady PP_TerragenSoften's variants are when the view turns slightly -> mesh/stability.txt
  - takes viewport screenshots -> mesh/view_*.png (sunlit_* too where the start is in cloud shadow: the same
    shapes moved to the nearest spot in the sun), and compares the sky through the look with the terrain atlas
    texel by texel (it should give the atlas's bytes back) -> mesh/sky_check.txt
Set TG_SETUP_NO_QUIT=1 to keep the editor open afterwards.
"""
import glob
import json
import math
import os
import re
import shutil
import sys
import time

import unreal

DEST = '/Game/MyStuff/Skybox/Terragen'
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
ROOT = os.path.join(PROJECT, 'ClimbingGameAssets', 'TerragenShader')
sys.path.insert(0, os.path.join(ROOT, 'Tools'))
from unpack_cross import read_png_rgb  # noqa: E402

with open(os.path.join(ROOT, 'Tools', 'worlds.json'), encoding='utf-8') as _f:
    WORLDS = {k: v for k, v in json.load(_f).items() if not k.startswith('_')}
WORLD = os.environ.get('TG_WORLD', 'MountainTop')
if WORLD not in WORLDS:
    raise SystemExit(f'TG_WORLD={WORLD}: unknown world; one of {", ".join(WORLDS)}')
SETTINGS = WORLDS[WORLD]
SUFFIX = '' if WORLD == 'MountainTop' else f'_{WORLD}'   # ue_setup.py's assets
OUT = os.path.join(ROOT, 'Out', 'ue', *([WORLD] if SUFFIX else []), 'mesh')
HEADING = SETTINGS['heading']
TERRAIN = os.path.splitext(os.path.basename(SETTINGS['ter']))[0]
HEIGHT_TEXTURE = {'prototype2_v2': 'T_TerragenHeight_MountainTop'}.get(TERRAIN, f'T_TerragenHeight_{TERRAIN}')
FACE_SIZE = 512.0           # ATerragenSkybox's default terrain atlas: 2048 wide, 512-pixel faces
SKY_RADIUS = 1000000.0      # and its SkyRadius, cm
SEGMENTS = 24.0             # atmosphere step budget; covers the sky radius uncapped in both worlds (tg_mesh_test)
INCLUDES = ['/TerragenShaders/TerragenMesh.ush', f'/TerragenShaders/Worlds/{WORLD}.ush']
SHOTS = os.path.join(PROJECT, 'Saved', 'Screenshots', 'WindowsEditor')
GIZMO = 60

tools = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log(f'[TerragenMesh] {msg}')


def editor_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def get_or_create(name, path, cls, factory):
    full = f'{path}/{name}'
    if EAL.does_asset_exist(full):
        return unreal.load_asset(full)
    return tools.create_asset(name, path, cls, factory)


# ---------------------------------------------------------------- shared assets

COLLECTION_SCALARS = {'CentimetresPerUnit': 1500.0, 'HeadingAtPlusX': HEADING, 'AtlasHeading': HEADING,
                      'FaceSize': FACE_SIZE, 'Projection': 0.0, 'SkyRadius': SKY_RADIUS}
COLLECTION_VECTORS = {'TerragenStart': unreal.LinearColor(*SETTINGS['start'], 0.0),
                      'ActorLocation': unreal.LinearColor(0.0, 0.0, 0.0, 0.0)}


def build_collection():
    """MPC_Terragen, the names ATerragenSkybox::UpdateParameterCollection writes. Defaults are only for materials
    shown without a skybox in the level."""
    mpc = get_or_create('MPC_Terragen', DEST, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    scalars = list(mpc.get_editor_property('scalar_parameters'))
    vectors = list(mpc.get_editor_property('vector_parameters'))
    have = {str(p.get_editor_property('parameter_name')) for p in scalars + vectors}
    for name, value in COLLECTION_SCALARS.items():
        if name not in have:
            p = unreal.CollectionScalarParameter()
            p.set_editor_property('parameter_name', name)
            p.set_editor_property('default_value', value)
            scalars.append(p)
    for name, value in COLLECTION_VECTORS.items():
        if name not in have:
            p = unreal.CollectionVectorParameter()
            p.set_editor_property('parameter_name', name)
            p.set_editor_property('default_value', value)
            vectors.append(p)
    mpc.set_editor_property('scalar_parameters', scalars)
    mpc.set_editor_property('vector_parameters', vectors)
    EAL.save_loaded_asset(mpc)
    return mpc


def build_white_cube():
    """A longlat Radiance HDR of constant 1.0, imported as a cubemap: the sky light's radiance before its tint."""
    path = f'{DEST}/Textures/T_TerragenWhiteCube'
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    src = os.path.join(ROOT, 'Out', 'ue', 'T_TerragenWhiteCube.hdr')
    os.makedirs(os.path.dirname(src), exist_ok=True)
    w, h = 64, 32
    with open(src, 'wb') as f:
        f.write(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n' + f'-Y {h} +X {w}\n'.encode())
        f.write(bytes((128, 128, 128, 129)) * (w * h))   # RGBE for 1.0: mantissa 0.5 * 256, exponent 1 + 128
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = f'{DEST}/Textures'
    task.destination_name = 'T_TerragenWhiteCube'
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    cube = unreal.load_asset(path)
    if not isinstance(cube, unreal.TextureCube):
        raise RuntimeError(f'{path} imported as {type(cube).__name__}, not a TextureCube')
    return cube


# ---------------------------------------------------------------- material helpers

class Graph:
    """Builds one material's nodes, left to right."""

    def __init__(self, mat, mpc):
        self.mat, self.mpc, self.y = mat, mpc, -400

    def node(self, cls, x=-1400, **props):
        n = MEL.create_material_expression(self.mat, cls, x, self.y)
        self.y += 110
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, b, pin='', out=''):
        MEL.connect_material_expressions(a, out, b, pin)
        return b

    def collection(self, name):
        return self.node(unreal.MaterialExpressionCollectionParameter, collection=self.mpc, parameter_name=name)

    def scalar(self, name, value):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    def texture(self, name, tex):
        return self.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name, texture=tex,
                         sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)

    def offset_from_actor(self, source):
        """source (an absolute, double-precision position) minus the skybox actor's location, as a plain float3."""
        loc = self.link(self.collection('ActorLocation'), self.node(unreal.MaterialExpressionComponentMask, x=-1200, r=True, g=True, b=True, a=False))
        sub = self.node(unreal.MaterialExpressionSubtract, x=-1000)
        self.link(source, sub, 'A')
        self.link(loc, sub, 'B')
        return self.link(sub, self.node(unreal.MaterialExpressionTruncateLWC, x=-850))

    def custom(self, desc, code, output_type, inputs, outputs=()):
        n = self.node(unreal.MaterialExpressionCustom, x=-500, description=desc, code=code, output_type=output_type,
                      include_file_paths=INCLUDES)
        pins = []
        for name, _ in inputs:
            pin = unreal.CustomInput()
            pin.set_editor_property('input_name', name)
            pins.append(pin)
        n.set_editor_property('inputs', pins)
        extra = []
        for name, kind in outputs:
            o = unreal.CustomOutput()
            o.set_editor_property('output_name', name)
            o.set_editor_property('output_type', kind)
            extra.append(o)
        n.set_editor_property('additional_outputs', extra)
        for name, src in inputs:
            self.link(src, n, name)
        return n


def new_material(name):
    mat = get_or_create(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    return mat


def finish(mat):
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def cloud_input(g, clouds, noise=None):
    """The CloudTex argument and the inputs it needs: the world's cloud map, or NoiseTex for worlds that never read
    one (as ue_setup.py). Pass noise when the material has no NoiseTex input of its own yet."""
    if clouds is None:
        return 'NoiseTex', [('NoiseTex', g.texture('NoiseTex', noise))] if noise is not None else []
    return 'CloudTex', [('CloudTex', g.texture('CloudTex', clouds))]


def mesh_inputs(g, noise):
    """Inputs both mesh materials share: positions as offsets from the skybox, the normal and the placement."""
    return [
        ('Offset', g.offset_from_actor(g.node(unreal.MaterialExpressionWorldPosition))),
        ('CameraOffset', g.offset_from_actor(g.node(unreal.MaterialExpressionCameraPositionWS))),
        ('NormalUE', g.node(unreal.MaterialExpressionVertexNormalWS)),
        ('TerragenStart', g.collection('TerragenStart')),
        ('CentimetresPerUnit', g.collection('CentimetresPerUnit')),
        ('HeadingAtPlusX', g.collection('HeadingAtPlusX')),
        ('FaceSize', g.collection('FaceSize')),
        ('Projection', g.collection('Projection')),
        # 1 = the atlas's own detail; 2 = half of it (cheaper), 0.5 = twice (sharper close up).
        ('DetailScale', g.scalar('DetailScale', 1.0)),
        # Finest bump level; 23 never caps (Terragen's own detail).
        ('MaxLevels', g.scalar('MaxLevels', 23.0)),
        ('NoiseTex', g.texture('NoiseTex', noise)),
    ]


# ---------------------------------------------------------------- the materials

def build_mesh_material(mpc, noise):
    mat = new_material(f'M_TerragenMesh_{WORLD}')
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # The custom normal is in world space.
    mat.set_editor_property('tangent_space_normal', False)
    g = Graph(mat, mpc)
    inputs = mesh_inputs(g, noise)
    code = (f'TGMeshSurface S = TG_MeshMaterialSurface(TGWorld_{WORLD}(), NoiseTex, Offset, CameraOffset, NormalUE, '
            'TerragenStart.xyz, CentimetresPerUnit, HeadingAtPlusX, FaceSize, Projection, DetailScale, MaxLevels);\n'
            'NormalWS = S.Normal;\n'
            'return S.Colour;')
    custom = g.custom('Terragen surface layers', code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs,
                      [('NormalWS', unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
    tint = g.node(unreal.MaterialExpressionVectorParameter, x=-300, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    mul = g.node(unreal.MaterialExpressionMultiply, x=-150)
    g.link(custom, mul, 'A')
    g.link(tint, mul, 'B')
    MEL.connect_material_property(mul, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(custom, 'NormalWS', unreal.MaterialProperty.MP_NORMAL)
    # Terragen's light is diffuse only: Lambert sun plus a constant ambient.
    MEL.connect_material_property(g.node(unreal.MaterialExpressionConstant, x=-150, r=0.0), '', unreal.MaterialProperty.MP_SPECULAR)
    MEL.connect_material_property(g.node(unreal.MaterialExpressionConstant, x=-150, r=1.0), '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(g.node(unreal.MaterialExpressionConstant, x=-150, r=0.0), '', unreal.MaterialProperty.MP_METALLIC)
    return finish(mat)


def build_unlit_material(mpc, height, noise, clouds):
    mat = new_material(f'M_TerragenMeshUnlit_{WORLD}')
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    g = Graph(mat, mpc)
    cloud_arg, cloud_pins = cloud_input(g, clouds)
    inputs = mesh_inputs(g, noise) + [
        ('HeightTex', g.texture('HeightTex', height)),
        # 1 adds the Terragen terrain's shadow (only right where the level lines up with the Terragen ground).
        ('TerrainShadows', g.scalar('TerrainShadows', 0.0)),
        # 1 = atmosphere and tone curve here, for scenes without PP_TerragenLook.
        ('Standalone', g.scalar('Standalone', 0.0)),
        ('Segments', g.scalar('Segments', SEGMENTS)),
    ] + cloud_pins
    code = (f'return TG_MeshMaterialUnlit(TGWorld_{WORLD}(), HeightTex, NoiseTex, {cloud_arg}, Offset, CameraOffset, NormalUE, '
            'TerragenStart.xyz, CentimetresPerUnit, HeadingAtPlusX, FaceSize, Projection, DetailScale, MaxLevels, '
            'TerrainShadows, Standalone, Segments);')
    custom = g.custom('Terragen mesh, Terragen light', code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs)
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


LOOK_CODE = '''TGWorld W = TGWorld_{world}();
float2 uv = ViewportUV;
// This pixel's view direction (Unreal world space): any point on its clip-space ray, from the camera.
float4 onRay = mul(float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0), View.ClipToTranslatedWorld);
float3 dir = normalize(onRay.xyz / onRay.w - View.TranslatedWorldCameraOrigin);
float3 fwd = View.ViewForward;
// Scene colour without Unreal's pre-exposure: the sky sphere's emissive, and meshes lit in Terragen's units.
float3 here = TG_LookSample(W, {clouds}, Scene0.rgb * View.OneOverPreExposure, Depth0.r, dir, fwd, CameraOffset,
                            TerragenStart.xyz, CentimetresPerUnit, HeadingAtPlusX, SkyRadius, Segments);
if (Softness <= 0.0)
{{
	return here;
}}
// The four terrain-atlas texels around this direction: sample the screen where each texel centre is, and blend
// as the sky sphere's bilinear lookup does, so everything is shown at the atlas's resolution, on its grid.
TGAtlasSnap Sn = TG_AtlasSnap(TG_FromUEDirection(dir, HeadingAtPlusX), AtlasHeading, FaceSize, Projection);
float3 D[4];
D[0] = TG_ToUEDirection(Sn.D00, HeadingAtPlusX);
D[1] = TG_ToUEDirection(Sn.D10, HeadingAtPlusX);
D[2] = TG_ToUEDirection(Sn.D01, HeadingAtPlusX);
D[3] = TG_ToUEDirection(Sn.D11, HeadingAtPlusX);
float3 c[4];
[unroll] for (int k = 0; k < 4; k++)
{{
	float4 q = mul(float4(View.TranslatedWorldCameraOrigin + D[k] * 100.0, 1.0), View.TranslatedWorldToClip);
	float2 s = q.w > 0.0 ? float2(q.x / q.w * 0.5 + 0.5, 0.5 - q.y / q.w * 0.5) : uv;
	s = clamp(s, 0.0, 1.0);
	float3 col = SceneTextureLookup(ViewportUVToSceneTextureUV(s, PPI_PostProcessInput0), PPI_PostProcessInput0, false).rgb * View.OneOverPreExposure;
	float depth = SceneTextureLookup(ViewportUVToSceneTextureUV(s, PPI_SceneDepth), PPI_SceneDepth, false).r;
	c[k] = TG_LookSample(W, {clouds}, col, depth, D[k], fwd, CameraOffset, TerragenStart.xyz, CentimetresPerUnit,
	                     HeadingAtPlusX, SkyRadius, Segments);
}}
return lerp(here, TG_SnapBlend(Sn, c[0], c[1], c[2], c[3]), saturate(Softness));'''


def build_look_material(mpc, clouds, noise):
    mat = new_material(f'PP_TerragenLook_{WORLD}')
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property('blendable_location', unreal.BlendableLocation.BL_REPLACING_TONEMAPPER)
    g = Graph(mat, mpc)
    scene = g.node(unreal.MaterialExpressionSceneTexture, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    depth = g.node(unreal.MaterialExpressionSceneTexture, scene_texture_id=unreal.SceneTextureId.PPI_SCENE_DEPTH)
    cloud_arg, cloud_pins = cloud_input(g, clouds, noise)
    inputs = [
        ('ViewportUV', g.node(unreal.MaterialExpressionScreenPosition)),
        ('Scene0', scene),
        ('Depth0', depth),
        ('CameraOffset', g.offset_from_actor(g.node(unreal.MaterialExpressionCameraPositionWS))),
        ('TerragenStart', g.collection('TerragenStart')),
        ('CentimetresPerUnit', g.collection('CentimetresPerUnit')),
        ('HeadingAtPlusX', g.collection('HeadingAtPlusX')),
        ('AtlasHeading', g.collection('AtlasHeading')),
        ('FaceSize', g.collection('FaceSize')),
        ('Projection', g.collection('Projection')),
        ('SkyRadius', g.collection('SkyRadius')),
        # 1 = the terrain atlas's resolution and grid (the "low-res" look); 0 = screen resolution.
        ('Softness', g.scalar('Softness', 1.0)),
        ('Segments', g.scalar('Segments', SEGMENTS)),
    ] + cloud_pins
    custom = g.custom('Terragen look', LOOK_CODE.format(world=WORLD, clouds=cloud_arg), unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs)
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


def build_sun_light_function(mpc, noise, clouds):
    """The directional light's light function: TG_MeshCloudLight at the lit point, so the clouds' shadow moves over
    meshes as it does over the terrain (the light's colour is the sun through the atmosphere)."""
    mat = new_material(f'M_TerragenSunLight_{WORLD}')
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    g = Graph(mat, mpc)
    cloud_arg, cloud_pins = cloud_input(g, clouds, noise)
    inputs = [
        ('Offset', g.offset_from_actor(g.node(unreal.MaterialExpressionWorldPosition))),
        ('TerragenStart', g.collection('TerragenStart')),
        ('CentimetresPerUnit', g.collection('CentimetresPerUnit')),
        ('HeadingAtPlusX', g.collection('HeadingAtPlusX')),
    ] + cloud_pins
    code = (f'float f = TG_MeshMaterialCloudLight(TGWorld_{WORLD}(), {cloud_arg}, Offset, TerragenStart.xyz, '
            'CentimetresPerUnit, HeadingAtPlusX);\nreturn float3(f, f, f);')
    custom = g.custom('Cloud shadow', code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs)
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


SOFTEN_CODE = '''float2 uv = ViewportUV;
float3 here = Scene0.rgb;
// This pixel's view direction (Unreal world space), and its distance along it.
float4 onRay = mul(float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0), View.ClipToTranslatedWorld);
float3 dir = normalize(onRay.xyz / onRay.w - View.TranslatedWorldCameraOrigin);
float dist = Depth0.r / max(dot(dir, View.ViewForward), 1e-4);
// The sky sphere already shows the atlas at its own resolution: leave it exactly as Unreal drew it.
if (Softness <= 0.0 || dist >= 0.98 * SkyRadius)
{
	return here;
}
// The four terrain-atlas texels around this direction, found where their centres are on screen and blended as
// the sky sphere's lookup does. Sky pixels among them blend in too, so silhouettes soften against the sky as the
// terrain's do.
TGAtlasSnap Sn = TG_AtlasSnap(TG_FromUEDirection(dir, HeadingAtPlusX), AtlasHeading, FaceSize, Projection);
float3 D[4];
D[0] = TG_ToUEDirection(Sn.D00, HeadingAtPlusX);
D[1] = TG_ToUEDirection(Sn.D10, HeadingAtPlusX);
D[2] = TG_ToUEDirection(Sn.D01, HeadingAtPlusX);
D[3] = TG_ToUEDirection(Sn.D11, HeadingAtPlusX);
float2 T[4];
T[0] = Sn.T0;
T[1] = float2(Sn.T1.x, Sn.T0.y);
T[2] = float2(Sn.T0.x, Sn.T1.y);
T[3] = Sn.T1;
float2 s[4];
[unroll] for (int k = 0; k < 4; k++)
{
	float4 q = mul(float4(View.TranslatedWorldCameraOrigin + D[k] * 100.0, 1.0), View.TranslatedWorldToClip);
	s[k] = q.w > 0.0 ? float2(q.x / q.w * 0.5 + 0.5, 0.5 - q.y / q.w * 0.5) : uv;
}
// One texel's size and orientation on screen, for placing the jitter and the prefilter taps.
float4 axes = TG_SnapScreenAxes(s[0], s[1], s[2], s[3]);
float3 c[4];
[unroll] for (int k = 0; k < 4; k++)
{
	// Jitter: each texel is read at its own fixed point inside it, as Terragen shades each micro-polygon at a
	// jittered point. Prefilter: four bilinear reads around that point average about the texel's footprint
	// instead of one spot, so fine detail and edges don't shimmer through the snapping.
	float2 j = TG_TexelJitter(T[k], Sn.Size, Jitter);
	float3 sum = float3(0.0, 0.0, 0.0);
	[unroll] for (int t = 0; t < 4; t++)
	{
		float2 p = clamp(TG_SnapTap(s[k], axes, j, Prefilter, t), 0.0, 1.0);
		sum += SceneTextureLookup(ViewportUVToSceneTextureUV(p, PPI_PostProcessInput0), PPI_PostProcessInput0, true).rgb;
	}
	c[k] = sum * 0.25;
}
return lerp(here, TG_SnapBlend(Sn, c[0], c[1], c[2], c[3]), saturate(Softness));'''


def build_soften_material(mpc):
    """The softness alone, after Unreal's tonemapper (display colours, which is what the atlas's bilinear lookup
    blends too). No Terragen world in it, so one material serves every world and any scene."""
    mat = get_or_create('PP_TerragenSoften', DEST, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    g = Graph(mat, mpc)
    inputs = [
        ('ViewportUV', g.node(unreal.MaterialExpressionScreenPosition)),
        ('Scene0', g.node(unreal.MaterialExpressionSceneTexture, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)),
        ('Depth0', g.node(unreal.MaterialExpressionSceneTexture, scene_texture_id=unreal.SceneTextureId.PPI_SCENE_DEPTH)),
        ('HeadingAtPlusX', g.collection('HeadingAtPlusX')),
        ('AtlasHeading', g.collection('AtlasHeading')),
        ('FaceSize', g.collection('FaceSize')),
        ('Projection', g.collection('Projection')),
        ('SkyRadius', g.collection('SkyRadius')),
        ('Softness', g.scalar('Softness', 1.0)),
        # 1 = each texel averages about its footprint on screen (four bilinear reads); 0 = one point.
        ('Prefilter', g.scalar('Prefilter', 1.0)),
        # How far inside its texel each texel is read, in texels: 1 = anywhere in it (Terragen's amount); 0 = the centre.
        ('Jitter', g.scalar('Jitter', 1.0)),
    ]
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    n.set_editor_property('description', 'Terragen softness')
    n.set_editor_property('code', SOFTEN_CODE)
    n.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    n.set_editor_property('include_file_paths', ['/TerragenShaders/TerragenMesh.ush'])
    pins = []
    for name, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    n.set_editor_property('inputs', pins)
    for name, src in inputs:
        g.link(src, n, name)
    MEL.connect_material_property(n, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


def build_soften_instance(soften, name, values):
    """A PP_TerragenSoften instance with some parameters changed, for comparison screenshots."""
    mi = get_or_create(name, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', soften)
    for param, value in values.items():
        MEL.set_material_instance_scalar_parameter_value(mi, param, value)
    EAL.save_loaded_asset(mi)
    return mi


def build_sharp_instance(look):
    mi = get_or_create(f'MI_TerragenLookSharp_{WORLD}', DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', look)
    MEL.set_material_instance_scalar_parameter_value(mi, 'Softness', 0.0)
    EAL.save_loaded_asset(mi)
    return mi


def build_probe_material(height, noise, clouds):
    mat = new_material(f'M_TerragenMeshProbe_{WORLD}')
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    g = Graph(mat, None)
    cloud_arg = 'NoiseTex' if clouds is None else 'CloudTex'
    inputs = [('UV', g.node(unreal.MaterialExpressionTextureCoordinate)),
              ('Start', g.node(unreal.MaterialExpressionVectorParameter, parameter_name='Start', default_value=unreal.LinearColor(*SETTINGS['start'], 0.0))),
              ('NoiseTex', g.texture('NoiseTex', noise))]
    if clouds is not None:
        inputs.append(('CloudTex', g.texture('CloudTex', clouds)))
    code = f'return TG_MeshLightProbe(TGWorld_{WORLD}(), {cloud_arg}, Start.xyz, UV.x < 0.5 ? 0.0 : 1.0).rgb;'
    custom = g.custom('Sun and ambient', code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs)
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


def build_check_material(height, noise, clouds):
    mat = new_material(f'M_TerragenMeshCheck_{WORLD}')
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    g = Graph(mat, None)
    cloud_arg = 'NoiseTex' if clouds is None else 'CloudTex'
    inputs = [('UV', g.node(unreal.MaterialExpressionTextureCoordinate)),
              ('Start', g.node(unreal.MaterialExpressionVectorParameter, parameter_name='Start', default_value=unreal.LinearColor(*SETTINGS['start'], 0.0))),
              ('Mode', g.scalar('Mode', 0.0)),
              ('HeightTex', g.texture('HeightTex', height)),
              ('NoiseTex', g.texture('NoiseTex', noise))]
    if clouds is not None:
        inputs.append(('CloudTex', g.texture('CloudTex', clouds)))
    # 32 x 32 texels over the target; a larger target (for timing) repeats each over a block, the same work per pixel.
    code = (f'return TG_MeshCheckTexel(TGWorld_{WORLD}(), HeightTex, NoiseTex, {cloud_arg}, '
            'floor(UV * 32.0), Start.xyz, Mode);')
    custom = g.custom('Mesh check texel', code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, inputs)
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish(mat)


# ---------------------------------------------------------------- measurements

def render_target(w, h):
    return unreal.RenderingLibrary.create_render_target2d(editor_world(), w, h, unreal.TextureRenderTargetFormat.RTF_RGBA16F,
                                                          unreal.LinearColor(0, 0, 0, 1), False)


def raw(rt, x, y):
    c = unreal.RenderingLibrary.read_render_target_raw_pixel(editor_world(), rt, x, y, False)
    return (c.r, c.g, c.b)


def probe(mat):
    rt = render_target(2, 1)
    unreal.RenderingLibrary.draw_material_to_render_target(editor_world(), rt, mat)
    sun, ambient = raw(rt, 0, 0), raw(rt, 1, 0)
    with open(os.path.join(OUT, 'probe.txt'), 'w') as f:
        f.write(f'sun {sun[0]:.6f} {sun[1]:.6f} {sun[2]:.6f}\nambient {ambient[0]:.6f} {ambient[1]:.6f} {ambient[2]:.6f}\n')
    log(f'sun colour at TerragenStart {sun}, ambient {ambient}')
    return sun, ambient


def gpu_check(mat):
    """TG_MeshCheckTexel on the GPU, read back for the CPU comparison: texel (x, y) at pixel (x, y)."""
    rt = render_target(32, 32)
    mid = unreal.MaterialLibrary.create_dynamic_material_instance(editor_world(), mat)
    lines = ['start {:.6f} {:.6f} {:.6f}'.format(*SETTINGS['start'])]
    for mode in range(4):
        mid.set_scalar_parameter_value('Mode', float(mode))
        unreal.RenderingLibrary.draw_material_to_render_target(editor_world(), rt, mid)
        for y in range(32):
            for x in range(32):
                r, g, b = raw(rt, x, y)
                lines.append(f'{mode} {x} {y} {r:.7g} {g:.7g} {b:.7g}')
    with open(os.path.join(OUT, 'mesh_gpu.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    log(f'wrote {len(lines) - 1} GPU texels to mesh_gpu.txt')


def time_draws(mid, rt, iterations=6):
    unreal.RenderingLibrary.draw_material_to_render_target(editor_world(), rt, mid)
    raw(rt, 0, 0)   # waits for the GPU
    t0 = time.perf_counter()
    for _ in range(iterations):
        unreal.RenderingLibrary.draw_material_to_render_target(editor_world(), rt, mid)
    raw(rt, 0, 0)
    return (time.perf_counter() - t0) * 1000.0 / iterations


def timing(check):
    """Per-pixel GPU cost, as milliseconds for a full 1920 x 1080 screen of it. Modes: 0 the lit material's work
    (layer tree), 2 the unlit material's (plus light and shadow), 3 that plus the look's atmosphere and tone curve
    for one sample, 4 the look's maths alone for four samples (what it does per pixel, less the screen reads).
    The check texels' footprints run from coarse to fine, like a scene from far to near."""
    size = 1024
    rt = render_target(size, size)
    mid = unreal.MaterialLibrary.create_dynamic_material_instance(editor_world(), check)
    results = {}
    # Two rounds, keeping each mode's faster one: the first draws after loading also pay for warming the GPU up.
    for _ in range(2):
        for mode, label in ((0, 'surface (lit material)'), (2, 'surface + light (unlit material)'),
                            (3, 'surface + light + one look sample'), (4, 'look alone (four samples, as per pixel)')):
            mid.set_scalar_parameter_value('Mode', float(mode))
            ms = time_draws(mid, rt) * (1920.0 * 1080.0) / (size * size)
            results[label] = min(ms, results.get(label, ms))
    with open(os.path.join(OUT, 'timing.txt'), 'w') as f:
        for label, ms in results.items():
            f.write(f'{label}: {ms:.2f} ms per full 1080p screen\n')
            log(f'{label}: {ms:.2f} ms per full 1080p screen')


def sunlit_offset(check):
    """Where to put a second group of test meshes in the sun, as an Unreal offset from the skybox in cm, or None
    when the start itself is sunlit. Reads TG_MeshCheckTexel mode 5 (the clouds' share of the sun on a grid
    2.5 units apart around TerragenStart, 0 under the Terragen ground) and takes the nearest grid point in full sun."""
    rt = render_target(32, 32)
    mid = unreal.MaterialLibrary.create_dynamic_material_instance(editor_world(), check)
    mid.set_scalar_parameter_value('Mode', 5.0)
    unreal.RenderingLibrary.draw_material_to_render_target(editor_world(), rt, mid)
    grid = [[raw(rt, x, y)[0] for x in range(32)] for y in range(32)]
    centre = grid[15][15]
    if centre > 0.5:
        return None
    best = None
    for y in range(32):
        for x in range(32):
            # Away from the shadow's edge: this point and its neighbours in full sun.
            if all(grid[min(max(y + dy, 0), 31)][min(max(x + dx, 0), 31)] > 0.95 for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                d = (x - 15.5) ** 2 + (y - 15.5) ** 2
                if best is None or d < best[0]:
                    best = (d, (x - 15.5) * 2.5, (y - 15.5) * 2.5)
    if best is None:
        log(f'start in cloud shadow ({centre:.3f} of the sun) and no sunlit spot within 40 units')
        return None
    # A Terragen offset to Unreal (TG_ToUEDirection): Unreal X is Terragen y turned by HeadingAtPlusX.
    h = math.radians(HEADING)
    tx, ty = best[1], best[2]
    ux, uy = tx * math.sin(h) + ty * math.cos(h), tx * math.cos(h) - ty * math.sin(h)
    offset = unreal.Vector(ux * 1500.0, uy * 1500.0, 0.0)
    log(f'start in cloud shadow ({centre:.3f} of the sun); sunlit test group at Terragen offset ({tx:+.1f}, {ty:+.1f}) units, Unreal {offset}')
    return offset


# ---------------------------------------------------------------- the world asset and the test map

def ush_value(name):
    text = open(os.path.join(ROOT, 'Shaders', 'Worlds', f'{WORLD}.ush'), encoding='utf-8').read()
    return float(re.search(rf'W\.{re.escape(name)} = ([-0-9.e]+);', text).group(1))


def update_world_asset(mesh, unlit, look, sun_function, sun, ambient):
    asset = unreal.load_asset(f'{DEST}/DA_TerragenWorld_{WORLD}')
    asset.set_editor_property('sun_light_function', sun_function)
    asset.set_editor_property('sun_colour', unreal.LinearColor(*sun, 1.0))
    asset.set_editor_property('ambient_colour', unreal.LinearColor(*ambient, 1.0))
    asset.set_editor_property('sun_diameter', ush_value('SunDiameter'))
    asset.set_editor_property('mesh_material', mesh)
    asset.set_editor_property('mesh_unlit_material', unlit)
    asset.set_editor_property('look_material', look)
    EAL.save_loaded_asset(asset)
    return asset


CAMERA = unreal.Vector(-200.0, 0.0, 260.0)
CAMERA_ROTATION = unreal.Rotator(0.0, -12.0, 0.0)   # roll, pitch, yaw


def build_test_map(world_asset, mesh, unlit, look, sunlit):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    sky = actors.spawn_actor_from_class(unreal.TerragenSkybox, unreal.Vector(0, 0, 0))
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500))
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    skylight = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 600))
    skylight.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('sun_light', sun)
    sky.set_editor_property('sky_light', skylight)
    # Turns the sun, gives both lights the world's colours and writes MPC_Terragen.
    sky.set_terragen_world(world_asset)
    actors.spawn_actor_from_class(unreal.PlayerStart, CAMERA)

    volume = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    volume.set_actor_label('Terragen look')
    volume.set_editor_property('unbound', True)
    s = volume.get_editor_property('settings')
    for prop, value in (
            # Terragen has no bounce light, reflections or ambient occlusion.
            ('dynamic_global_illumination_method', unreal.DynamicGlobalIlluminationMethod.NONE),
            ('reflection_method', unreal.ReflectionMethod.NONE),
            ('ambient_occlusion_intensity', 0.0),
            ('motion_blur_amount', 0.0),
            # The look reads the scene unexposed; fixed exposure keeps the scene's own pre-exposure steady.
            ('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL),
            ('auto_exposure_apply_physical_camera_exposure', False),
            ('auto_exposure_bias', 0.0)):
        s.set_editor_property(f'override_{prop}', True)
        s.set_editor_property(prop, value)
    s.set_editor_property('weighted_blendables', unreal.WeightedBlendables([unreal.WeightedBlendable(1.0, look)]))
    volume.set_editor_property('settings', s)

    # The test row: engine shapes and project rocks with the lit material, two shapes unlit, and one sphere with
    # the engine's default material (the look tone-maps everything, not only Terragen meshes).
    placed = []

    def place(asset_path, location, scale, material, label):
        asset = unreal.load_asset(asset_path) if EAL.does_asset_exist(asset_path) else None
        if asset is None:
            log(f'skipped {asset_path} (not found)')
            return
        a = actors.spawn_actor_from_object(asset, location)
        a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        a.set_actor_label(label)
        if material is not None:
            comp = a.get_component_by_class(unreal.StaticMeshComponent)
            for i in range(comp.get_num_materials()):
                comp.set_material(i, material)
        placed.append(a)

    place('/Engine/BasicShapes/Plane', unreal.Vector(1500, 0, 0), 40.0, mesh, 'Floor (lit)')
    place('/Engine/BasicShapes/Sphere', unreal.Vector(700, -330, 100), 2.0, mesh, 'Sphere (lit)')
    place('/Engine/BasicShapes/Cube', unreal.Vector(800, -110, 100), 2.0, mesh, 'Cube (lit)')
    place('/Engine/BasicShapes/Cone', unreal.Vector(800, 110, 100), 2.0, mesh, 'Cone (lit)')
    place('/Engine/BasicShapes/Cylinder', unreal.Vector(700, 330, 100), 2.0, mesh, 'Cylinder (lit)')
    place('/Game/AdvancedLocomotionV4/Environment/Meshes/Complex_Rock', unreal.Vector(1500, -500, 0), 1.0, mesh, 'Rock (lit)')
    place('/Game/StarterContent/Props/SM_Rock', unreal.Vector(1300, 450, 0), 2.0, mesh, 'Starter rock (lit)')
    place('/Engine/BasicShapes/Sphere', unreal.Vector(1100, -150, 100), 2.0, unlit, 'Sphere (unlit)')
    place('/Engine/BasicShapes/Cube', unreal.Vector(1100, 150, 100), 2.0, unlit, 'Cube (unlit)')
    place('/Engine/BasicShapes/Sphere', unreal.Vector(1000, 600, 100), 1.5, None, 'Sphere (default material)')
    if sunlit is not None:
        # The same shapes where the clouds let the sun through (the start can be in their shadow).
        place('/Engine/BasicShapes/Plane', sunlit + unreal.Vector(1500, 0, 0), 40.0, mesh, 'Sunlit floor (lit)')
        place('/Engine/BasicShapes/Sphere', sunlit + unreal.Vector(700, -330, 100), 2.0, mesh, 'Sunlit sphere (lit)')
        place('/Engine/BasicShapes/Cube', sunlit + unreal.Vector(800, -110, 100), 2.0, mesh, 'Sunlit cube (lit)')
        place('/Engine/BasicShapes/Cone', sunlit + unreal.Vector(800, 110, 100), 2.0, mesh, 'Sunlit cone (lit)')
        place('/Game/AdvancedLocomotionV4/Environment/Meshes/Complex_Rock', sunlit + unreal.Vector(1500, -500, 0), 1.0, mesh, 'Sunlit rock (lit)')
        place('/Engine/BasicShapes/Sphere', sunlit + unreal.Vector(1100, 150, 100), 2.0, unlit, 'Sunlit sphere (unlit)')

    level_path = f'{DEST}/TerragenMeshTest_{WORLD}'
    unreal.EditorLoadingAndSavingUtils.save_map(editor_world(), level_path)
    log(f'saved {level_path}')
    return sky, volume, placed


# ---------------------------------------------------------------- screenshots

def set_look(volume, material):
    s = volume.get_editor_property('settings')
    s.set_editor_property('weighted_blendables', unreal.WeightedBlendables([unreal.WeightedBlendable(1.0, material)]) if material else unreal.WeightedBlendables([]))
    volume.set_editor_property('settings', s)


CLOSE = unreal.Vector(450, -150, 170)
CLOSE_ROTATION = unreal.Rotator(0.0, -8.0, -20.0)
# Under half an atlas texel (0.22 degrees at 512-pixel faces): the texel grid stays put, the meshes slide under it.
STABILITY_TURN = 0.1


def views(sky, volume, placed, look, sharp, sunlit, soften, soften_variants):
    """(name, camera location, rotation, size, look material or None, meshes visible). The sky atlas is redrawn
    from each view's camera when that moves."""
    centre = sky.get_actor_location()
    front = unreal.Rotator(0.0, 0.0, 0.0)
    out = [
        ('meshes_look', CAMERA, CAMERA_ROTATION, '1280x720', look, True),
        ('meshes_sharp', CAMERA, CAMERA_ROTATION, '1280x720', sharp, True),
        ('meshes_ue_tonemapper', CAMERA, CAMERA_ROTATION, '1280x720', None, True),
        ('meshes_close_look', unreal.Vector(450, -150, 170), unreal.Rotator(0.0, -8.0, -20.0), '1280x720', look, True),
        ('meshes_soften', CAMERA, CAMERA_ROTATION, '1280x720', soften, True),
        ('meshes_close_soften', CLOSE, CLOSE_ROTATION, '1280x720', soften, True),
        ('meshes_close_soften_plain', CLOSE, CLOSE_ROTATION, '1280x720', soften_variants['plain'], True),
        ('meshes_close_soften_nojitter', CLOSE, CLOSE_ROTATION, '1280x720', soften_variants['nojitter'], True),
        ('sky_front_look', centre, front, '512x512', look, False),
        ('sky_front_sharp', centre, front, '512x512', sharp, False),
        ('sky_front_ue_tonemapper', centre, front, '512x512', None, False),
        ('sky_front_soften', centre, front, '512x512', soften, False),
        # The same view through Unreal's tonemapper again: how much two shots differ anyway (temporal AA).
        ('sky_front_ue_tonemapper_again', centre, front, '512x512', None, False),
    ]
    # Stability: the close view and the same view turned slightly, per variant (and without any softening).
    turned = unreal.Rotator(CLOSE_ROTATION.roll, CLOSE_ROTATION.pitch, CLOSE_ROTATION.yaw + STABILITY_TURN)
    for name, mat in list(soften_variants.items()) + [('none', None)]:
        out += [(f'stability_{name}_a', CLOSE, CLOSE_ROTATION, '1280x720', mat, True),
                (f'stability_{name}_b', CLOSE, turned, '1280x720', mat, True)]
    if sunlit is not None:
        out += [('sunlit_look', sunlit + CAMERA, CAMERA_ROTATION, '1280x720', look, True),
                ('sunlit_ue_tonemapper', sunlit + CAMERA, CAMERA_ROTATION, '1280x720', None, True)]
    return out


state = {'tick': 0, 'step': 0, 'handle': None, 'before': set(), 'views': [], 'names': []}
SETTLE = 60


def tick(_delta):
    try:
        state['tick'] += 1
        if state['tick'] < SETTLE:
            return
        state['tick'] = 0
        step = state['step']
        todo = state['views']
        if step >= len(todo) * 2:
            finish_views()
            return
        name, loc, rot, size, mat, show = todo[step // 2]
        if step % 2 == 0:
            set_look(state['volume'], mat)
            for a in state['placed']:
                a.set_is_temporarily_hidden_in_editor(not show)
            unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(loc, rot)
            if state.get('drawn_at') is None or (loc - state['drawn_at']).length() > 1.0:
                state['sky'].redraw_from(loc)
                state['drawn_at'] = loc
        else:
            unreal.SystemLibrary.execute_console_command(None, f'HighResShot {size}')
            state['names'].append(name)
            log(f'screenshot {name}')
        state['step'] += 1
    except Exception as e:
        unreal.log_error(f'[TerragenMesh] failed: {e!r}')
        stop()


def sky_check(sky):
    """The sky through the look against the terrain atlas's Front face. A 90-degree, 512-pixel view along Unreal +X
    is that face, mirrored left-right in the atlas (Terragen.ush, TG_AtlasDirection): screenshot pixel (x, y) is
    atlas texel (1024 + 511 - x, 768 + y). Compares a 64 x 64 grid of texels, away from the axis gizmo."""
    rt = sky.get_atlas()
    grid = [(x, y) for y in range(4, 512, 8) for x in range(4, 512, 8) if not (x < GIZMO and y >= 512 - GIZMO)]
    atlas = {}
    for x, y in grid:
        c = unreal.RenderingLibrary.read_render_target_pixel(editor_world(), rt, 1024 + 511 - x, 768 + y)
        atlas[(x, y)] = (c.r, c.g, c.b)
    lines = []
    for name in ('sky_front_look', 'sky_front_sharp', 'sky_front_ue_tonemapper'):
        png = os.path.join(OUT, f'view_{name}.png')
        if not os.path.exists(png):
            continue
        w, h, img = read_png_rgb(png)
        diffs = []
        for (x, y), a in atlas.items():
            p = img[(y * w + x) * 3:(y * w + x) * 3 + 3]
            diffs.append(max(abs(p[k] - a[k]) for k in range(3)))
        diffs.sort()
        line = (f'{name}: {len(diffs)} texels, mean {sum(diffs) / len(diffs):.2f}, median {diffs[len(diffs) // 2]}, '
                f'p99 {diffs[int(0.99 * (len(diffs) - 1))]}, max {diffs[-1]} levels vs the atlas')
        lines.append(line)
        log(line)
    # PP_TerragenSoften must leave the sky as Unreal's tonemapper draws it: compare every pixel of the two shots,
    # and, as the noise floor, two shots through Unreal's tonemapper alone.
    for x, y in (('soften', 'ue_tonemapper'), ('ue_tonemapper_again', 'ue_tonemapper')):
        a, b = (os.path.join(OUT, f'view_sky_front_{n}.png') for n in (x, y))
        if not (os.path.exists(a) and os.path.exists(b)):
            continue
        w, h, ia = read_png_rgb(a)
        _, _, ib = read_png_rgb(b)
        d = sorted(max(abs(ia[i + k] - ib[i + k]) for k in range(3)) for i in range(0, w * h * 3, 3))
        line = (f'sky_front_{x} vs sky_front_{y}: {len(d)} pixels, mean {sum(d) / len(d):.3f}, '
                f'{sum(1 for v in d if v == 0) * 100.0 / len(d):.2f}% identical, p99 {d[int(0.99 * (len(d) - 1))]}, max {d[-1]} levels')
        lines.append(line)
        log(line)
    with open(os.path.join(OUT, 'sky_check.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


def stability_check():
    """How much the close view changes when it turns by STABILITY_TURN, per softening variant: the mean change per
    pixel in levels, and how many pixels change by more than 16 levels (the flicker that shows). The meshes slide
    under the fixed texel grid by under half a texel, so a steadier variant changes less; 'none' is Unreal's own
    picture for scale."""
    lines = []
    for name in ('plain', 'nojitter', 'full', 'none'):
        a, b = (os.path.join(OUT, f'view_stability_{name}_{k}.png') for k in 'ab')
        if not (os.path.exists(a) and os.path.exists(b)):
            continue
        w, h, ia = read_png_rgb(a)
        _, _, ib = read_png_rgb(b)
        d = [max(abs(ia[i + k] - ib[i + k]) for k in range(3)) for i in range(0, w * h * 3, 3)]
        line = (f'stability {name}: mean change {sum(d) / len(d):.3f} levels, '
                f'{sum(1 for v in d if v > 16) * 100.0 / len(d):.2f}% of pixels change by more than 16')
        lines.append(line)
        log(line)
    with open(os.path.join(OUT, 'stability.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


def finish_views():
    shots = sorted(set(glob.glob(os.path.join(SHOTS, '*.png'))) - state['before'], key=os.path.getmtime)
    for name, path in zip(state['names'], shots):
        shutil.move(path, os.path.join(OUT, f'view_{name}.png'))
    log(f'saved {len(shots)} screenshots to {OUT}')
    # Leave the map as saved: the look on, the meshes shown.
    set_look(state['volume'], state['look'])
    for a in state['placed']:
        a.set_is_temporarily_hidden_in_editor(False)
    # The sky check reads the atlas as drawn from the skybox, where the sky views were taken.
    state['sky'].redraw_from_actor_location()
    sky_check(state['sky'])
    stability_check()
    log('done')
    stop()


def stop():
    if state['handle'] is not None:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        state['handle'] = None
    if os.environ.get('TG_SETUP_NO_QUIT') != '1':
        unreal.SystemLibrary.quit_editor()


def main():
    os.makedirs(OUT, exist_ok=True)
    log(f'world {WORLD}')
    height = unreal.load_asset(f'{DEST}/Textures/{HEIGHT_TEXTURE}')
    noise = unreal.load_asset(f'{DEST}/Textures/T_TerragenNoise')
    clouds = unreal.load_asset(f'{DEST}/Textures/T_TerragenClouds_{WORLD}') if SETTINGS['clouds'] else None
    if height is None or noise is None or (SETTINGS['clouds'] and clouds is None):
        raise RuntimeError('missing Terragen textures: run Tools/ue_setup.py for this world first')
    mpc = build_collection()
    build_white_cube()
    mesh = build_mesh_material(mpc, noise)
    unlit = build_unlit_material(mpc, height, noise, clouds)
    look = build_look_material(mpc, clouds, noise)
    sharp = build_sharp_instance(look)
    soften = build_soften_material(mpc)
    soften_variants = {
        'plain': build_soften_instance(soften, 'MI_TerragenSoftenPlain', {'Prefilter': 0.0, 'Jitter': 0.0}),
        'nojitter': build_soften_instance(soften, 'MI_TerragenSoftenNoJitter', {'Jitter': 0.0}),
        'full': soften,
    }
    sun_function = build_sun_light_function(mpc, noise, clouds)
    probe_mat = build_probe_material(height, noise, clouds)
    check = build_check_material(height, noise, clouds)
    unreal.TerragenSkybox.finish_compilation()
    for m in (mesh, unlit, look, soften, sun_function, probe_mat, check):
        stats = MEL.get_statistics(m)
        log(f'{m.get_name()}: {stats.num_pixel_shader_instructions} pixel shader instructions')

    sun, ambient = probe(probe_mat)
    gpu_check(check)
    timing(check)
    world_asset = update_world_asset(mesh, unlit, look, sun_function, sun, ambient)
    sunlit = sunlit_offset(check)
    sky, volume, placed = build_test_map(world_asset, mesh, unlit, look, sunlit)
    unreal.TerragenSkybox.finish_compilation()
    sky.redraw_from_actor_location()

    state.update(sky=sky, volume=volume, placed=placed, look=look, views=views(sky, volume, placed, look, sharp, sunlit, soften, soften_variants),
                 before=set(glob.glob(os.path.join(SHOTS, '*.png'))))
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle'] = unreal.register_slate_post_tick_callback(tick)


try:
    main()
except Exception as e:
    unreal.log_error(f'[TerragenMesh] failed: {e!r}')
    stop()
    raise
