@echo off
setlocal enabledelayedexpansion

if not defined VSCMD_ARG_TGT_ARCH (
  set "VS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
  if not exist "!VS!" for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do set "VS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  if not exist "!VS!" echo [!] vcvars64.bat not found & exit /b 1
  call "!VS!" >nul
)

cd /d "%~dp0"

if not exist third_party\lzma\LzmaDec.c (
  echo [!] third_party\lzma\LzmaDec.c missing - see third_party\lzma\VENDOR.md
  exit /b 1
)

fxc /nologo /T vs_5_0 /E vs_main /O3 /Fh src\gfx\shader_vs.h /Vn g_vs_main src\gfx\shader.hlsl
if errorlevel 1 (echo [!] fxc vs_main failed & exit /b 1)
fxc /nologo /T ps_5_0 /E ps_main /O3 /Fh src\gfx\shader_ps.h /Vn g_ps_main src\gfx\shader.hlsl
if errorlevel 1 (echo [!] fxc ps_main failed & exit /b 1)

if not exist bin mkdir bin
if not exist bin\obj mkdir bin\obj

rc /nologo /fo bin\obj\assets.res assets.rc
if errorlevel 1 (echo [!] asset resource build failed & exit /b 1)

set "CXXFLAGS=/nologo /std:c++latest /EHsc /W4 /sdl /permissive- /Zc:preprocessor /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_UNICODE /DUNICODE /Isrc /Ithird_party"
set "CFLAGS=/nologo /W3 /sdl /utf-8 /D_7ZIP_ST"
set "LDFLAGS=winhttp.lib shlwapi.lib shell32.lib advapi32.lib user32.lib gdi32.lib ole32.lib d3d11.lib dxgi.lib dcomp.lib dxguid.lib windowscodecs.lib /SUBSYSTEM:WINDOWS"

if /i "%~1"=="debug" (
  set "CXXFLAGS=!CXXFLAGS! /Od /Zi /MDd /RTC1 /D_DEBUG"
  set "CFLAGS=!CFLAGS! /Od /Zi /MDd"
  set "LDFLAGS=!LDFLAGS! /DEBUG"
  echo [i] debug build
) else (
  set "CXXFLAGS=!CXXFLAGS! /O2 /GL /MD /DNDEBUG"
  set "CFLAGS=!CFLAGS! /O2 /GL /MD"
  set "LDFLAGS=!LDFLAGS! /LTCG"
)

set SRC=
for %%f in (src\*.cpp src\core\*.cpp src\gfx\*.cpp src\ui\*.cpp src\app\*.cpp src\update\*.cpp) do set "SRC=!SRC! %%f"

cl !CFLAGS! /c /Fobin\obj\ third_party\lzma\LzmaDec.c || exit /b 1
cl !CXXFLAGS! /Fobin\obj\ /Fdbin\obj\ !SRC! bin\obj\LzmaDec.obj bin\obj\assets.res /Febin\Launcher.exe /link !LDFLAGS! || exit /b 1

echo [+] bin\Launcher.exe
