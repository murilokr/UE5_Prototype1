"""Turn a Terragen 0.9 world (.tgw) plus a converted terrain (.json from ter_convert.py) into a shader include.

The output defines `TGWorld TGWorld_<Name>()`, filled with the world's sun, background light, water, cloud
dimming, tone curve and the whole surface-layer tree, ready for TG_RenderRay in Terragen.ush. It also
defines the world file's camera position and heading as TGWorld_<Name>_CameraPos / _CameraHeading.

With the sky on (RSKY) it also fills the atmosphere and the sky dome / cloud layer; the cloud map itself comes
from make_cloud_map.py.

Usage: python tgw_to_ush.py <world.tgw> <terrain.json> -o <out.ush> --name <Name>
         [--sun-scale K] [--ambient-scale K] [--exposure E] [--sun HEADING ALTITUDE] [--cloud-shadows 0|1]
         [--pre-sky] [--frame F] [--sun-path-scale K] [--cloud-samples N]
"""
import argparse
import json
import math
import os

import make_cloud_map
from tgfiles import read_tgw

MAX_LAYERS = 8


def f(v):
    """Format a float as an HLSL/C++-compatible literal."""
    if isinstance(v, float) and (math.isnan(v) or math.isinf(v)):
        v = 0.0
    s = f'{float(v):.7g}'
    if 'e' not in s and '.' not in s:
        s += '.0'
    return s


def f2(a, b):
    return f'float2({f(a)}, {f(b)})'


def f3(c):
    return f'float3({f(c[0])}, {f(c[1])}, {f(c[2])})'


def atmosphere_step(world, ter):
    """The view-ray march step, (7680 / scale) / 2^ATAC; 0x1001a100 also uses it for the cloud dimming."""
    return 7680.0 / ter['scale_m'] / 2.0 ** world.find('WLDG').f32('ATAC', 4.0)


def atmosphere_fields(world, ter, sun_path_scale, sun_altitude):
    """The atmosphere as ReceiveRender (0x1002b1f0) and the penetration table (0x10019b50) prepare it."""
    atli = world.find('ATLI')
    ghaz, lita, ligt, cldl = atli.find('GHAZ'), atli.find('LITA'), atli.find('LIGT'), atli.find('CLDL')
    scale = ter['scale_m']
    span = 7680.0 / scale

    def length(d):
        return max((1.0 - d) * span / d, 0.01) if d != 0.0 else 0.0

    dh, db, dr = ghaz.f32('HVAL'), ghaz.f32('BVAL'), ghaz.f32('RVAL')
    hh, bh, rh = ghaz.f32('HHH ', 1.0), ghaz.f32('BHH ', 1.0), ghaz.f32('RHH ', 1.0)
    top = 6.0 * max(hh, bh, rh)
    if cldl and cldl.f32('ALT ') > top:
        top = cldl.f32('ALT ')
    lut = max(rh, hh)
    if dh != 0.0 and hh < lut:
        lut = hh
    if dr != 0.0 and rh < lut:
        lut = rh
    lut *= 0.125
    bottom = -float(ter['cells'])
    radius = ter['planet_radius_km'] * 1000.0 / scale
    sin_a = math.sin(math.radians(sun_altitude))
    chord = (math.sqrt((lut + radius) ** 2 + sin_a * sin_a * radius * radius - radius * radius) - sin_a * radius) * sun_path_scale
    return {
        'On': 1.0 if (dh != 0.0 or db != 0.0 or dr != 0.0) else 0.0,
        'LutOn': 1.0 if lita.f32('ACST') != 0.0 else 0.0,
        'HazeColour': ghaz.colour('HCOL'),
        'BlueColour': ghaz.colour('BCOL'),
        'RedColour': ghaz.colour('RCOL'),
        'HazeLength': length(dh),
        'BlueLength': length(db),
        'RedLength': length(dr),
        'HazeHalf': hh,
        'BlueHalf': bh,
        'RedHalf': rh,
        'Top': top,
        'Step': atmosphere_step(world, ter),
        'LutStep': lut,
        'LutBottom': bottom,
        'LutCount': float(int((top - bottom) / lut)),
        'SunChord': chord,
        'PlanetRadius': radius,
        'GlowAmount': lita.f32('GLOA', 1.0),
        'GlowPower': lita.f32('GLOP', 1.0),
        'GlowFlag': 1.0 if (ligt.f32('LCST', 1.0) != 0.0 and lita.f32('ATSH') != 0.0) else 0.0,
    }


