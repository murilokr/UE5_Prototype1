# Validation

How close the shader comes to Terragen 0.9.43, what is exact and what is approximated, and what was checked in Unreal. The tools and harness are described in the [README](../README.md#regenerating-and-testing). Error is mean absolute difference per channel on a 0–255 scale throughout.

## How close it is

The five faces that show terrain were compared with the project's `ClimbingGameAssets/Textures/MountainTop` crosses (Up is sky only in both). Overlap is how much the silhouettes coincide (1.0 = identical). The last column is how far the face averages differ.

| Frame (height) | Silhouette overlap | Error | Average colour |
|---|---|---|---|
| 1 (start) | 0.994–1.000 | 6–16 | red/green within 6; blue up to 7 lower |
| 200 (+199 m) | 0.975–0.995 | 13–42 | red/green within 6; blue up to 13 lower |
| 400 (+399 m) | 0.951–0.993 | 12–50 | red/green within 6; blue up to 12 lower |

Most of the remaining per-pixel error is texture: the coverage speckle and bump are statistically the same, but not identical pixel for pixel.

Findings from the comparison:
- The renders used **`prototype2_v2.ter`** (flat). The curved `prototype2.ter` matches worse.
- `ClimbingGameAssets/Output/*.bmp` holds a different scene (sky, clouds, reflective water). It is not the MountainTop reference; it is the sky reference below.
- The light values taken straight from the `.tgw` are already optimal in display space: sun = `LIGC × STRE`, ambient = `AMBC × BRSC`, exposure = `EXPO`, then tone mode `EXME` with `GAMA`.

### The sky world (Prototype2World)

`ClimbingGameAssets/Output/Sky_<Face>0001..0021.bmp` was rendered from `Prototype2World.tgw` (its snow and grass layers), on `prototype2_v2.ter`, with the camera at (198, 270, 16.43) heading 18, rising about 0.067 units per frame. The camera comes from matching the sky silhouettes; the mask overlap is 0.96.

Two settings differ from the saved world file, and both were recovered from the render itself:
- **The sun was at heading 24, altitude 48**, not 119/43. Two independent checks agree: the brightest sky (the glow through the clouds) is centred there, and the terrain shadows match.
- **"Clouds cast shadows" was on**; the file has it off. Without it, the cloud-shadowed slopes can't be reproduced.

`Shaders/Worlds/Prototype2World.ush` is generated with those two overrides (`ush_args` in `Tools/worlds.json`). Per-channel error against frame 1, all pixels:

| Face | First sky render (world file's sun, before the cloud-map fixes) | Now |
|---|---|---|
| Front | 71–113 | 29–45 |
| Left | 31–53 | 23–29 |
| Back | 45–71 | 19–21 |
| Right | 78–113 | 21–24 |
| Up | 74–125 | 18–22 |
| Down | 72–99 | 22–26 |

Most of what's left is the cloud pattern. The cloud map is generated exactly; its gaps line up with Terragen's in the Left, Back and Right faces. But the overhead clouds come out thicker than Terragen's, and they sit about one cell off (shifting the map by roughly (−8, +12) units lowers the error by about 3 levels; Terragen has a cloud scroll, `SetCloudLayerScroll`, which the world file doesn't store). Where the clouds are, the shadows are, so the near terrain in the Front face inherits the difference.

## What is exact and what is not

**Taken from the decompiled DLL:**
- triangle interpolation of the heightfield (the (1,0)–(0,1) diagonal), and normals from ±0.5-cell differences
- the soft-shadow march, with two rays aimed at the sun disc's lower and upper edges, followed by a smoothstep
- the light equation and the uniform cloud dimming
- the value noise and its exact lattice, fractal sums, gradient noise and bump levels
- layer coverage: the sigmoid, altitude and slope penalties, and the level of detail
- tone modes 0, 1 and 2 (`RGBm` and the buffer pass), with Terragen's 16-bit and 8-bit quantisation in the harness
- the camera: focal length = zoom × width / 2, heading from +y towards +x
- the atmosphere march (`0x1001a350`, `0x1001a730`, `0x1001aad0`): red decay, blue in-scatter and haze per step, with the grid-edge split
- the sunlight penetration table (`0x10019b50`), as a closed-form geometric sum instead of a 24 000-entry table
- the glow (`0x1001b450`)
- the sky dome sphere (`0x1001e0d0`) and the sun disc with its corona (`0x1001d430`)
- the cloud map (`0x1001ca60`, `0x1001cc30`, and the exe's own generation loop): the midpoint displacement, `rand()` order, char wrap-around, and `GFSZ` as a 16-bit level offset
- cloud density, slab and march (`0x1001cd40`, `0x1001cec0`, `0x1001d020`), both render methods, with the Perlin tables, turbulence and fBm (`0x10020b00`–`0x10020e80`)
- cloud shadows (`0x1001a100`, `0x1001be60`, `0x1001c560`): a grid one atmosphere step apart, smoothstep-blended, averaged along atmosphere steps
- water (`0x10034ce0`, `0x10035020`, `0x10035fd0`, `0x10036280`): wave turbulence, the reflection with its reflectivity curve, the sun lobe, refraction to the bed with per-channel absorption

**Approximations:**
- The jitter that offsets coverage and bump samples is a PCG hash, not Terragen's table.
- The bump amplitude reads a per-point map that wasn't identified. `BumpTerrainValue = 0.25` stands in for it.
- The footprint used for detail is distance × pixel angle / Detail.
- In multi-directional background light, the away/sun-side colours are guessed (`SDSC`). MountainTop uses single colour, so it isn't affected.
- Terragen shades the sky and water as micro-polygons. Here they are shaded once per pixel, so:
  - the sky's noise footprint is one pixel (Terragen's polygons are about 1–4 pixels);
  - water takes one sample, where Terragen averages `WAQU` jittered samples.
- The cloud marches take 64 segments (`--cloud-samples`) of Terragen's 4096 steps; within each segment the slab coverage is exact.
- Penetration table: the sun-path factor `--sun-path-scale` comes from a GUI value that wasn't decoded; 1 is assumed.

**Not implemented:**
- atmospheric shadows from terrain (Terragen skips them too whenever the camera is inside the terrain grid);
- water surf and foam (`SFDN` and the rest), the wave-strength pattern (`WVAR`), and curved water;
- the cloud scroll (`SetCloudLayerScroll`), which is 0 unless clouds were animated.

**Known difference:** at higher frames, Terragen's distant outlines sit 1–3 px higher. The cause isn't identified; Terragen's sub-cell geometry is a candidate.

## In Unreal

Measured with `Tools/ue_setup.py`, `ue_view_check.py` and `ue_profile.py`, on an RTX 4060 Ti. The CPU harness renders the full 2048² atlas in 2.0 s for MountainTop and 3.2 s for Prototype2World (24 threads).

| Check | Result |
|---|---|
| GPU atlas vs the CPU harness, frames 1 and 400 | Silhouette overlap 1.000; colour within 1.1 levels (float rounding in the bump and speckle) |
| GPU atlas vs Terragen | Overlap 0.996 at frame 1 and 0.986 at frame 400, the same as the CPU harness |
| Level-viewport screenshots vs Terragen (`ue_view_check.py`) | Overlap 0.94–0.99 on the five faces with terrain; correct orientation, no mirroring |
| Following the player: 300 m along Unreal +X, and 1 km below the start (lifted above the ground) | Unreal→Terragen mapping exact; GPU vs CPU overlap 1.000 on every face, colour within 2.3 levels |
| Builds | Development and DebugGame editor targets, and the game target |
| Cost of a full six-face redraw of the 2048² atlas | 30 ms on an idle GPU (about 5 ms per face); 59 ms while a game was also using the GPU |
| Sky world (`TG_WORLD=Prototype2World`): GPU atlas vs the CPU harness | Colour within 1.7 levels per channel, identical face averages |
| Sky world: GPU atlas vs Terragen (`ClimbingGameAssets/Output/Sky_*0001.bmp`) | Error 18–45 levels per channel, the same as the CPU harness |
| Sky world: full six-face redraw, idle GPU | 148 ms, about 5× MountainTop (about 25 ms per face) |
| Sky world: level-viewport screenshots (`ue_view_check.py`) | Sky, clouds, haze and rippled water all render; the image is brighter than Terragen's because UE's tonemapper runs on top |
| Split rendering with the actor's defaults (sky, reflection and terrain atlases), GPU vs the CPU harness `--split 256 --refl-face 128 --cloud-quality 0.5` | MountainTop within 1.1 levels, Prototype2World within 1.8; error against Terragen unchanged (MountainTop overlap 0.996, Prototype2World 20–45) |
| Cost per pass on an idle GPU, Prototype2World (MountainTop) | Visible terrain tiles of a 90° 16:9 view, the per-frame cost while moving: 11.5 ms (8.1 ms). Whole terrain atlas 33 ms (23 ms). Whole sky atlas 86 ms, about 3.6 ms a tile; whole reflection atlas 35 ms, about 6 ms a face; both only while catching up after 20 m of movement |
| The same view with reflections computed exactly (`ReflectionAtlasSize` 0) | 29 ms; the whole terrain atlas 98 ms |
| The same view with a 4096 terrain atlas | 40.5 ms (28.5 ms) |
| The same view with `Projection` Sphere | 11.2 ms (7.6 ms), slightly less than the cube: its pixels are spread evenly, so fewer land in a view. The whole atlas costs more: 39 ms (27 ms) |
| Sphere-map viewport screenshots vs cube ones (`TG_PROJECTION=sphere` with `ue_view_check.py`), Prototype2World | Same image and orientation on all six views, face averages within 1 level; 7–10 levels per pixel from sampling the fine texture at different points |
| Culled vs full screenshots (`TG_CULLTEST=1`), cube and sphere | Identical |

## Meshes

`Tests/tg_mesh_test.cpp` and `Tools/ue_mesh_setup.py`. Results are MountainTop / Prototype2World.

| Check | Result |
|---|---|
| Terrain renderer after splitting the layer tree for meshes (`TG_SurfaceLayers`) | All six faces bit-identical to before, `.bmp` and float `.aux`, both worlds |
| A mesh face lying on the terrain vs the terrain there, 3000 random points and footprints | Albedo mean 0.009, p99 0.11 levels. Lit colour for faces up to 38°: mean 0.018 / 0.009, p99 0.07 / 0.06 display levels. Steeper faces blend in a side plane's bump, a different pattern by design: median about 15 display levels from the terrain's. |
| Bump-plane blend | Continuous: a turn 100× smaller across a plane boundary changes the colour about 270× less |
| Atmosphere in the look vs `TG_Atmosphere` | Identical whenever the march fits in `Segments` steps (Prototype2World's step is 32 units, 480 m). Capping it does cost: with 2–8 steps, longer segments are off by 10–16 levels on average. Hence the default of 24. |
| Light colour × light function vs Terragen's sunlight at a point | Identical (500 points) |
| Mapping | Positions match `ATerragenSkybox::ToTerragen` to 5e-4 units; direction and sRGB round trips exact |
| Atlas snapping (cube and sphere map) | Corners are texel centres inside the direction's face; the weights reproduce its atlas position to 5e-4 texels |
| GPU vs CPU (`TG_MeshCheckTexel`, 1024 texels per mode, and the light probe) | Albedo ≤ 0.12 levels, normals ≤ 0.07°, unlit colour ≤ 0.07, look colour ≤ 0.12 display levels; probe within 0.07% (half floats) |
| Sky through the look vs the atlas's bytes (`mesh/sky_check.txt`, 90° 512² viewport shots) | Median 2 / 3 levels (mean 4.1 / 6.1); through Unreal's tonemapper, median 15 / 19. About 1% of texels are off by more (p99 56 / 60); not traced yet, TAA on high-contrast edges being the likely cause. |
| `PP_TerragenSoften` leaves the sky alone (`mesh/sky_check.txt`, every pixel of the 512² shot) | It differs from Unreal's own sky exactly as much as two shots through Unreal's tonemapper differ from each other: mean 0.22 / 0.33 levels, p99 1, for both comparisons (temporal AA) |
| `PP_TerragenSoften`'s jitter and prefilter, on the CPU | The jitter belongs to the corners' texels (40 000 corners), is uniform within ±half a texel and the same across the sphere map's seam; the four taps centre on the jittered point and span half a texel |
| Steadiness of `PP_TerragenSoften` when the close view turns 0.1° (`mesh/stability.txt`; under half a texel, so the meshes slide under the fixed grid) | Pixels changing by more than 16 levels: without prefilter or jitter 10.2% / 3.4%, prefilter only 8.3% / 3.1%, both (the default) 8.6% / 3.2%. Mean change 5.9 → 5.3 and 2.9 → 2.7 levels. The jitter gives back a little of the prefilter's gain: it is deliberate per-texel variation. |
| Cost per full 1080p screen of mesh pixels (`mesh/timing.txt`) | Lit material about 12.6 / 13.1 ms, unlit 14.3 / 13.8 ms. The look's maths alone 0.7 / 7.2 ms (the atmosphere is the difference; screen reads not included). |

The screenshots in `Out/ue/<World>/mesh/` show the test maps through the look (`view_meshes_look`, `view_meshes_close_look`), at screen resolution (`view_meshes_sharp`), through Unreal's tonemapper (`view_meshes_ue_tonemapper`) and with only the softness (`view_meshes_soften`, `view_meshes_close_soften`, and `view_meshes_close_soften_plain` / `_nojitter` without prefilter and jitter). Prototype2World's start is in a cloud shadow, so the script also puts the shapes in the nearest sunlit spot that is above the Terragen ground (`view_sunlit_*`).
