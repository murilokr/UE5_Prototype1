"""What the ue_*.py tools share: where the plugin and its content are, and which world this run is for.

The tools run inside the Unreal Editor (see ue_setup.py for the command line). TG_WORLD=<Name> picks the world
from Tools/worlds.json; the default is MountainTop.

Content layout, under the plugin's /Terragen mount point:
  Shared/                  MPC_Terragen (the placement ATerragenSkybox writes for the mesh materials)
  Shared/Textures/         T_TerragenNoise, T_TerragenWhiteCube, T_TerragenHeight_<terrain> (one per .ter)
  Shared/PostProcess/      PP_TerragenSoften and its instances: no world compiled in, one for all worlds
  Worlds/<World>/          everything compiled for one world, each name ending in _<World>, and its DA_TerragenWorld
  Worlds/<World>/Debug/    the setup scripts' measurement materials
  Maps/                    TerragenSkyTest_<World> and TerragenMeshTest_<World>
"""
import json
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # the plugin's folder
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SHOTS = os.path.join(PROJECT, 'Saved', 'Screenshots', 'WindowsEditor')
DATA_TEXTURES = os.path.join(ROOT, 'Data', 'Textures')

CONTENT = '/Terragen'
SHARED = f'{CONTENT}/Shared'
SHARED_TEXTURES = f'{SHARED}/Textures'
POST_PROCESS = f'{SHARED}/PostProcess'
MAPS = f'{CONTENT}/Maps'

with open(os.path.join(ROOT, 'Tools', 'worlds.json'), encoding='utf-8') as _f:
    WORLDS = {k: v for k, v in json.load(_f).items() if not k.startswith('_')}
WORLD = os.environ.get('TG_WORLD', 'MountainTop')
if WORLD not in WORLDS:
    raise SystemExit(f'TG_WORLD={WORLD}: unknown world; one of {", ".join(WORLDS)}')
SETTINGS = WORLDS[WORLD]
HEADING = SETTINGS['heading']   # HeadingAtPlusX and AtlasHeading
TERRAIN = os.path.splitext(os.path.basename(SETTINGS['ter']))[0]

WORLD_DIR = f'{CONTENT}/Worlds/{WORLD}'
DEBUG_DIR = f'{WORLD_DIR}/Debug'
SKY_TEST_MAP = f'{MAPS}/TerragenSkyTest_{WORLD}'
MESH_TEST_MAP = f'{MAPS}/TerragenMeshTest_{WORLD}'
# Worlds on the same terrain share its height texture.
HEIGHT_TEXTURE = f'T_TerragenHeight_{TERRAIN}'
OUT = os.path.join(ROOT, 'Out', 'ue', WORLD)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def world_asset_name(prefix):
    """A per-world asset's name: M_TerragenRender -> M_TerragenRender_<World>."""
    return f'{prefix}_{WORLD}'


def get_or_create(name, path, cls, factory):
    full = f'{path}/{name}'
    if EAL.does_asset_exist(full):
        return unreal.load_asset(full)
    return tools.create_asset(name, path, cls, factory)


def quit_unless_kept():
    """-ExecutePythonScript runs leave the editor open with TG_SETUP_NO_QUIT=1."""
    if os.environ.get('TG_SETUP_NO_QUIT') != '1':
        unreal.SystemLibrary.quit_editor()
