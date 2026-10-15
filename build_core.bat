@echo off
rem Build core\vectrex_core.exe from src\core with Microsoft Visual C++ (Visual Studio 2019 Build
rem Tools or newer: run from any prompt, it finds vcvars64.bat itself).
setlocal
cd /d "%~dp0"
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" for /f "delims=" %%i in ('dir /b /s "C:\Program Files*\Microsoft Visual Studio\*vcvars64.bat" 2^>nul') do set VCVARS=%%i
call "%VCVARS%" >nul || (echo Visual C++ not found & exit /b 1)
if not exist core mkdir core
if not exist build mkdir build
cl /nologo /O2 /W0 /TC /Fobuild\ /Fe:core\vectrex_core.exe src\core\vtest.c src\core\vecx.c src\core\e6809.c src\core\e8910.c || exit /b 1
echo built core\vectrex_core.exe