def sky_fields(world, ter, samples):
    """The sky dome and cloud layer: ConsCloudLayer(.., 512, ..), SetCloudLayerProperties, ReceiveRender."""
    wldg, atli = world.find('WLDG'), world.find('ATLI')
    cldl, sun = atli.find('CLDL'), atli.find('SUN ')
    n = make_cloud_map.N
    steps = 2.0 ** wldg.f32('CLAC', 6.0)
    persistence = cldl.f32('GPRS', 0.25) if cldl else 0.25
    noise_amp = make_cloud_map.noise_amplitude(persistence, make_cloud_map.feature_steps(cldl)) if cldl else 0.0
    cors = sun.f32('CORS')
    extent = cldl.f32('CSIZ', 4096.0) if cldl else 4096.0
    return {
        'On': 1.0 if (wldg.f32('RSKY') != 0.0 and cldl) else 0.0,
        'Corona': 1.0 / (cors * cors) if cors > 0.0 else 0.0,
        'DomeRadius': 32.0 * ter['cells'],
        'CloudN': float(n),
        'CloudExtent': extent,
        'CloudCell': extent / n,
        'CloudContrast': cldl.f32('CONT') if cldl else 0.0,
        'CloudBias': float(int(cldl.f32('BIAS'))) if cldl else 0.0,
        'CloudMethod': float(cldl.i32('REME')) if cldl else 0.0,
        'CloudNoiseAmp': noise_amp,
        'CloudDarkness': atli.find('GHAZ').f32('CDRK'),
        'CloudSteps': steps,
        'CloudStepsMax': 4.0 * steps,
        'CloudNoiseLimit': 2.0 ** (wldg.f32('CLAC', 6.0) - 8.0),
        'CloudSamples': samples,
    }


