# Terragen

An Unreal Engine 5 plugin that renders Terragen 0.9.43 worlds live, in a shader, as a skybox that follows the player, and gives ordinary level meshes the same look.

`Shaders/Terragen.ush` re-implements how Terragen 0.9.43 renders terrain, using the formulas and constants decompiled from `Terragen.dll`:
- the heightfield surface
- the surface-layer tree with its coverage and bump texture
- sun, ambient and soft shadows
- the atmosphere (haze, blue and red layers, glow, sunlight penetration)
- the sky dome with the sun disc and the cloud layer, including cloud shadows
- water with waves, reflections, the sun's reflection and a see-through shallow bed
- the exposure/tone curve

Terragen rasterises micro-polygons; the shader ray-marches the same surface per pixel instead. Both find the same visible point.

The same `.ush` also compiles as C++ (`Tests/hlsl_shim.h`), so `Tests/tg_render` can render it on the CPU and compare it with real Terragen output. How close it gets, what is exact and what is approximated, and everything that was measured: [Docs/Validation.md](Docs/Validation.md).

## Contents

- [Plugin layout](#plugin-layout)
- [Installing](#installing)
- [The skybox actor](#the-skybox-actor)
- [Worlds](#worlds)
- [Meshes with the Terragen look](#meshes-with-the-terragen-look)
- [Regenerating and testing](#regenerating-and-testing)
- [Future work: authoring worlds in the editor](#future-work-authoring-worlds-in-the-editor)

## Plugin layout

| Path | What |
|---|---|
| `Terragen.uplugin` | Two runtime modules (below) and the content. |
| `Source/Terragen/` | `ATerragenSkybox` (renders the atlases and shows them on a sky sphere around the camera), `UTerragenWorld` (one world's materials and placement), the `LogTerragen` category. |
| `Source/TerragenShaders/` | Loads at `PostConfigInit`, before any material compiles, and maps `/TerragenShaders` to `Shaders/` so materials can include the `.ush` files. |
| `Shaders/Terragen.ush` | The renderer. Main entry points: `TG_RenderRay`, `TG_ShadeRay`, `TG_CameraRay`. |
| `Shaders/TerragenUE.ush` | Unreal glue: the skybox cross atlas, UE↔Terragen directions, sRGB decode. |
| `Shaders/TerragenMesh.ush` | Terragen's shading on ordinary meshes and the look post-process's maths; see [Meshes with the Terragen look](#meshes-with-the-terragen-look). |
| `Shaders/TerragenPerlin.ush` | Terragen's Perlin tables (`srand(0)` permutation and gradients), for the cloud and wave noise. |
| `Shaders/Worlds/<World>.ush` | Generated per world by `Tools/build_world.py`: sun, light, water, sky, tone settings and surface layers. |
| `Content/` | The Unreal assets, mounted at `/Terragen/`; see [Content](#content). |
| `Data/Worlds/` | The Terragen source files the worlds are built from (`.tgw` world files, `.ter` terrain). |
| `Data/Textures/` | Generated from them, and imported by `Tools/ue_setup.py`: `<terrain>_height.png` (lossless 16-bit heightfield, `.r16` the same data raw for the harness, `.json` its metadata), `noise128.png/.r16` (Terragen's value-noise lattice: `srand(1)`, 16384 × `rand()`), `<World>_clouds.png/.r8` (the world's 513² cloud map, 8-bit, value + 128). |
| `Tools/` | Python tools, stock Python 3 with no packages; see [Tools](#tools). |
| `Tests/` | The CPU harness (`tg_render.cpp`, `tg_mesh_test.cpp`) and HLSL compile checks (`dxc_check.hlsl`); `build.cmd` builds them. |
| `Docs/Validation.md` | Accuracy against Terragen, GPU against CPU, and timings. |
| `Build/`, `Out/` | Not in source control: the harness's executables, and every tool's renders, comparisons and logs. |

### Content

| Folder | Assets |
|---|---|
| `Shared/` | `MPC_Terragen`: the placement the mesh materials and the look read; the skybox actor writes it. |
| `Shared/Textures/` | `T_TerragenNoise`, `T_TerragenHeight_<terrain>` (G16, no mips, never streamed; one per `.ter`, shared by every world on it), `T_TerragenWhiteCube` (the sky light's constant cubemap). |
| `Shared/PostProcess/` | `PP_TerragenSoften` and its instances `MI_TerragenSoftenPlain`, `MI_TerragenSoftenNoJitter`: no world compiled in, so one serves every world. |
| `Worlds/<World>/` | Everything compiled for one world, each name ending in `_<World>`: `DA_TerragenWorld`, the render, sky and display materials (`M_TerragenRender`, `M_TerragenRenderSky`, `M_TerragenSkyDisplay`), their default render targets (`RT_TerragenSky`, `RT_TerragenSkyOnly`, `RT_TerragenReflSky`), the mesh materials (`M_TerragenMesh`, `M_TerragenMeshUnlit`, `M_TerragenSunLight`), the look (`PP_TerragenLook`, `MI_TerragenLookSharp`) and, for sky worlds, the cloud map `T_TerragenClouds`. |
| `Worlds/<World>/Debug/` | `M_TerragenMeshProbe`, `M_TerragenMeshCheck`: only the setup scripts' measurements use them. |
| `Maps/` | `TerragenSkyTest_<World>` (the skybox on a floor) and `TerragenMeshTest_<World>` (lit and unlit meshes under the look). |

The render targets are only the materials' default inputs; the actor makes its own at run time.

### Tools

| Tool | What |
|---|---|
| `Tools/worlds.json` | The registry of worlds: world file, terrain, overrides, start camera, sun. Every tool reads it. |
| `Tools/build_world.py` | Generates a registered world's `Shaders/Worlds/<Name>.ush` and cloud map (runs the three tools below). |
| `Tools/ter_convert.py` | `.ter` → height texture + `.json` metadata. |
| `Tools/tgw_to_ush.py` | `.tgw` + terrain `.json` → `Shaders/Worlds/<Name>.ush`. |
| `Tools/make_cloud_map.py` | `.tgw` → `<World>_clouds.png/.r8`, generated exactly like Terragen (seed, persistence, feature size). |
| `Tools/make_noise_table.py` | Writes `noise128.png/.r16`. |
| `Tools/make_perlin_tables.py` | Writes `Shaders/TerragenPerlin.ush`. |
| `Tools/unpack_cross.py` | Splits a 2048² skybox cross back into six faces. |
| `Tools/compare.py` | Side-by-side images and metrics against Terragen; `--calibrate` fits light scales. |
| `Tools/ue_setup.py` | Run inside the editor: creates a world's sky assets and test map, renders on the GPU and exports the atlas. |
| `Tools/ue_mesh_setup.py` | Run inside the editor after `ue_setup.py`: creates the mesh materials, the look post-process and a test map, and checks them on the GPU. |
| `Tools/ue_view_check.py` | Run inside the editor: six screenshots of the sky from the level viewport, plus a quiet-GPU redraw timing. `TG_CULLTEST=1` draws only the tiles the culling picks before each shot; `TG_PROJECTION=sphere` uses the sphere map. |
| `Tools/ue_profile.py` | Run inside the editor: times each pass with one feature switched off at a time, to see where the GPU time goes. |
| `Tools/ue_common.py` | What the `ue_*.py` tools share: the plugin's paths, the content layout and the world picked with `TG_WORLD`. |

## Installing

Copy `Plugins/Terragen` into a project's `Plugins` folder (or the engine's) and enable it. It needs a C++ build (the two modules) and nothing else; the tools run with Python enabled only for that run (see [Recreating the assets](#recreating-the-assets)).

In this project, `Config/DefaultEngine.ini` carries class redirects from `/Script/Prototype1` to `/Script/Terragen`, because the actor and the world asset started out in the game module.

## The skybox actor

### How it draws

The cost is per pixel, so the actor splits the picture by how fast each part changes and draws only what is out of date:

| Atlas | Size | What | Redrawn |
|---|---|---|---|
| Sky | 1024² (256-px faces), RGBA8 | Sky, clouds and sun. Directions more than 5° below the horizon whose ray reaches the lowest ground (or the water) well inside the grid are skipped: the terrain always hides them. | When the camera has moved `SkyRedrawDistance` (20 m), `SkyTilesPerFrame` tiles a frame |
| Reflection | 512² (128-px faces), RGBA16f | The sky the water reflects, from water level below the camera, in linear colour. The terrain pass aims each lookup at the dome point its reflection really meets, so the clouds stay in place. | Like the sky, a face a frame |
| Terrain | 2048² (512-px faces), RGBA8 | Terrain and water. Rays that miss the terrain take the sky atlas's colour, so this atlas is the whole picture the sky sphere shows. | When the camera has moved 5 cm, in 128-px tiles: the ones in the player camera's view first (most out of date first), then the rest, nearest the view first, up to `TerrainTilesPerFrame` |

Visibility is a cone test per tile against the camera's view (half its diagonal field of view, plus `CullingMargin`). A 90° 16:9 view sees about 40 of the 96 terrain tiles, so with the default budget of 48 the whole view is redrawn every frame. A sky or reflection redraw marks the terrain tiles it affects as out of date.

Against a full render of every pixel, the split costs nothing visible: MountainTop is identical, and Prototype2World is within 3 levels per channel (from the reflection atlas and the halved cloud samples), with an unchanged error against Terragen. The sky cull is lossless, but saves little here: the skipped directions mostly miss the cloud map, and those were cheap already.

### Using it

Place an **ATerragenSkybox** and set its `TerragenWorld`. The actor's location is the Unreal point for the world's `TerragenStart`; from there, the Terragen camera moves with the player in X, Y and Z, at true scale (`CentimetresPerUnit`, 1500 cm for both worlds here). The terrain is 513 × 513 points, a 7.7 km square; beyond its edges there is no terrain.

In this project, that's where frame 1 of the old skybox was taken: the same point as `Base Altitude` in `BP_DynamicHeightSkybox`.

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

The picture is shown on `SkyMesh`, the engine's basic sphere scaled to `SkyRadius` and kept centred on the camera. The mesh only carries the picture: its material (`M_TerragenSkyDisplay_<World>`) turns each pixel's view direction into a lookup in the terrain atlas, so a cube or any other closed mesh would show exactly the same image. What does matter is how the atlas is laid out, which `Projection` sets:

| | Cube | Sphere |
|---|---|---|
| Layout | Six 90° faces in the old skyboxes' cross (`TerrainAtlasSize`² with 6 of 16 squares used) | Equirectangular, `TerrainAtlasSize` × half that: longitude across, from up to down vertically |
| Sharpness | Varies across each face: a texel at a face's centre covers about twice the angle of one at its edge, so detail visibly sharpens and softens as the view turns | The same all round the horizon (0.18° per pixel at 2048); finer towards straight up and down |
| Seams | Filtering stops at every cube edge, which can show as faint lines that move as you turn | One, directly behind, and filtering wraps across it |
| Cost | 1.57 M pixels at 2048 | 2.1 M pixels at 2048 (see [the timings](Docs/Validation.md#in-unreal)) |

The sky and reflection atlases stay cube crosses either way: they are only looked up by direction, never shown directly. The display samples the atlas at mip level 0 directly, without screen derivatives, so UV jumps at face edges and at the seam can't make the filtering smear.

### Things to know

- **Post-processing.** The sky material decodes the atlas as sRGB, so its emissive equals what the old sRGB skybox textures gave. UE's tonemapper, bloom and temporal AA still run on top, as they did for the old textures, so the screen isn't byte-identical to Terragen. Two optional tweaks:
  - Nearest filtering on `RT_TerragenSky_<World>` keeps the texels crisp.
  - `PP_TerragenLook_<World>` replaces the tonemapper and shows the sky as the atlas's bytes; see [Meshes with the Terragen look](#meshes-with-the-terragen-look).
- **Async builds in the editor.** Right after loading, the height texture is still building. The actor waits for its textures and shaders before drawing, because the placeholder texture would render the whole sky as snow.
- **The sky at a lifted camera.** Where the Terragen ground is above the level, `KeepAboveTerrain` lifts the Terragen camera onto it. At one such spot in Prototype2World the sky came out black in both the CPU harness and the GPU (they agree to 0.8 levels), so it is the renderer's own behaviour there.

## Worlds

| World | Built from | |
|---|---|---|
| `MountainTop` | `Data/Worlds/WorldNewLocationAndAtmosphere.tgw` on `prototype2_v2.ter` | Sun, light, water, tone settings and the 5 surface layers. Sky off. |
| `Prototype2World` | `Data/Worlds/Prototype2World.tgw` on `prototype2_v2.ter` | With the sun and cloud shadows recovered from its reference render ([Docs/Validation.md](Docs/Validation.md#the-sky-world-prototype2world)). Sky, clouds, atmosphere and water on. |

### Switching worlds

Each world is one `UTerragenWorld` asset (`Worlds/<World>/DA_TerragenWorld_<World>`). To switch:
- **In the editor:** pick another asset in the actor's `TerragenWorld`, then press **Redraw From Actor Location**.
- **During play:** call `SetTerragenWorld` (Blueprint or C++). The next frame redraws everything once, about 150 ms for Prototype2World, then it is back to the usual per-frame cost.

Why an asset and not a setting: a world's terrain layers, sun, water and sky are compiled into its materials' shaders (from `Shaders/Worlds/<Name>.ush`). That keeps each world as fast as its features allow (MountainTop's shaders contain no sky code at all), but it means each world has its own materials. The asset bundles them with the world's placement.

### Adding a world

1. Put its `.tgw` (and `.ter`, if new) in `Data/Worlds/` and add an entry to `Tools/worlds.json`: the files, any overrides, the start camera, heading and sun.
2. `python Tools/build_world.py <Name>` generates `Shaders/Worlds/<Name>.ush` (and the cloud map, and the terrain's height texture if new).
3. With the editor closed, run `Tools/ue_setup.py` with `TG_WORLD=<Name>` (see below). It makes `Worlds/<Name>/`, with `DA_TerragenWorld_<Name>`, and `Maps/TerragenSkyTest_<Name>`, and checks the GPU render.
4. For meshes, run `Tools/ue_mesh_setup.py` the same way.
5. Optionally add the world to `Tests/build.cmd` and `Tests/dxc_check.hlsl` for the CPU harness and compile checks.

### Recreating the assets

Run `Tools/ue_setup.py` inside the editor. Python only needs enabling for that run; the project itself doesn't need it:

```
UnrealEditor.exe <project>.uproject -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
    -ExecutePythonScript="<project>/Plugins/Terragen/Tools/ue_setup.py" -RenderOffscreen -unattended
    -ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorLoadingSavingSettings]:RestoreOpenAssetTabsOnRestart=NeverRestore
```

`TG_WORLD=<Name>` picks the world (default `MountainTop`). The last option stops the editor reopening last session's asset tabs: with those tabs open, an offscreen editor crashed on exit in the Skeletal Mesh editor; it didn't affect the assets.

Each run rebuilds that world's folder and test map, and leaves other worlds alone. The shared textures are imported only when missing (`TG_REIMPORT_SHARED=1` reimports them). It renders the world's check frames on the GPU, times each pass and exports them, with `timing.txt`, to `Out/ue/<World>/`. `TG_SETUP_NO_QUIT=1` keeps the editor open afterwards.

Shader cost: FXC compiles the Prototype2World material in about 30 s, DXC in 1 s.

## Meshes with the Terragen look

`Shaders/TerragenMesh.ush` shades ordinary level geometry the way the terrain is shaded, so props and level meshes match the picture behind them. Everything Terragen does after finding the visible point needs only a position and a normal, so it carries over:

| Terragen part | On a mesh |
|---|---|
| Surface-layer tree (snow, grass, rock: noise, altitude, slope) | `TG_MeshSurface`: altitude is the mesh point's own Terragen z, slope comes from its normal. Coverage noise reads the same world x, y as the terrain, so patches line up with the terrain behind. |
| Bump | Projected on the axis plane nearest the normal, blended across the 45° boundaries (up to three planes near a cube-corner direction). For upward faces up to about 38° this is exactly the terrain's own x, y plane. |
| Level of detail | The footprint uses the terrain atlas's pixel angle (2 / face size), not the screen's, so meshes stop at the same detail as the picture. |
| Sun, soft shadow, ambient | Lit material: Unreal's Lambert diffuse with lights set to Terragen's values (below), with Unreal's own shadows. Unlit material: Terragen's light equation exactly, no mesh shadows. |
| Cloud shadows | The directional light's light function (`M_TerragenSunLight_<World>`) evaluates the clouds' share of the sun at each lit point, so cloud shadows move over meshes as over the terrain. |
| Atmosphere and tone curve | `PP_TerragenLook_<World>`, a post-process that replaces the tonemapper: Terragen's atmosphere march from the camera to each pixel's depth, then the world's exposure and tone curve. The sky sphere's pixels pass through as the atlas's own bytes. |
| The soft, low-res look | The same post-process samples the screen at the four terrain-atlas texel centres around each pixel's direction and blends them bilinearly, as the sky sphere does. Meshes then sit on exactly the atlas's texel grid. `Softness` 0 keeps screen resolution. |

### Using it

1. Run `Tools/ue_setup.py` for the world (if not done), then `Tools/ue_mesh_setup.py` with the same command line and `TG_WORLD=<Name>`. It makes the assets below, fills in `DA_TerragenWorld_<Name>`, checks everything on the GPU and builds `Maps/TerragenMeshTest_<Name>`.
2. Put `M_TerragenMesh_<Name>` on any mesh. It needs no UVs or textures. Its parameters are `Tint`, `DetailScale` (2 = half the detail, cheaper) and `MaxLevels` (caps the finest bump level; 23 = Terragen's own).
3. On the `ATerragenSkybox`, set `SunLight` and `SkyLight` (both Movable). With `DriveLights` on (the default), setting the world gives the sun π × the world's sun colour, its light function and the sun's angular size. The sky light gets a constant cubemap (`T_TerragenWhiteCube`) tinted to the world's ambient, with no lower-hemisphere black and no distance-field shadowing.
4. Add an unbound post-process volume with `PP_TerragenLook_<Name>` as a blendable. Turn global illumination, reflections and ambient occlusion off in it: Terragen has none, and the light values assume none. The test maps show the settings.

The actor writes its placement to `MPC_Terragen` (start, its own location, cm per unit, headings, atlas face size and projection, sky radius), which all the mesh materials read. One skybox per level.

| Asset | What |
|---|---|
| `M_TerragenMesh_<World>` | Lit: BaseColor and a world-space Normal from the layer tree; specular 0, roughness 1. |
| `M_TerragenMeshUnlit_<World>` | Terragen's own light, linear, for the look. `Standalone` = 1 adds the atmosphere and tone curve itself, for scenes without the look. `TerrainShadows` = 1 adds the Terragen terrain's shadow (only right where the level lines up with the Terragen ground). |
| `M_TerragenSunLight_<World>` | The sun's light function: cloud shadow, or the uniform cloud dimming in worlds without cloud shadows. |
| `PP_TerragenLook_<World>`, `MI_TerragenLookSharp_<World>` | The look; the instance has `Softness` 0. `Segments` caps the atmosphere march's steps (24 reaches the 10 km sky sphere in both worlds without capping). |
| `Shared/PostProcess/PP_TerragenSoften` | Only the softness, after Unreal's tonemapper: everything but the sky sphere goes on the atlas's texel grid. The sky's pixels are left exactly as Unreal draws them, and Unreal's tonemapper, bloom and grading stay. Use it instead of `PP_TerragenLook_<World>` when you want the softness without Terragen's tone curve and haze. Parameters: `Softness`; `Prefilter` (1 = each texel is the average of its footprint on screen, from four bilinear reads; 0 = one point); `Jitter` (how far inside its texel each texel is read, 1 = anywhere, as Terragen jitters its shading points; the offset is fixed per texel, so it doesn't flicker). |
| `Shared/PostProcess/MI_TerragenSoftenPlain`, `MI_TerragenSoftenNoJitter` | `PP_TerragenSoften` without prefilter and jitter, and without jitter, for comparison. |
| `Worlds/<World>/Debug/M_TerragenMeshProbe_<World>`, `M_TerragenMeshCheck_<World>` | Measurement only (the setup script's light probe and GPU check). |

### Why the light values work

Unreal's Lambert term is albedo / π × irradiance, Terragen's is albedo × light colour. So a directional light of π × the sun colour (lux) gives Terragen's sun term. A sky light whose cubemap has a constant radiance L gives albedo × L, which is Terragen's single-colour ambient. The look reads the scene colour without Unreal's pre-exposure, so Unreal's exposure settings don't matter.

The light colour is the sun through the atmosphere at `TerragenStart`, measured on the GPU (`TG_MeshLightProbe`); its change with altitude over a level is ignored. The clouds come per pixel from the light function. Multi-directional ambient is averaged to one constant (exact for both worlds, which use a single colour).

### Things to know

- **Cost.** The layer tree is the terrain pass's own, per pixel, at about 13 ms for a whole 1080p screen. Meshes covering a third of the screen cost a third of that. `DetailScale` and `MaxLevels` trade detail for speed. Baking the material into textures for static props is the cheap alternative.
- **The look replaces the tonemapper for everything** in its volume, Terragen materials or not. Bloom, Unreal's tone curve and colour grading are gone; UI is unaffected.
- **Sky detection.** The look treats pixels at 98% of `SkyRadius` or more as sky. In the editor without play the sphere stays on the actor, not the camera, so far from the actor some sky pixels can fall short of that and get toned twice.
- **Mesh textures and normal maps** are not used: the surface comes from the world's layers. `Tint` is the only per-mesh control so far.
- **Steep faces** take their bump from a side plane, so a cliff mesh doesn't continue the terrain's exact bump pattern (its colours do continue).
- **Editor exit.** In 2 of 11 runs, the offscreen editor exited with 0xC0000409 after its log had closed, with everything already saved and no crash report. Not traced.

## Regenerating and testing

From the plugin's folder:

```
python Tools/ter_convert.py Data/Worlds/prototype2_v2.ter -o Data/Textures
python Tools/make_noise_table.py -o Data/Textures
python Tools/make_perlin_tables.py
python Tools/build_world.py --all
Tests\build.cmd
```

`build_world.py` runs `tgw_to_ush.py` (and `make_cloud_map.py` for worlds with clouds) with each world's options from `Tools/worlds.json`; the output is byte-identical to the committed files. `--out <dir>` writes elsewhere, to check that.

`--pre-sky` keeps MountainTop rendering exactly as it was validated before the sky work: the cloud dimming fades in over the cloud depth (the DLL uses the atmosphere step, which would shift MountainTop's frame-1 error by about ±1 level per channel) and the water is a flat lit plane.

`Tests/build.cmd` builds `Build/tg_render.exe` (MountainTop), `Build/tg_render_Prototype2World.exe` and the matching `tg_mesh_test` executables with MSVC, and checks the shaders compile with DXC (SM6.6) and FXC (SM5): the sky lookup, the atlas for both worlds, and the exact Custom-node calls of the Unreal materials.

### The CPU harness

The reference renders are this project's, outside the plugin (`ClimbingGameAssets/Textures/MountainTop` for MountainTop, `ClimbingGameAssets/Output` for Prototype2World):

```
Build\tg_render.exe Data/Textures/prototype2_v2_height.r16 Data/Textures/noise128.r16 Out/frame0001
python Tools/unpack_cross.py ../../ClimbingGameAssets/Textures/MountainTop/Sky_MountainSide_Alt_0001.png Out/reference 0001
python Tools/compare.py Out/reference 0001 Out/frame0001
Build\tg_render_Prototype2World.exe Data/Textures/prototype2_v2_height.r16 Data/Textures/noise128.r16 Out/sky_p2w --cam 198 270 16.43 --heading 18 --clouds Data/Textures/Prototype2World_clouds.r8
python Tools/compare.py ../../ClimbingGameAssets/Output 0001 Out/sky_p2w
```

`tg_render` takes `--cam x y z` (Terragen units), `--heading`, `--size` and `--faces`. For MountainTop frame N, the camera is `164 223 (45.647 + (N-1) * 0.067)`. With `--atlas` it renders the whole 2048² cross through the same function the Unreal material uses, and checks that the sky lookup inverts the atlas exactly. Sky worlds also take `--clouds <World>_clouds.r8`; `--sun heading altitude` overrides the world's sun, and `--mask` writes visibility masks for camera searches. The world is chosen at compile time (`TG_WORLD_FILE`, see `build.cmd`).

`--split <sky face size>` (with `--atlas`) renders the way `ATerragenSkybox` does, pass by pass (see [How it draws](#how-it-draws)). `--refl-face <size>` adds the reflection atlas, and `--cloud-quality` and `--atmos-quality` set the sky passes' quality. The actor's defaults are:

```
Build\tg_render_Prototype2World.exe Data/Textures/prototype2_v2_height.r16 Data/Textures/noise128.r16 Out/split/p2_actor --atlas --clearance 0.1 --split 256 --refl-face 128 --cloud-quality 0.5 --atmos-quality 0.5 --cam 198 270 16.43 --heading 18 --clouds Data/Textures/Prototype2World_clouds.r8
```

`Build/tg_mesh_test[_Prototype2World].exe` checks `TerragenMesh.ush` against the terrain renderer. Run it with the height and noise files (and `--clouds` for sky worlds); it prints PASS/FAIL per check. With `--gpu Out/ue/<World>/mesh/mesh_gpu.txt` it compares `ue_mesh_setup.py`'s GPU read-back with the CPU instead.

## Future work: authoring worlds in the editor

Today a world starts life as a Terragen 0.9 `.tgw` + `.ter` made in Terragen itself, and goes through `build_world.py` (Python, outside the editor) and `ue_setup.py`. The goal is an in-editor tool that makes our own worlds without Terragen:

- **A world editor panel** (Editor Utility Widget or a Slate editor module in this plugin) to edit what `tgw_to_ush.py` reads: sun, light and ambient colours, exposure and tone curve, atmosphere layers, cloud layer (seed, persistence, feature size, altitude, density), water level and waves, and the surface-layer tree (colours, altitude and slope limits, coverage, bump, noise).
- **Terrain** from a heightmap import, or sculpted with Unreal's Landscape tools and exported to the 513² height texture (the research on in-world Terragen terrain covers using Landscape).
- **Output** in the same form the pipeline already uses: a `worlds.json` entry plus a generated `Shaders/Worlds/<Name>.ush` and cloud map, then the `ue_setup.py` steps. Optionally a `.tgw`/`.ter` writer too, so a world can still be opened in Terragen to compare.
- **Live preview** while editing. Compiled worlds need a shader recompile per change (FXC takes about 30 s for a sky world), so the tool would want a "runtime world" variant: the same shader reading the `TGWorld` values from material parameters or a small data texture instead of constants. Slower per pixel, but instant to tweak; bake to a compiled world when done.

The pieces that make this practical are already in place: `tgw_to_ush.py` documents every field and its source in the DLL, `make_cloud_map.py` reproduces Terragen's generator exactly, and `UTerragenWorld` is the hand-off point to the actor.
