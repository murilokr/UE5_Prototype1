"""Find where a world's GPU time goes: time each pass with one feature switched off at a time.

Runs inside the editor like ue_setup.py (TG_WORLD selects the world; run ue_setup.py for it first). It copies
the world's render materials into a temporary folder, edits each copy's Custom node to change one TGWorld
field before rendering, times the pass with ATerragenSkybox.measure_pass_milliseconds on the test map, writes
Out/ue/<World>/profile.txt, and deletes the copies. Nothing else is saved.
"""
import os

import unreal

DEST = '/Game/MyStuff/Skybox/Terragen'
TEMP = f'{DEST}/ProfileTemp'
WORLD = os.environ.get('TG_WORLD', 'MountainTop')
SUFFIX = '' if WORLD == 'MountainTop' else f'_{WORLD}'
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, 'ClimbingGameAssets', 'TerragenShader', 'Out', 'ue', *([WORLD] if SUFFIX else []))
WORLD_CALL = f'TGWorld_{WORLD}()'

# (pass, label, statements run on `TGWorld W` before rendering, Terragen.ush profiling switches)
VARIANTS = [
    ('terrain', 'baseline', '', {}),
    ('terrain', 'no water shading (flat lit water)', 'W.WaterShading = 0.0;', {}),
    ('terrain', 'water: no waves', '', {'TG_PROFILE_NO_WAVES': '1'}),
    ('terrain', 'water: no reflection sky', '', {'TG_PROFILE_NO_REFL_SKY': '1'}),
    ('terrain', 'water: no bed (refraction)', '', {'TG_PROFILE_WATER_TRACES': '2'}),
    ('terrain', 'water: no reflection, no bed', '', {'TG_PROFILE_WATER_TRACES': '1'}),
    ('terrain', 'no atmosphere', 'W.Atmos.On = 0.0;', {}),
    ('sky', 'baseline', '', {}),
    ('sky', 'no atmosphere', 'W.Atmos.On = 0.0;', {}),
    ('refl', 'baseline', '', {}),
    ('refl', 'no atmosphere', 'W.Atmos.On = 0.0;', {}),
]

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log(f'[TerragenProfile] {msg}')


def variant_material(source_path, name, prelude, defines):
    path = f'{TEMP}/{name}'
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = EAL.duplicate_asset(source_path, path)
    node = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    code = node.get_editor_property('code')
    if 'TGWorld W =' in code:
        # The sky material starts with its TGWorld line; the switches go right after it.
        first, rest = code.split('\n', 1)
        code = f'{first}\n{prelude}\n{rest}'
    else:
        code = f'TGWorld W = {WORLD_CALL};\n{prelude}\n' + code.replace(WORLD_CALL, 'W')
    node.set_editor_property('code', code)
    extra = []
    for k, v in defines.items():
        d = unreal.CustomDefine()
        d.set_editor_property('define_name', k)
        d.set_editor_property('define_value', v)
        extra.append(d)
    node.set_editor_property('additional_defines', extra)
    MEL.recompile_material(mat)
    return mat


def main():
    os.makedirs(OUT, exist_ok=True)
    unreal.EditorLoadingAndSavingUtils.load_map(f'{DEST}/TerragenSkyTest{SUFFIX}')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    template = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.TerragenSkybox)][0]
    sky_path = f'{DEST}/M_TerragenRenderSky{SUFFIX}'
    sources = {'terrain': f'{DEST}/M_TerragenRender{SUFFIX}', 'sky': sky_path, 'refl': sky_path}
    mats = [(p, label, variant_material(sources[p], f'P{i}', prelude, defines))
            for i, (p, label, prelude, defines) in enumerate(VARIANTS)]
    unreal.TerragenSkybox.finish_compilation()

    lines = []
    for p, label, mat in mats:
        sky = actors.spawn_actor_from_class(unreal.TerragenSkybox, template.get_actor_location())
        for prop in ('render_material', 'sky_render_material', 'display_material', 'terragen_start'):
            sky.set_editor_property(prop, template.get_editor_property(prop))
        sky.set_editor_property('render_material' if p == 'terrain' else 'sky_render_material', mat)
        sky.redraw_from_actor_location()
        ms = sky.measure_reflection_milliseconds(4) if p == 'refl' else sky.measure_pass_milliseconds(p == 'sky', 4)
        lines.append(f'{p:7s} {label:36s} {ms:7.1f} ms')
        log(lines[-1])
        actors.destroy_actor(sky)
    with open(os.path.join(OUT, 'profile.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    EAL.delete_directory(TEMP)


try:
    main()
except Exception as e:
    unreal.log_error(f'[TerragenProfile] failed: {e!r}')
    raise
finally:
    if os.environ.get('TG_SETUP_NO_QUIT') != '1':
        unreal.SystemLibrary.quit_editor()
