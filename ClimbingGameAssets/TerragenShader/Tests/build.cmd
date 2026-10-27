@echo off
rem Builds the CPU test renderer and checks that the shader compiles as HLSL (SM6) with DXC.
rem Output goes to ..\Build. Run from any directory.
setlocal
set HERE=%~dp0
set ROOT=%HERE%..
if not exist "%ROOT%\Build" mkdir "%ROOT%\Build"

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
rem No -latest: it can pick an install without C++ tools, which -requires then filters out entirely.
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem C4305/C4244: HLSL float literals are doubles in C++; harmless here.
cl /nologo /O2 /fp:precise /EHsc /std:c++17 /W3 /wd4305 /wd4244 /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\Shaders" "%HERE%tg_render.cpp" /Fo"%ROOT%\Build\\" /Fe"%ROOT%\Build\tg_render.exe" || exit /b 1

rem The project targets D3D12 SM6 (DXC) and D3D11 SM5 (FXC), so check both compilers.
where dxc >nul 2>nul || (echo dxc not on PATH, skipping the HLSL checks & exit /b 0)
for %%r in (0 1) do (
	dxc -nologo -T ps_6_6 -E main -HV 2018 -DRENDER_ATLAS=%%r -I "%ROOT%\Shaders" -Fo "%ROOT%\Build\dxc_check_%%r.dxil" "%HERE%dxc_check.hlsl" || exit /b 1
	fxc /nologo /T ps_5_0 /E main /O1 /DRENDER_ATLAS=%%r /I "%ROOT%\Shaders" /Fo "%ROOT%\Build\fxc_check_%%r.dxbc" "%HERE%dxc_check.hlsl" >nul || exit /b 1
)
echo HLSL checks passed (DXC SM6.6, FXC SM5.0)
