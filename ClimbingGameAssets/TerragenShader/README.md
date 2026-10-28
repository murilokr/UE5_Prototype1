# Terragen 0.9 renderer as an HLSL shader

`Shaders/Terragen.ush` re-implements how Terragen 0.9.43 renders terrain, using the formulas and constants decompiled from `Terragen.dll`:
- the heightfield surface
- the surface-layer tree with its coverage and bump texture
- sun, ambient and soft shadows
- the atmosphere (haze, blue and red layers, glow, sunlight penetration)
- the sky dome with the sun disc and the cloud layer, including cloud shadows
- water with waves, reflections, the sun's reflection and a see-through shallow bed
- the exposure/tone curve

Terragen rasterises micro-polygons; the shader ray-marches the same surface per pixel instead. Both find the same visible point.

The same `.ush` also compiles as C++ (`Tests/hlsl_shim.h`), so `Tests/tg_render` can render it on the CPU and compare it with real Terragen output.

## Folder

| Path | What |
|---|---|
| `Shaders/Terragen.ush` | The renderer. Main entry points: `TG_RenderRay`, `TG_ShadeRay`, `TG_CameraRay`. |
| `Shaders/TerragenUE.ush` | Unreal glue: the skybox cross atlas (same layout as `Scripts/GenerateSkyboxTexture.py`), UE↔Terragen directions, sRGB decode. |
| `Shaders/TerragenPerlin.ush` | Terragen's Perlin tables (`srand(0)` permutation and gradients), for the cloud and wave noise. |
| `Shaders/Worlds/MountainTop.ush` | Generated from `DynamicPrototype2/WorldNewLocationAndAtmosphere.tgw`: sun, light, water, tone settings and the 5 surface layers. Sky off. |
| `Shaders/Worlds/Prototype2World.ush` | Generated from `DynamicPrototype2/Prototype2World.tgw`, with the sun and cloud shadows of the `Output/` reference render (see below). Sky, clouds, atmosphere and water on. |
| `Textures/prototype2_v2_height.png` | Lossless 16-bit heightfield (`.r16` is the same data, raw, for the harness). |
| `Textures/noise128.png` | Terragen's value-noise lattice: `srand(1)`, 16384 × `rand()`. |
| `Textures/<World>_clouds.png` | The world's 513² cloud map, 8-bit (value + 128); `.r8` is the same data, raw. |
| `Tools/worlds.json` | The registry of worlds: world file, terrain, overrides, start camera, sun. Every tool reads it. |
| `Tools/build_world.py` | Generates a registered world's `Shaders/Worlds/<Name>.ush` and cloud map (runs the three tools below). |
| `Tools/ter_convert.py` | `.ter` → height texture + `.json` metadata. |
| `Tools/make_noise_table.py` | Writes `noise128.png/.r16`. |
| `Tools/make_perlin_tables.py` | Writes `Shaders/TerragenPerlin.ush`. |
| `Tools/make_cloud_map.py` | `.tgw` → `<World>_clouds.png/.r8`, generated exactly like Terragen (seed, persistence, feature size). |
| `Tools/tgw_to_ush.py` | `.tgw` + terrain `.json` → `Shaders/Worlds/<Name>.ush`. |
| `Tools/unpack_cross.py` | Undoes `Scripts/GenerateSkyboxTexture.py`: splits a 2048² cross back into six faces. |
| `Tools/compare.py` | Side-by-side images and metrics against Terragen; `--calibrate` fits light scales. |
| `Tools/ue_setup.py` | Run inside the editor: creates the Unreal assets and test map, renders on the GPU and exports the atlas. |
| `Tools/ue_view_check.py` | Run inside the editor: six screenshots of the sky from the level viewport, plus a quiet-GPU redraw timing. Takes `TG_WORLD` like `ue_setup.py`; `TG_CULLTEST=1` draws only the tiles the culling picks before each shot. |
| `Tools/ue_profile.py` | Run inside the editor: times each pass with one feature switched off at a time, to see where the GPU time goes. |
| `Tests/build.cmd` | Builds `Build/tg_render.exe` (MountainTop) and `Build/tg_render_Prototype2World.exe` with MSVC, and checks the shaders compile with DXC (SM6.6) and FXC (SM5): the sky lookup, the atlas for both worlds, and the exact Custom-node calls of the existing Unreal materials. |

