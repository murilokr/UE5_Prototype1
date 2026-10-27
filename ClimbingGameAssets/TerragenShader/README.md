# Terragen 0.9 renderer as an HLSL shader

`Shaders/Terragen.ush` re-implements how Terragen 0.9.43 renders terrain, using the formulas and constants decompiled from `Terragen.dll`:
- the heightfield surface
- the surface-layer tree with its coverage and bump texture
- sun, ambient and soft shadows
- the exposure/tone curve

Terragen rasterises micro-polygons; the shader ray-marches the same surface per pixel instead. Both find the same visible point.

The same `.ush` also compiles as C++ (`Tests/hlsl_shim.h`), so `Tests/tg_render` can render it on the CPU and compare it with real Terragen output.

## Folder

| Path | What |
|---|---|
| `Shaders/Terragen.ush` | The renderer. Main entry points: `TG_RenderRay`, `TG_ShadeRay`, `TG_CameraRay`. |
| `Shaders/TerragenUE.ush` | Unreal glue: the skybox cross atlas (same layout as `Scripts/GenerateSkyboxTexture.py`), UE↔Terragen directions, sRGB decode. |
| `Shaders/Worlds/MountainTop.ush` | Generated from `DynamicPrototype2/WorldNewLocationAndAtmosphere.tgw`: sun, light, water, tone settings and the 5 surface layers. |
| `Textures/prototype2_v2_height.png` | Lossless 16-bit heightfield (`.r16` is the same data, raw, for the harness). |
| `Textures/noise128.png` | Terragen's value-noise lattice: `srand(1)`, 16384 × `rand()`. |
| `Tools/ter_convert.py` | `.ter` → height texture + `.json` metadata. |
| `Tools/make_noise_table.py` | Writes `noise128.png/.r16`. |
| `Tools/tgw_to_ush.py` | `.tgw` + terrain `.json` → `Shaders/Worlds/<Name>.ush`. |
| `Tools/unpack_cross.py` | Undoes `Scripts/GenerateSkyboxTexture.py`: splits a 2048² cross back into six faces. |
| `Tools/compare.py` | Side-by-side images and metrics against Terragen; `--calibrate` fits light scales. |
| `Tools/ue_setup.py` | Run inside the editor: creates the Unreal assets and test map, renders on the GPU and exports the atlas. |
| `Tools/ue_view_check.py` | Run inside the editor: six screenshots of the sky from the level viewport. |
| `Tests/build.cmd` | Builds `Build/tg_render.exe` with MSVC and checks the shaders compile with DXC (SM6.6) and FXC (SM5). |

The tools need only a stock Python 3, with no packages.

## Regenerating

```
cd ClimbingGameAssets/TerragenShader
python Tools/ter_convert.py ../DynamicPrototype2/prototype2_v2.ter -o Textures
python Tools/make_noise_table.py -o Textures
python Tools/tgw_to_ush.py ../DynamicPrototype2/WorldNewLocationAndAtmosphere.tgw Textures/prototype2_v2.json -o Shaders/Worlds/MountainTop.ush --name MountainTop
Tests\build.cmd
Build\tg_render.exe Textures/prototype2_v2_height.r16 Textures/noise128.r16 Out/frame0001
python Tools/unpack_cross.py ../Textures/MountainTop/Sky_MountainSide_Alt_0001.png Out/reference 0001
python Tools/compare.py Out/reference 0001 Out/frame0001
```

`tg_render` takes `--cam x y z` (Terragen units), `--heading`, `--size` and `--faces`. For MountainTop frame N, the camera is `164 223 (45.647 + (N-1) * 0.067)`. With `--atlas` it renders the whole 2048² cross through the same function the Unreal material uses, and checks that the sky lookup inverts the atlas exactly.

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
- `Output/*.bmp` holds a different scene (sky, clouds, reflective water). It is not the MountainTop reference.
- The light values taken straight from the `.tgw` are already optimal in display space: sun = `LIGC × STRE`, ambient = `AMBC × BRSC`, exposure = `EXPO`, then tone mode `EXME` with `GAMA`.

## What is exact and what is not

**Taken from the decompiled DLL:**
- triangle interpolation of the heightfield (the (1,0)–(0,1) diagonal), and normals from ±0.5-cell differences
- the soft-shadow march, with two rays aimed at the sun disc's lower and upper edges, followed by a smoothstep
- the light equation and the uniform cloud dimming
- the value noise and its exact lattice, fractal sums, gradient noise and bump levels
- layer coverage: the sigmoid, altitude and slope penalties, and the level of detail
- tone modes 0, 1 and 2 (`RGBm` and the buffer pass), with Terragen's 16-bit and 8-bit quantisation in the harness
- the camera: focal length = zoom × width / 2, heading from +y towards +x

**Approximations:**
- The jitter that offsets coverage and bump samples is a PCG hash, not Terragen's table.
- The bump amplitude reads a per-point map that wasn't identified. `BumpTerrainValue = 0.25` stands in for it.
- The footprint used for detail is distance × pixel angle / Detail.
- In multi-directional background light, the away/sun-side colours are guessed (`SDSC`). MountainTop uses single colour, so it isn't affected.

