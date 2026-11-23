@echo off
rem The PC renderer's line build (vxbuild.c), Smooth motion's work (vxsmooth.c) and the library's generated pictures
rem (libretro\genart.c: genart.py) for Windows (x64) with Microsoft Visual C++ -> vxbuild.dll, next to this
rem file: vecrender.py loads it from here (and from inside the exe: build_exe.bat puts it in). Visual Studio 2019 Build
rem Tools or newer; it finds vcvars64.bat itself, as libretro\build_libretro_win.bat. /MT: it needs nothing beyond
rem Windows itself. /fp:precise and no /arch: plain SSE2 doubles, no fused multiply-add - the results are numpy's.
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
if not exist obj_win mkdir obj_win
cl /nologo /O2 /W3 /TC /MT /fp:precise /I..\..\..\libretro "/DVX_GENART_API=__declspec(dllexport)" /Foobj_win\ /LD /Fe:vxbuild.dll vxbuild.c vxsmooth.c ..\..\..\libretro\genart.c || exit /b 1
del vxbuild.exp vxbuild.lib 2>nul
rmdir /s /q obj_win 2>nul
dir vxbuild.dll | find "vxbuild"