The tools need only a stock Python 3, with no packages.

## Regenerating

```
cd ClimbingGameAssets/TerragenShader
python Tools/ter_convert.py ../DynamicPrototype2/prototype2_v2.ter -o Textures
python Tools/make_noise_table.py -o Textures
python Tools/make_perlin_tables.py
python Tools/build_world.py --all
Tests\build.cmd
Build\tg_render.exe Textures/prototype2_v2_height.r16 Textures/noise128.r16 Out/frame0001
python Tools/unpack_cross.py ../Textures/MountainTop/Sky_MountainSide_Alt_0001.png Out/reference 0001
python Tools/compare.py Out/reference 0001 Out/frame0001
Build\tg_render_Prototype2World.exe Textures/prototype2_v2_height.r16 Textures/noise128.r16 Out/sky_p2w --cam 198 270 16.43 --heading 18 --clouds Textures/Prototype2World_clouds.r8
python Tools/compare.py ../Output 0001 Out/sky_p2w
```

`build_world.py` runs `tgw_to_ush.py` (and `make_cloud_map.py` for worlds with clouds) with each world's options from `Tools/worlds.json`; the output is byte-identical to the committed files. `--out <dir>` writes elsewhere, to check that.

`--pre-sky` keeps MountainTop rendering exactly as it was validated before the sky work: the cloud dimming fades in over the cloud depth (the DLL uses the atmosphere step, which would shift MountainTop's frame-1 error by about ±1 level per channel) and the water is a flat lit plane.

`tg_render` takes `--cam x y z` (Terragen units), `--heading`, `--size` and `--faces`. For MountainTop frame N, the camera is `164 223 (45.647 + (N-1) * 0.067)`. With `--atlas` it renders the whole 2048² cross through the same function the Unreal material uses, and checks that the sky lookup inverts the atlas exactly. Sky worlds also take `--clouds <World>_clouds.r8`; `--sun heading altitude` overrides the world's sun, and `--mask` writes visibility masks for camera searches. The world is chosen at compile time (`TG_WORLD_FILE`, see `build.cmd`).

`--split <sky face size>` (with `--atlas`) renders the way `ATerragenSkybox` does, pass by pass (see [How the actor draws](#how-the-actor-draws)). `--refl-face <size>` adds the reflection atlas, and `--cloud-quality` and `--atmos-quality` set the sky passes' quality. The actor's defaults are:

```
Build\tg_render_Prototype2World.exe Textures/prototype2_v2_height.r16 Textures/noise128.r16 Out/split/p2_actor --atlas --clearance 0.1 --split 256 --refl-face 128 --cloud-quality 0.5 --atmos-quality 0.5 --cam 198 270 16.43 --heading 18 --clouds Textures/Prototype2World_clouds.r8
```

## How close it is

The five faces that show terrain were compared with your `Textures/MountainTop` crosses (Up is sky only in both). Overlap is how much the silhouettes coincide (1.0 = identical). Error is mean absolute difference per channel on a 0–255 scale. The last column is how far the face averages differ.

| Frame (height) | Silhouette overlap | Error | Average colour |
|---|---|---|---|
| 1 (start) | 0.994–1.000 | 6–16 | red/green within 6; blue up to 7 lower |
| 200 (+199 m) | 0.975–0.995 | 13–42 | red/green within 6; blue up to 13 lower |
| 400 (+399 m) | 0.951–0.993 | 12–50 | red/green within 6; blue up to 12 lower |

Most of the remaining per-pixel error is texture: the coverage speckle and bump are statistically the same, but not identical pixel for pixel.

Findings from the comparison:
- The renders used **`prototype2_v2.ter`** (flat). The curved `prototype2.ter` matches worse.
- `Output/*.bmp` holds a different scene (sky, clouds, reflective water). It is not the MountainTop reference; it is the sky reference below.
- The light values taken straight from the `.tgw` are already optimal in display space: sun = `LIGC × STRE`, ambient = `AMBC × BRSC`, exposure = `EXPO`, then tone mode `EXME` with `GAMA`.

### The sky world (Prototype2World)

`ClimbingGameAssets/Output/Sky_<Face>0001..0021.bmp` was rendered from `DynamicPrototype2/Prototype2World.tgw` (its snow and grass layers), on `prototype2_v2.ter`, with the camera at (198, 270, 16.43) heading 18, rising about 0.067 units per frame. The camera comes from matching the sky silhouettes; the mask overlap is 0.96.

Two settings differ from the saved world file, and both were recovered from the render itself:
- **The sun was at heading 24, altitude 48**, not 119/43. Two independent checks agree: the brightest sky (the glow through the clouds) is centred there, and the terrain shadows match.
- **"Clouds cast shadows" was on**; the file has it off. Without it, the cloud-shadowed slopes can't be reproduced.

`Worlds/Prototype2World.ush` is generated with those two overrides. Per-channel error against frame 1, all pixels, on a 0–255 scale:

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

### What is in the project

| Where | What |
|---|---|
| `Source/TerragenShaders/` | Module loaded at `PostConfigInit` that maps `/TerragenShaders` to this folder's `Shaders`, so materials can include the `.ush` files. Listed in `Prototype1.uproject` and both `*.Target.cs`. |
| `Source/Prototype1/.../Skybox/TerragenSkybox.h/.cpp` | `ATerragenSkybox`: renders the atlases and shows them on a sky sphere around the camera. |
| `/Game/MyStuff/Skybox/Terragen/` | `M_TerragenRender` (the terrain pass, a Custom node calling `TG_RenderAtlasTexelComposite`), `M_TerragenRenderSky` (the sky and reflection-sky passes, `TG_RenderAtlasTexelSky`), `M_TerragenSkyDisplay` (looks the view direction up in the atlas), `Textures/T_TerragenHeight_MountainTop` and `T_TerragenNoise` (G16, no mips, never streamed), and the `TerragenSkyTest` map. `RT_TerragenSky`, `RT_TerragenSkyOnly` and `RT_TerragenReflSky` are only the materials' default inputs; the actor makes its own render targets at run time. |

### How the actor draws

The cost is per pixel, so the actor splits the picture by how fast each part changes and draws only what is out of date:

| Atlas | Size | What | Redrawn |
|---|---|---|---|
| Sky | 1024² (256-px faces), RGBA8 | Sky, clouds and sun. Directions more than 5° below the horizon whose ray reaches the lowest ground (or the water) well inside the grid are skipped: the terrain always hides them. | When the camera has moved `SkyRedrawDistance` (20 m), `SkyTilesPerFrame` tiles a frame |
| Reflection | 512² (128-px faces), RGBA16f | The sky the water reflects, from water level below the camera, in linear colour. The terrain pass aims each lookup at the dome point its reflection really meets, so the clouds stay in place. | Like the sky, a face a frame |
| Terrain | 2048² (512-px faces), RGBA8 | Terrain and water. Rays that miss the terrain take the sky atlas's colour, so this atlas is the whole picture the sky sphere shows. | When the camera has moved 5 cm, in 128-px tiles: the ones in the player camera's view first (most out of date first), then the rest, nearest the view first, up to `TerrainTilesPerFrame` |

Visibility is a cone test per tile against the camera's view (half its diagonal field of view, plus `CullingMargin`). A 90° 16:9 view sees about 40 of the 96 terrain tiles, so with the default budget of 48 the whole view is redrawn every frame. A sky or reflection redraw marks the terrain tiles it affects as out of date.

Against a full render of every pixel, the split costs nothing visible: MountainTop is identical, and Prototype2World is within 3 levels per channel (from the reflection atlas and the halved cloud samples), with an unchanged error against Terragen. The sky cull is lossless, but saves little here: the skipped directions mostly miss the cloud map, and those were cheap already.

### Using it

Place an **ATerragenSkybox** where frame 1 of the old skybox was taken. That's the same point as `Base Altitude` in `BP_DynamicHeightSkybox`, because the actor's location is the Unreal point for `TerragenStart` (164, 223, 45.647). From there, the Terragen camera moves with the player in X, Y and Z, at true scale: one Terragen unit is 1500 cm. The terrain is 513 × 513 points, a 7.7 km square; beyond its edges there is no terrain.

| Setting | Default | Meaning |
|---|---|---|
| `TerragenWorld` | none | The world to show: a `DA_TerragenWorld_<Name>` asset. It sets the materials, `TerragenStart`, `CentimetresPerUnit` and the headings, which are then greyed out. Without one, set those by hand (the defaults are MountainTop's). |
| `SunLight` | none | A directional light to turn to the world's sun whenever the world is set. It must be Movable or Stationary to turn during play. |
| `FollowHorizontal` | on | Off matches the old skyboxes, which only changed with altitude. |
| `KeepAboveTerrain`, `TerrainClearance` | on, 150 cm | Where your level and the Terragen terrain don't line up, the player can be below the Terragen ground. This lifts the Terragen camera to the clearance above the ground (or water) there, instead of rendering the inside of the terrain. |
| `HeadingAtPlusX` | 18 | Which Terragen heading Unreal +X looks at. 18 puts the old Front face along +X. |
| `StartDistance` | 0 | Leaves out terrain nearer than this many Terragen units, so real level geometry can stand in for it. |
| `Projection` | Cube | The terrain atlas's layout; see [Cube or sphere map](#cube-or-sphere-map). |
| `TerrainAtlasSize`, `TilesPerFaceSide` | 2048, 4 | The terrain atlas: 512-px faces in 128-px tiles. On a 1080p screen a 90° view spreads each texel over about 4 pixels, which is the softness you see; 4096 (with 8 tiles per side) is sharper and costs about 3.5× as much per frame. |
| `SkyAtlasSize`, `SkyTilesPerFaceSide` | 1024, 2 | The sky atlas. The sky is soft, so it can be smaller. |
| `ReflectionAtlasSize` | 512 | The reflection atlas. 0 computes every reflection exactly in the terrain pass instead. |
| `CloudQuality`, `AtmosQuality` | 0.5, 1 | Scale the sky passes' cloud samples and atmosphere steps (1 = the world's own). CloudQuality 0.5 stays within 2 levels. AtmosQuality 0.5 is Terragen's atmosphere accuracy one notch lower, which also coarsens the cloud-shadow grid: about 40% off the sky pass, but the sky under the clouds comes out up to 10 levels brighter. |
| `TerrainRedrawDistance` | 5 cm | Camera movement before a terrain tile is redrawn. |
| `SkyRedrawDistance` | 20 m | Camera movement before a sky or reflection tile is redrawn. |
| `TerrainTilesPerFrame`, `SkyTilesPerFrame`, `ReflectionFacesPerFrame` | 48, 2, 1 | Budgets per frame. The very first draw always does everything. |
| `CullingMargin` | 5° | Extra angle around the view when deciding which tiles it can see. |
| `SkyRadius` | 10 km | Radius of the sky sphere. Level geometry beyond it is hidden. |

In the editor, the **Redraw From Actor Location** button on the actor previews the sky.

### Cube or sphere map

The picture is shown on `SkyMesh`, the engine's basic sphere scaled to `SkyRadius` and kept centred on the camera. The mesh only carries the picture: its material (`M_TerragenSkyDisplay`) turns each pixel's view direction into a lookup in the terrain atlas, so a cube or any other closed mesh would show exactly the same image. What does matter is how the atlas is laid out, which `Projection` sets:

| | Cube | Sphere |
|---|---|---|
| Layout | Six 90° faces in the old skyboxes' cross (`TerrainAtlasSize`² with 6 of 16 squares used) | Equirectangular, `TerrainAtlasSize` × half that: longitude across, from up to down vertically |
| Sharpness | Varies across each face: a texel at a face's centre covers about twice the angle of one at its edge, so detail visibly sharpens and softens as the view turns | The same all round the horizon (0.18° per pixel at 2048); finer towards straight up and down |
| Seams | Filtering stops at every cube edge, which can show as faint lines that move as you turn | One, directly behind, and filtering wraps across it |
| Cost | 1.57 M pixels at 2048 | 2.1 M pixels at 2048 (see the timings) |

The sky and reflection atlases stay cube crosses either way: they are only looked up by direction, never shown directly. The display samples the atlas at mip level 0 directly, without screen derivatives, so UV jumps at face edges and at the seam can't make the filtering smear.

### Switching worlds

Each world is one `UTerragenWorld` asset (`DA_TerragenWorld_MountainTop`, `DA_TerragenWorld_Prototype2World`). To switch:
- **In the editor:** pick another asset in the actor's `TerragenWorld`, then press **Redraw From Actor Location**.
- **During play:** call `SetTerragenWorld` (Blueprint or C++). The next frame redraws everything once, about 150 ms for Prototype2World, then it is back to the usual per-frame cost.

Why an asset and not a setting: a world's terrain layers, sun, water and sky are compiled into its materials' shaders (from `Shaders/Worlds/<Name>.ush`). That keeps each world as fast as its features allow (MountainTop's shaders contain no sky code at all), but it means each world has its own materials. The asset bundles them with the world's placement.

### Adding a world

1. Add an entry to `Tools/worlds.json`: the `.tgw`, its `.ter`, any overrides, the start camera, heading and sun.
2. `python Tools/build_world.py <Name>` generates `Shaders/Worlds/<Name>.ush` (and the cloud map, the terrain's height texture if new).
3. With the editor closed, run `Tools/ue_setup.py` with `TG_WORLD=<Name>` (see below). It makes the materials, `DA_TerragenWorld_<Name>` and a `TerragenSkyTest_<Name>` map, and checks the GPU render.
4. Optionally add the world to `Tests/build.cmd` and `Tests/dxc_check.hlsl` for the CPU harness and compile checks.

### Recreating the assets

Run `Tools/ue_setup.py` inside the editor. Python only needs enabling for that run; the project itself doesn't need the plugin:

```
UnrealEditor.exe Prototype1.uproject -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
    -ExecutePythonScript="<project>/ClimbingGameAssets/TerragenShader/Tools/ue_setup.py" -RenderOffscreen -unattended
    -ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorLoadingSavingSettings]:RestoreOpenAssetTabsOnRestart=NeverRestore
```

The last option stops the editor reopening last session's asset tabs. With those tabs open, an offscreen editor crashed on exit in the Skeletal Mesh editor; it didn't affect the assets.

To build the sky world instead, set `TG_WORLD=Prototype2World` for that run. It adds assets with a `_Prototype2World` suffix:
- `M_TerragenRender_Prototype2World` and `M_TerragenRenderSky_Prototype2World`, with a `CloudTex` input whose default is `Textures/T_TerragenClouds_Prototype2World`
- `M_TerragenSkyDisplay_Prototype2World` and the default render targets
- `DA_TerragenWorld_Prototype2World`, which bundles those for the actor
- the `TerragenSkyTest_Prototype2World` map, with an `ATerragenSkybox` using it

It also exports `Out/ue/Prototype2World/`. The MountainTop assets are left alone: the height and noise textures are shared, and only imported if missing. To use the sky world in another level, set a placed actor's `TerragenWorld` to `DA_TerragenWorld_Prototype2World`.

Both runs also time each pass on the GPU and write `timing.txt` next to the exported atlases.

Shader cost: the CPU harness renders the full 2048² atlas in 2.0 s for MountainTop and 3.2 s for Prototype2World (24 threads). FXC compiles the Prototype2World material in about 30 s, DXC in 1 s.

### Verified on this machine

| Check | Result |
|---|---|
| GPU atlas vs the CPU harness, frames 1 and 400 | Silhouette overlap 1.000; colour within 1.1 levels (float rounding in the bump and speckle) |
| GPU atlas vs Terragen | Overlap 0.996 at frame 1 and 0.986 at frame 400, the same as the CPU harness |
| Level-viewport screenshots vs Terragen (`ue_view_check.py`) | Overlap 0.94–0.99 on the five faces with terrain; correct orientation, no mirroring |
| Following the player: 300 m along Unreal +X, and 1 km below the start (lifted above the ground) | Unreal→Terragen mapping exact; GPU vs CPU overlap 1.000 on every face, colour within 2.3 levels |
| Builds | Development and DebugGame editor targets. The game target too, with one caveat below |
| Cost of a full six-face redraw of the 2048² atlas, RTX 4060 Ti | 30 ms on an idle GPU (about 5 ms per face); 59 ms while a game was also using the GPU |
| Sky world (`TG_WORLD=Prototype2World`): GPU atlas vs the CPU harness | Colour within 1.7 levels per channel, identical face averages |
| Sky world: GPU atlas vs Terragen (`Output/Sky_*0001.bmp`) | Error 18–45 levels per channel, the same as the CPU harness |
| Sky world: full six-face redraw, idle RTX 4060 Ti | 148 ms, about 5× MountainTop (about 25 ms per face) |
| Sky world: level-viewport screenshots (`TG_WORLD=Prototype2World` with `ue_view_check.py`) | Sky, clouds, haze and rippled water all render; the image is brighter than Terragen's because UE's tonemapper runs on top |
| Split rendering with the actor's defaults (sky, reflection and terrain atlases), GPU vs the CPU harness `--split 256 --refl-face 128 --cloud-quality 0.5` | MountainTop within 1.1 levels, Prototype2World within 1.8; error against Terragen unchanged (MountainTop overlap 0.996, Prototype2World 20–45) |
| Cost per pass on an idle RTX 4060 Ti, Prototype2World (MountainTop) | Visible terrain tiles of a 90° 16:9 view, the per-frame cost while moving: 11.5 ms (8.1 ms). Whole terrain atlas 33 ms (23 ms). Whole sky atlas 86 ms, about 3.6 ms a tile; whole reflection atlas 35 ms, about 6 ms a face; both only while catching up after 20 m of movement |
| The same view with reflections computed exactly (`ReflectionAtlasSize` 0) | 29 ms; the whole terrain atlas 98 ms |
| The same view with a 4096 terrain atlas | 40.5 ms (28.5 ms) |
| The same view with `Projection` Sphere | 11.2 ms (7.6 ms), slightly less than the cube: its pixels are spread evenly, so fewer land in a view. The whole atlas costs more: 39 ms (27 ms) |
| Sphere-map viewport screenshots vs cube ones (`TG_PROJECTION=sphere` with `ue_view_check.py`), Prototype2World | Same image and orientation on all six views, face averages within 1 level; 7–10 levels per pixel from sampling the fine texture at different points |
| Culled vs full screenshots (`TG_CULLTEST=1`), cube and sphere | Identical |

### Things to know

- **Post-processing.** The sky material decodes the atlas as sRGB, so its emissive equals what the old sRGB skybox textures gave. UE's tonemapper, bloom and temporal AA still run on top, as they did for the old textures, so the screen isn't byte-identical to Terragen. Two optional tweaks:
  - Nearest filtering on `RT_TerragenSky` keeps the texels crisp.
  - A post-process material in front of the tonemapper would be needed to show Terragen's exact bytes.
- **Async builds in the editor.** Right after loading, the height texture is still building. The actor waits for its textures and shaders before drawing, because the placeholder texture would render the whole sky as snow.
- **Game target.** `Prototype1.Build.cs` now adds `UnrealEd` only for editor builds (`if (Target.bBuildEditor)`), which the game target needs; with it, the game target built cleanly.

## Future work: authoring worlds in the editor

Today a world starts life as a Terragen 0.9 `.tgw` + `.ter` made in Terragen itself, and goes through `build_world.py` (Python, outside the editor) and `ue_setup.py`. The goal is an in-editor tool that makes our own worlds without Terragen:

- **A world editor panel** (Editor Utility Widget or a Slate plugin) to edit what `tgw_to_ush.py` reads: sun, light and ambient colours, exposure and tone curve, atmosphere layers, cloud layer (seed, persistence, feature size, altitude, density), water level and waves, and the surface-layer tree (colours, altitude and slope limits, coverage, bump, noise).
- **Terrain** from a heightmap import, or sculpted with Unreal's Landscape tools and exported to the 513² height texture (the research on in-world Terragen terrain covers using Landscape).
- **Output** in the same form the pipeline already uses: a `worlds.json` entry plus a generated `Shaders/Worlds/<Name>.ush` and cloud map, then the `ue_setup.py` steps. Optionally a `.tgw`/`.ter` writer too, so a world can still be opened in Terragen to compare.
- **Live preview** while editing. Compiled worlds need a shader recompile per change (FXC takes about 30 s for a sky world), so the tool would want a "runtime world" variant: the same shader reading the `TGWorld` values from material parameters or a small data texture instead of constants. Slower per pixel, but instant to tweak; bake to a compiled world when done.

The pieces that make this practical are already in place: `tgw_to_ush.py` documents every field and its source in the DLL, `make_cloud_map.py` reproduces Terragen's generator exactly, and `UTerragenWorld` is the hand-off point to the actor.