**Not implemented yet:**
- the atmosphere (haze, blue and red layers). It's zero in MountainTop, so it doesn't show there.
- the sky dome, clouds and sun disc. The sky is off in MountainTop.
- water reflections and waves. MountainTop's water is opaque black.

**Known difference:** at higher frames, Terragen's distant outlines sit 1–3 px higher. The cause isn't identified; Terragen's sub-cell geometry is a candidate.

## In Unreal

### What is in the project

| Where | What |
|---|---|
| `Source/TerragenShaders/` | Module loaded at `PostConfigInit` that maps `/TerragenShaders` to this folder's `Shaders`, so materials can include the `.ush` files. Listed in `Prototype1.uproject` and both `*.Target.cs`. |
| `Source/Prototype1/.../Skybox/TerragenSkybox.h/.cpp` | `ATerragenSkybox`: renders the atlas and shows it on a sky sphere around the camera. |
| `/Game/MyStuff/Skybox/Terragen/` | `M_TerragenRender` (Custom node calling `TG_RenderAtlasTexel`), `M_TerragenSkyDisplay` (looks the view direction up in the atlas), `RT_TerragenSky` (2048², RGBA8 linear), `Textures/T_TerragenHeight_MountainTop` and `T_TerragenNoise` (G16, no mips, never streamed), and the `TerragenSkyTest` map. |

### Using it

Place an **ATerragenSkybox** where frame 1 of the old skybox was taken. That's the same point as `Base Altitude` in `BP_DynamicHeightSkybox`, because the actor's location is the Unreal point for `TerragenStart` (164, 223, 45.647). From there, the Terragen camera moves with the player in X, Y and Z, at true scale: one Terragen unit is 1500 cm. The terrain is 513 × 513 points, a 7.7 km square; beyond its edges there is no terrain.

| Setting | Default | Meaning |
|---|---|---|
| `FollowHorizontal` | on | Off matches the old skyboxes, which only changed with altitude. |
| `KeepAboveTerrain`, `TerrainClearance` | on, 150 cm | Where your level and the Terragen terrain don't line up, the player can be below the Terragen ground. This lifts the Terragen camera to the clearance above the ground (or water) there, instead of rendering the inside of the terrain. |
| `HeadingAtPlusX` | 18 | Which Terragen heading Unreal +X looks at. 18 puts the old Front face along +X. |
| `StartDistance` | 0 | Leaves out terrain nearer than this many Terragen units, so real level geometry can stand in for it. |
| `RedrawDistance` | 100 cm | Camera movement before a face is redrawn; the old frames were 1 m apart. |
| `FacesPerFrame` | 1 | Faces redrawn per frame. The very first draw always does all six. |
| `SkyRadius` | 10 km | Radius of the sky sphere. Level geometry beyond it is hidden. |

In the editor, the **Redraw From Actor Location** button on the actor previews the sky.

### Recreating the assets

Run `Tools/ue_setup.py` inside the editor. Python only needs enabling for that run; the project itself doesn't need the plugin:

```
UnrealEditor.exe Prototype1.uproject -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
    -ExecutePythonScript="<project>/ClimbingGameAssets/TerragenShader/Tools/ue_setup.py" -RenderOffscreen -unattended
    -ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorLoadingSavingSettings]:RestoreOpenAssetTabsOnRestart=NeverRestore
```

The last option stops the editor reopening last session's asset tabs. With those tabs open, an offscreen editor crashed on exit in the Skeletal Mesh editor; it didn't affect the assets.

### Verified on this machine

| Check | Result |
|---|---|
| GPU atlas vs the CPU harness, frames 1 and 400 | Silhouette overlap 1.000; colour within 1.1 levels (float rounding in the bump and speckle) |
| GPU atlas vs Terragen | Overlap 0.996 at frame 1 and 0.986 at frame 400, the same as the CPU harness |
| Level-viewport screenshots vs Terragen (`ue_view_check.py`) | Overlap 0.94–0.99 on the five faces with terrain; correct orientation, no mirroring |
| Following the player: 300 m along Unreal +X, and 1 km below the start (lifted above the ground) | Unreal→Terragen mapping exact; GPU vs CPU overlap 1.000 on every face, colour within 2.3 levels |
| Builds | Development and DebugGame editor targets. The game target too, with one caveat below |
| Cost of a full six-face redraw of the 2048² atlas, RTX 4060 Ti | 30 ms on an idle GPU (about 5 ms per face); 59 ms while a game was also using the GPU |

### Things to know

- **Post-processing.** The sky material decodes the atlas as sRGB, so its emissive equals what the old sRGB skybox textures gave. UE's tonemapper, bloom and temporal AA still run on top, as they did for the old textures, so the screen isn't byte-identical to Terragen. Two optional tweaks:
  - Nearest filtering on `RT_TerragenSky` keeps the texels crisp.
  - A post-process material in front of the tonemapper would be needed to show Terragen's exact bytes.
- **Async builds in the editor.** Right after loading, the height texture is still building. The actor waits for its textures and shaders before drawing, because the placeholder texture would render the whole sky as snow.
- **Game target.** `Prototype1.Build.cs` depends on `UnrealEd` for every target, which UBT refuses for non-editor builds. That predates this work and is the TODO in that file. The game target built cleanly once `UnrealEd` was made editor-only (`if (Target.bBuildEditor)`), but `Build.cs` was left unchanged.
