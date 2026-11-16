@echo off
rem The RetroArch core for Windows (x64) with Microsoft Visual C++ -> vectrex_emu_libretro.dll (Visual Studio 2019 Build
rem Tools or newer; it finds vcvars64.bat itself, as build_core.bat). No OpenMP: the release core needs nothing beyond
rem Windows itself (with /openmp it wanted VCOMP140.DLL, the Visual C++ runtime). Copyright (c) 2026 Hugo Bokhorst.
rem Part of Vectrex_EMU; see LICENSE.md.
setlocal
cd /d "%~dp0"
set VCVARS=
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat > "%TEMP%\vx_vcvars.txt"
if exist "%TEMP%\vx_vcvars.txt" set /p VCVARS=<"%TEMP%\vx_vcvars.txt"
if exist "%TEMP%\vx_vcvars.txt" del "%TEMP%\vx_vcvars.txt"
if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo Visual C++ not found: install Visual Studio Build Tools with C++ & exit /b 1)
call "%VCVARS%" >nul || (echo Visual C++ could not be set up & exit /b 1)
set C=..\src\core
if not exist obj_win mkdir obj_win
cl /nologo /O2 /W0 /TC /DVX_RETROARCH /I%C% /I. /Foobj_win\ /LD /Fe:vectrex_emu_libretro.dll libretro_vx.c %C%\state\state_vecx.c %C%\state\state_e6809.c %C%\state\state_e8910.c %C%\state\state_core.c %C%\cm33.c %C%\rp2350.c %C%\host.c %C%\arm11.c %C%\usbhost.c /link /DEF:exports.def || exit /b 1
del vectrex_emu_libretro.exp vectrex_emu_libretro.lib 2>nul
dir vectrex_emu_libretro.dll | find "vectrex"
