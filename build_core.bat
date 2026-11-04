@echo off
rem Build core\vectrex_core.exe from src\core with Microsoft Visual C++ (Visual Studio 2019 Build
rem Tools or newer: run from any prompt, it finds vcvars64.bat itself).
setlocal
cd /d "%~dp0"
rem (vswhere, Visual Studio's own locator, finds any edition and version with the C++ tools)
set VCVARS=
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat > "%TEMP%\vx_vcvars.txt"
if exist "%TEMP%\vx_vcvars.txt" set /p VCVARS=<"%TEMP%\vx_vcvars.txt"
if exist "%TEMP%\vx_vcvars.txt" del "%TEMP%\vx_vcvars.txt"
if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo Visual C++ not found: install Visual Studio Build Tools with C++ & exit /b 1)
echo using %VCVARS%
call "%VCVARS%" >nul || (echo Visual C++ could not be set up & exit /b 1)
if not exist core mkdir core
if not exist build mkdir build
rem (src\core\state: vtest.c, vecx.c, e6809.c and e8910.c compiled with all their data in sections of their own, so
rem  live.c can save and restore a running 6809 game whole: Display > Run-ahead. The Mac / Linux builds compile the
rem  files themselves, without run-ahead)
cl /nologo /O2 /GL /W0 /TC /DVX_STATES /Fobuild\ /Fe:core\vectrex_core.exe src\core\state\state_vtest.c src\core\state\state_vecx.c src\core\state\state_e6809.c src\core\state\state_e8910.c src\core\state\state_core.c src\core\cm33.c src\core\rp2350.c src\core\host.c src\core\arm11.c src\core\usbhost.c || exit /b 1
echo built core\vectrex_core.exe
