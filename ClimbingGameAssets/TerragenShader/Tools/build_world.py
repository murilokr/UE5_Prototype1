"""Generate a world's shader include (and cloud map) from its entry in Tools/worlds.json.

  python Tools/build_world.py <Name> [<Name> ...]     (or --all)

For each world it converts the terrain if Textures/<terrain>_height.png is missing (ter_convert.py), writes
Shaders/Worlds/<Name>.ush (tgw_to_ush.py with the entry's ush_args) and, for worlds with clouds,
Textures/<Name>_clouds.png/.r8 (make_cloud_map.py). Then run Tools/ue_setup.py with TG_WORLD=<Name> in the
editor to make the Unreal assets. --out writes the generated files to another folder instead, to check them
against the committed ones.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'Tools')


def load_worlds():
    with open(os.path.join(TOOLS, 'worlds.json'), encoding='utf-8') as f:
        return {k: v for k, v in json.load(f).items() if not k.startswith('_')}


def terrain_name(entry):
    return os.path.splitext(os.path.basename(entry['ter']))[0]


def run(*args):
    print('>', ' '.join(args))
    subprocess.run([sys.executable, *args], cwd=ROOT, check=True)


def build(name, entry, out):
    terrain = terrain_name(entry)
    textures = os.path.join(ROOT, 'Textures')
    if not os.path.exists(os.path.join(textures, f'{terrain}_height.png')):
        run(os.path.join(TOOLS, 'ter_convert.py'), entry['ter'], '-o', textures)
    shaders = out or os.path.join(ROOT, 'Shaders', 'Worlds')
    os.makedirs(shaders, exist_ok=True)
    run(os.path.join(TOOLS, 'tgw_to_ush.py'), entry['tgw'], os.path.join(textures, f'{terrain}.json'),
        '-o', os.path.join(shaders, f'{name}.ush'), '--name', name, *entry.get('ush_args', []))
    if entry.get('clouds'):
        run(os.path.join(TOOLS, 'make_cloud_map.py'), entry['tgw'], '-o', out or textures, '--name', name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--out', help='write the .ush and cloud maps here instead')
    a = ap.parse_args()
    worlds = load_worlds()
    names = list(worlds) if a.all else a.names
    if not names:
        ap.error(f'name a world ({", ".join(worlds)}) or pass --all')
    for name in names:
        if name not in worlds:
            ap.error(f'{name} is not in Tools/worlds.json')
        build(name, worlds[name], a.out and os.path.abspath(a.out))


if __name__ == '__main__':
    main()