def collect_layers(srfl, parent, out):
    """Depth-first list of surface layers; children always follow their parent."""
    index = len(out)
    term = srfl.find('TERM')
    dens = srfl.find('DENS')
    bump_set = term.find('BMTX').find('SETT') if term and term.find('BMTX') else None
    vart = dens.find('VART') if dens else None
    noise_set = vart.find('SETT') if vart else None
    out.append({
        'name': srfl.text('NAME') or f'Layer {index}',
        'parent': parent,
        'colour': term.colour('DIFC') if term else (0.5, 0.5, 0.5),
        'bump_amount': term.f32('BMAM') if term else 0.0,
        'bump_terrain_mod': term.f32('MTER') if term else 0.0,
        'bump_origin': bump_set.f32s('ORIG', 2) if bump_set else [0.0, 0.0],
        'bump_negate': 1.0 if bump_set and bump_set.i32('NEGA') else 0.0,
        'bump_isol': bump_set.f32('ISOL') if bump_set else 0.0,
        'bump_vari': bump_set.f32('VARI') if bump_set else 0.0,
        'has_noise': 1.0 if noise_set else 0.0,
        'noise_origin': noise_set.f32s('ORIG', 2) if noise_set else [0.0, 0.0],
        'variation': dens.f32('VARI') if dens else 0.0,
        'coverage': dens.f32('COVR', 1.0) if dens else 1.0,
        'alt_enable': dens.f32s('ALTE', 2) if dens else [0.0, 0.0],
        'alt_limit': dens.f32s('ALTL', 2) if dens else [0.0, 0.0],
        'alt_fuzz': dens.f32s('ALTF', 2) if dens else [0.0, 0.0],
        'slope_enable': dens.f32s('SLPE', 2) if dens else [0.0, 0.0],
        'slope_limit': dens.f32s('SLPL', 2) if dens else [0.0, 0.0],
        'slope_fuzz': dens.f32s('SLPF', 2) if dens else [0.0, 0.0],
        'smallest_size': dens.f32('SMSZ', 1.0) if dens else 1.0,
    })
    for child in srfl.findall('SRFL'):
        collect_layers(child, index, out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tgw')
    ap.add_argument('terrain_json')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--name', required=True, help='suffix for TGWorld_<Name>(); letters, digits and _')
    # Sun = LIGC * STRE and background light = colour * BRSC match Terragen's output to within about one
    # grey level for the MountainTop world, so these default to 1. They exist for fine-tuning against a
    # real render (compare.py --calibrate), since how the GUI hands these values to the DLL isn't decoded.
    ap.add_argument('--sun-scale', type=float, nargs=3, default=[1.0, 1.0, 1.0], metavar=('R', 'G', 'B'),
                    help='multiplier on the sun colour (LIGC * STRE)')
    ap.add_argument('--ambient-scale', type=float, nargs=3, default=[1.0, 1.0, 1.0], metavar=('R', 'G', 'B'),
                    help='multiplier on the background light (colour * BRSC)')
    ap.add_argument('--exposure', type=float, help='linear exposure before the tone curve (default: EXPO)')
    # ReceiveLighting scales the penetration table's sun path by a lighting-struct value * 0.01 whose GUI
    # source isn't decoded; 1 assumes it is passed as 100.
    ap.add_argument('--sun-path-scale', type=float, default=1.0, help='scale on the sun path through the atmosphere')
    ap.add_argument('--cloud-samples', type=float, default=64.0,
                    help='samples per cloud march (Terragen iterates 16 * 4 * 2^CLAC times)')
    ap.add_argument('--sun', type=float, nargs=2, metavar=('HEADING', 'ALTITUDE'),
                    help="override the world's sun direction, for renders made after the sun was moved")
    ap.add_argument('--cloud-shadows', type=int, choices=(0, 1),
                    help="override the world's 'clouds cast shadows' (LITA CCST)")
    # The MountainTop include predates the sky work and was validated against Terragen with the cloud dimming
    # fading in over the cloud depth (0x1001a100 uses the atmosphere step) and the water as a flat lit plane.
    ap.add_argument('--pre-sky', action='store_true',
                    help='render as before the sky work: cloud dimming over the cloud depth, flat water')
    ap.add_argument('--frame', type=float, default=1.0, help='animation frame, for the wave noise (time = frame / 24)')
    args = ap.parse_args()

    world = read_tgw(args.tgw)
    ter = json.load(open(args.terrain_json))
    wldg, atli = world.find('WLDG'), world.find('ATLI')
    sun, bgli, ligt, lita = atli.find('SUN '), atli.find('BGLI'), atli.find('LIGT'), atli.find('LITA')
    cldl, watr = atli.find('CLDL'), world.find('WATR')

    cells = ter['cells']
    curved = bool(ter['curved'])
    planet_radius = ter['planet_radius_km'] * 1000.0 / ter['scale_m'] if curved else 0.0
    max_drop = 0.0
    if curved:
        d = math.hypot(cells / 2, cells / 2)
        max_drop = planet_radius - math.sqrt(planet_radius ** 2 - d * d)

    layers = []
    collect_layers(world.find('SRFL'), -1, layers)
    if len(layers) > MAX_LAYERS:
        raise SystemExit(f'{len(layers)} surface layers; Terragen.ush supports {MAX_LAYERS} (raise TG_MAX_LAYERS)')

    sun_colour = [c * sun.f32('STRE', 1.0) * k for c, k in zip(sun.colour('LIGC', (1, 1, 1)), args.sun_scale)]
    bg_scale = bgli.f32('BRSC', 1.0)
    method = bgli.i32('METH')
    single = [c * bg_scale * k for c, k in zip(bgli.colour('AMBC'), args.ambient_scale)]
    up = [c * bg_scale * k for c, k in zip(bgli.colour('UPSC'), args.ambient_scale)]
    side = [c * bg_scale * k for c, k in zip(bgli.colour('SDSC'), args.ambient_scale)]
    zero = (0.0, 0.0, 0.0)

    cam = [wldg.f32('CAMX'), wldg.f32('CAMY'), wldg.f32('CAMZ')]
    heading = math.degrees(math.atan2(wldg.f32('TARX') - cam[0], wldg.f32('TARY') - cam[1]))
    exposure = args.exposure if args.exposure is not None else wldg.f32('EXPO', 1.0)
    sun_heading, sun_altitude = args.sun if args.sun else (sun.f32('HEAD'), sun.f32('ALT '))
    cloud_depth = cldl.f32('DEPT') if cldl else 0.0
    cloud_fade = cloud_depth if args.pre_sky else atmosphere_step(world, ter)
    cloud_shadows = args.cloud_shadows if args.cloud_shadows is not None else int(lita.f32('CCST') != 0.0)
    name = args.name
    atmos = atmosphere_fields(world, ter, args.sun_path_scale, sun_altitude)
    sky = sky_fields(world, ter, args.cloud_samples)

    L = []
    w = L.append
    w(f'// Generated by Tools/tgw_to_ush.py from {os.path.basename(args.tgw)} and {os.path.basename(args.terrain_json)}.')
    w('// Re-run the tool instead of editing this file.')
    w(f'// sun scale {args.sun_scale}, ambient scale {args.ambient_scale}, exposure {exposure:.6g}')
    if args.sun:
        w(f'// Sun moved to heading {sun_heading:g}, altitude {sun_altitude:g} (the world file has '
          f'{sun.f32("HEAD"):g}, {sun.f32("ALT "):g}).')
    if args.cloud_shadows is not None:
        w(f'// Clouds cast shadows set to {args.cloud_shadows} (the world file has {int(lita.f32("CCST") != 0.0)}).')
    if args.pre_sky:
        w('// Pre-sky renderer: cloud dimming over the cloud depth, flat water (see tgw_to_ush.py --pre-sky).')
    if args.sun_scale != [1.0, 1.0, 1.0] or args.ambient_scale != [1.0, 1.0, 1.0]:
        w('// The light scales were fitted against a Terragen render (compare.py --calibrate).')
    guard = f'TERRAGEN_WORLD_{name.upper()}_USH'
    w(f'#ifndef {guard}')
    w(f'#define {guard}')
    w('')
    w(f'static const float3 TGWorld_{name}_CameraPos = {f3(cam)};')
    w(f'static const float TGWorld_{name}_CameraHeading = {f(heading)};')
    w(f'static const float TGWorld_{name}_ImageWidth = {f(wldg.i32("IMWI"))};')
    w('')
    w(f'TGWorld TGWorld_{name}()')
    w('{')
    w('\tTGWorld W;')
    w(f'\tW.Terrain.Cells = {cells};')
    w(f'\tW.Terrain.CellsLog2 = {cells.bit_length() - 1};')
    w(f'\tW.Terrain.BaseHeight = {f(ter["base_height"])};')
    w(f'\tW.Terrain.HeightScale = {f(ter["height_scale"])};')
    w(f'\tW.Terrain.MinHeight = {f(ter["min_height"] - max_drop)};')
    w(f'\tW.Terrain.MaxHeight = {f(ter["max_height"])};')
    w(f'\tW.Terrain.PlanetRadius = {f(planet_radius)};')
    w(f'\tW.Detail = {f(wldg.f32("LDET", 1.0))};')
    w(f'\tW.SunHeading = {f(sun_heading)};')
    w(f'\tW.SunAltitude = {f(sun_altitude)};')
    w(f'\tW.SunDiameter = {f(sun.f32("DIAM", 1.0))};')
    w(f'\tW.SunColour = {f3(sun_colour)};')
    w(f'\tW.LandShadows = {f(ligt.f32("LCST", 1.0))};')
    w(f'\t// Background light method {method} (0 single colour, 1 multi-directional, 2 GI treated as 0).')
    w(f'\tW.Ambient = {f3(single if method != 1 else zero)};')
    w(f'\tW.AmbientUp = {f3(up if method == 1 else zero)};')
    w(f'\tW.AmbientAway = {f3(side if method == 1 else zero)};')
    w(f'\tW.AmbientSun = {f3(side if method == 1 else zero)};')
    w(f'\tW.CloudsPresent = {f(1.0 if cldl else 0.0)};')
    w(f'\tW.CloudAltitude = {f(cldl.f32("ALT ") if cldl else 0.0)};')
    w(f'\tW.CloudDepth = {f(cloud_depth)};')
    w(f'\tW.CloudSunlight = {f(1.0 - lita.f32("CLDS"))};')
    w(f'\tW.CloudFade = {f(cloud_fade)};')
    w(f'\tW.CloudShadows = {f(1.0 if cldl and cloud_shadows else 0.0)};')
    w('\tW.WaterPresent = 1.0;')
    w(f'\tW.WaterLevel = {f(watr.f32("LEVL"))};')
    w(f'\tW.WaterColour = {f3(watr.colour("DIFC"))};')
    w(f'\tW.ToneMode = {wldg.i32("EXME")};')
    w(f'\tW.Gamma = {f(wldg.f32("GAMA", 2.0))};')
    w(f'\tW.Exposure = {f(exposure)};')
    w('\tW.BumpTerrainValue = 0.25;')
    w('\tW.BackgroundColour = float3(0.0, 0.0, 0.0);')
    # Water (WATR, copied to 0x100acb58 by ReceiveLandscape; shaded by 0x10035020). TRNC * TRNS is the
    # distance in metres over which each channel decays by e; the refraction trace stops at 6 of the shortest.
    wlen = watr.f32('WLEN')
    absorb = [ter['scale_m'] * 256.0 / max(c * 256.0 * watr.f32('TRNS'), 1e-4) for c in watr.colour('TRNC')]
    w(f'\tW.WaterShading = {f(0.0 if args.pre_sky else 1.0)};')
    w(f'\tW.WaterWaveSize = {f(wlen)};')
    w(f'\tW.WaterWaveAmp = {f(watr.f32("WAMP") * wlen)};')
    w(f'\tW.WaterWaveTime = {f((args.frame + 1e-4) / 24.0 / max(wlen, 1e-6) / ter["scale_m"])};')
    w(f'\tW.WaterViscosity = {f(watr.f32("VISC") * watr.f32("WAMP"))};')
    w(f'\tW.WaterReflMin = {f(watr.f32("MINR"))};')
    w(f'\tW.WaterReflMax = {f(watr.f32("MAXR"))};')
    w(f'\tW.WaterReflCurve = {f(watr.f32("RCRV", 1.0))};')
    w(f'\tW.WaterSunLobe = {f(watr.f32("RSLB"))};')
    w(f'\tW.WaterSunWidth = {f(max(watr.f32("RSLS"), 0.01))};')
    w(f'\tW.WaterAbsorb = {f3(absorb)};')
    w(f'\tW.WaterMaxDepth = {f(6.0 / max(min(absorb), 1e-6))};')
    for key, value in atmos.items():
        w(f'\tW.Atmos.{key} = {f3(value) if isinstance(value, (list, tuple)) else f(value)};')
    for key, value in sky.items():
        w(f'\tW.Sky.{key} = {f(value)};')
    w(f'\tW.NumLayers = {len(layers)};')
    for i in range(MAX_LAYERS):
        ly = layers[i] if i < len(layers) else None
        p = f'\tW.Layers[{i}].'
        if ly:
            w(f'\t// {ly["name"]}' + (f' (child of {layers[ly["parent"]]["name"]})' if ly['parent'] >= 0 else ' (root)'))
        else:
            w('\t// unused')
            ly = {'name': '', 'parent': -2, 'colour': zero, 'bump_amount': 0.0, 'bump_terrain_mod': 0.0,
                  'bump_origin': [0, 0], 'bump_negate': 0.0, 'bump_isol': 0.0, 'bump_vari': 0.0, 'has_noise': 0.0,
                  'noise_origin': [0, 0], 'variation': 0.0, 'coverage': 0.0, 'alt_enable': [0, 0],
                  'alt_limit': [0, 0], 'alt_fuzz': [0, 0], 'slope_enable': [0, 0], 'slope_limit': [0, 0],
                  'slope_fuzz': [0, 0], 'smallest_size': 1.0}
        w(f'{p}Parent = {ly["parent"]};')
        w(f'{p}Colour = {f3(ly["colour"])};')
        w(f'{p}BumpAmount = {f(ly["bump_amount"])};')
        w(f'{p}BumpTerrainMod = {f(ly["bump_terrain_mod"])};')
        w(f'{p}BumpOrigin = {f2(*ly["bump_origin"])};')
        w(f'{p}BumpNegate = {f(ly["bump_negate"])};')
        w(f'{p}BumpIsol = {f(ly["bump_isol"])};')
        w(f'{p}BumpVari = {f(ly["bump_vari"])};')
        w(f'{p}HasNoise = {f(ly["has_noise"])};')
        w(f'{p}NoiseOrigin = {f2(*ly["noise_origin"])};')
        w(f'{p}Variation = {f(ly["variation"])};')
        w(f'{p}Coverage = {f(ly["coverage"])};')
        for key, field in (('alt_enable', 'AltEnable'), ('alt_limit', 'AltLimit'), ('alt_fuzz', 'AltFuzz'),
                           ('slope_enable', 'SlopeEnable'), ('slope_limit', 'SlopeLimit'), ('slope_fuzz', 'SlopeFuzz')):
            w(f'{p}{field} = {f2(*ly[key])};')
        w(f'{p}SmallestSize = {f(ly["smallest_size"])};')
    w('\treturn W;')
    w('}')
    w('')
    w(f'#endif // {guard}')
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w', newline='\n') as fh:
        fh.write('\n'.join(L) + '\n')
    print(f'{args.out}: {len(layers)} layers ({", ".join(l["name"] for l in layers)}), '
          f'camera {cam} heading {heading:.2f}')


if __name__ == '__main__':
    main()
