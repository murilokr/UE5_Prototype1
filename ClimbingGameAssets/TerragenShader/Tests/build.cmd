@echo off
rem Builds the CPU test renderer and checks that the shader compiles as HLSL (SM6) with DXC.
rem Output goes to ..\Build. Run from any directory.
setlocal
set HERE=%~dp0
set ROOT=%HERE%..
if not exist "%ROOT%\Build" mkdir "%ROOT%\Build"

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem C4305/C4244: HLSL float literals are doubles in C++; harmless here.
set CLFLAGS=/nologo /O2 /fp:precise /EHsc /std:c++17 /W3 /wd4305 /wd4244 /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\Shaders"
cl %CLFLAGS% "%HERE%tg_render.cpp" /Fo"%ROOT%\Build\\" /Fe"%ROOT%\Build\tg_render.exe" || exit /b 1
rem The sky reference world (Prototype2World) gets its own build; the world is a compile-time choice.
cl %CLFLAGS% /DTG_WORLD_FILE=\"Worlds/Prototype2World.ush\" /DTG_WORLD_FN=TGWorld_Prototype2World /DTG_WORLD_CAMERA=TGWorld_Prototype2World_CameraPos /DTG_WORLD_HEADING=TGWorld_Prototype2World_CameraHeading "%HERE%tg_render.cpp" /Fo"%ROOT%\Build\\" /Fe"%ROOT%\Build\tg_render_Prototype2World.exe" || exit /b 1

rem The project targets D3D12 SM6 (DXC) and D3D11 SM5 (FXC), so check both compilers.
where dxc >nul 2>nul || (echo dxc not on PATH, skipping the HLSL checks & exit /b 0)
rem Variants (RENDER_ATLAS SKY_WORLD LEGACY_MATERIAL): sky lookup; atlas with MountainTop (sky off); atlas with
rem Prototype2World (sky on); then the exact Custom-node calls of the first Unreal materials (LEGACY_MATERIAL=1)
rem and of the first split ones (LEGACY_MATERIAL=2: terrain pass, then sky pass, for both worlds), and the current
rem ones (LEGACY_MATERIAL=3, with the reflection-sky atlas).
for %%v in ("0 0 0" "1 0 0" "1 1 0" "0 0 1" "1 0 1" "1 0 2" "1 1 2" "2 0 2" "2 1 2" "1 0 3" "1 1 3" "2 0 3" "2 1 3") do for /f "tokens=1,2,3" %%a in (%%v) do (
	dxc -nologo -T ps_6_6 -E main -HV 2018 -DRENDER_ATLAS=%%a -DSKY_WORLD=%%b -DLEGACY_MATERIAL=%%c -I "%ROOT%\Shaders" -Fo "%ROOT%\Build\dxc_check_%%a%%b%%c.dxil" "%HERE%dxc_check.hlsl" || exit /b 1
	fxc /nologo /T ps_5_0 /E main /O1 /DRENDER_ATLAS=%%a /DSKY_WORLD=%%b /DLEGACY_MATERIAL=%%c /I "%ROOT%\Shaders" /Fo "%ROOT%\Build\fxc_check_%%a%%b%%c.dxbc" "%HERE%dxc_check.hlsl" >nul || exit /b 1
)
echo HLSL checks passed (DXC SM6.6, FXC SM5.0)
