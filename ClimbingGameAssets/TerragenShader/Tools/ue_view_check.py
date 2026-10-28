"""Check the sky as the editor viewport draws it: six 90-degree screenshots from the TerragenSkybox's centre.

Runs inside the editor (command line as for ue_setup.py). Loads TerragenSkyTest, redraws the atlas, then
over the following frames points the level viewport along Unreal +X, +Y, -X, -Y, up and down and takes a
512x512 HighResShot of each. Those land in Saved/Screenshots/WindowsEditor; the script moves them to
Out/ue/view_<Face>.png and also writes Out/ue/view_faces/<Face>.bmp with the viewport's axis gizmo blanked.
With HeadingAtPlusX = 18 these views are the atlas faces Front, Left, Back, Right, Up and Down, so:
  python Tools/compare.py <terragen faces dir> 0001 Out/ue/view_faces --black-level 8
(the black level absorbs the few grey levels post-processing adds to the empty sky).

Set TG_WORLD=Prototype2World (as for ue_setup.py) to check that world's test map, TerragenSkyTest_<World>;
its output goes to Out/ue/<World>/ instead.
"""
import glob
import os
import shutil
import sys

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
TOOLS = os.path.join(PROJECT, 'ClimbingGameAssets', 'TerragenShader', 'Tools')
WORLD = os.environ.get('TG_WORLD', 'MountainTop')
SUFFIX = '' if WORLD == 'MountainTop' else f'_{WORLD}'
OUT = os.path.join(PROJECT, 'ClimbingGameAssets', 'TerragenShader', 'Out', 'ue', *([WORLD] if SUFFIX else []))
GIZMO = 60  # size of the axis gizmo in the bottom-left corner of viewport screenshots
SHOTS = os.path.join(PROJECT, 'Saved', 'Screenshots', 'WindowsEditor')
VIEWS = [('Front', 0, 0), ('Left', 0, 90), ('Back', 0, 180), ('Right', 0, 270), ('Up', 90, 270), ('Down', -90, 270)]
SETTLE_TICKS = 40  # frames to let a view render before and after each screenshot

# TG_CULLTEST=1: before each screenshot, clear the terrain atlas and draw only the tiles ATerragenSkybox's
# culling picks for that view (90-degree, square). The screenshots then go to view_cull_faces/ and should
# match view_faces/ from a normal run exactly if the culling never leaves out a visible tile.
CULL_TEST = os.environ.get('TG_CULLTEST') == '1'
FACES_DIR = 'view_cull_faces' if CULL_TEST else 'view_faces'

state = {'tick': 0, 'step': 0, 'handle': None, 'before': set(), 'location': None, 'sky': None}


def setup():
    os.makedirs(OUT, exist_ok=True)
    unreal.EditorLoadingAndSavingUtils.load_map(f'/Game/MyStuff/Skybox/Terragen/TerragenSkyTest{SUFFIX}')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sky = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.TerragenSkybox)][0]
    # Only the sky: hide the floor, light and player start so nothing covers the views.
    for a in actors.get_all_level_actors():
        if a != sky:
            a.set_is_temporarily_hidden_in_editor(True)
    unreal.TerragenSkybox.finish_compilation()
    sky.redraw_from_actor_location()
    # Everything is compiled by now, so this times the redraw on a quiet GPU.
    ms = sky.measure_redraw_milliseconds(8)
    unreal.log(f'[TerragenView] full six-face redraw: {ms:.1f} ms')
    with open(os.path.join(OUT, 'timing.txt'), 'w') as f:
        f.write(f'{ms:.2f}\n')
    state['location'] = sky.get_actor_location()
    state['sky'] = sky
    state['before'] = set(glob.glob(os.path.join(SHOTS, '*.png')))


def finish():
    sys.path.insert(0, TOOLS)
    from tgfiles import write_bmp
    from unpack_cross import read_png_rgb

    os.makedirs(os.path.join(OUT, FACES_DIR), exist_ok=True)
    shots = sorted(set(glob.glob(os.path.join(SHOTS, '*.png'))) - state['before'], key=os.path.getmtime)
    for (name, _, _), path in zip(VIEWS, shots):
        png = os.path.join(OUT, f'view_{"cull_" if CULL_TEST else ""}{name}.png')
        shutil.move(path, png)
        w, h, img = read_png_rgb(png)
        for y in range(h - GIZMO, h):
            img[y * w * 3:(y * w + GIZMO) * 3] = bytes(GIZMO * 3)
        write_bmp(os.path.join(OUT, FACES_DIR, f'{name}.bmp'), w, h, img)
    unreal.log(f'[TerragenView] saved {len(shots)} screenshots')
    unreal.unregister_slate_post_tick_callback(state['handle'])
    if os.environ.get('TG_SETUP_NO_QUIT') != '1':
        unreal.SystemLibrary.quit_editor()


def tick(_delta):
    try:
        state['tick'] += 1
        step = state['step']
        if step >= len(VIEWS) * 2:
            if state['tick'] > SETTLE_TICKS:
                finish()
            return
        if state['tick'] < SETTLE_TICKS:
            return
        state['tick'] = 0
        name, pitch, yaw = VIEWS[step // 2]
        if step % 2 == 0:
            rotation = unreal.Rotator(0.0, pitch, yaw)
            unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(state['location'], rotation)
            if CULL_TEST:
                n = state['sky'].draw_visible_tiles(state['location'], rotation, 90.0, 1.0, True)
                unreal.log(f'[TerragenView] {name}: {n} visible tiles drawn')
        else:
            unreal.SystemLibrary.execute_console_command(None, 'HighResShot 512x512')
            unreal.log(f'[TerragenView] screenshot {name}')
        state['step'] += 1
    except Exception as e:
        unreal.log_error(f'[TerragenView] failed: {e!r}')
        unreal.unregister_slate_post_tick_callback(state['handle'])
        unreal.SystemLibrary.quit_editor()


# -ExecutePythonScript closes the editor once the script returns unless asked to keep it running; the
# screenshots happen over the next frames, and finish() quits.
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
setup()
state['handle'] = unreal.register_slate_post_tick_callback(tick)
