@echo off
rem Build Vectrex_EMU.exe (one file, no console window) from src\frontend with PyInstaller. Needs Python 3
rem with tkinter (python.org); builds the core first (build_core.bat, Visual C++) and installs
rem PyInstaller if they are missing.
setlocal
cd /d "%~dp0"
set PY=python
if exist "C:\Program Files\Python312\python.exe" set PY="C:\Program Files\Python312\python.exe"
if not exist core\vectrex_core.exe (call "%~dp0build_core.bat" || exit /b 1)
%PY% -c "import PyInstaller" 2>nul || (%PY% -m pip install --user pyinstaller || exit /b 1)
rem the core and the power sounds go inside the exe; the BIOS (system\bios.bin) and Mine Storm's overlay
rem (the overlays) sit next to it, as on Mac and Linux
rem The exe goes into the install folder build\Vectrex_EMU (with the BIOS and that overlay, copied in when missing:
rem your ROMs, overlays, SD cards and settings live there too)
set OUT=build\Vectrex_EMU
if not exist "%OUT%\system\bios.bin" (mkdir "%OUT%\system" 2>nul & copy /y system\bios.bin "%OUT%\system\" >nul)
if not exist "%OUT%\overlays" mkdir "%OUT%\overlays"
rem (the overlays that come with it: each copied in when it is missing)
for %%f in (overlays\*.art overlays\CREDITS.md overlays\RENAMED.txt) do if not exist "%OUT%\overlays\%%~nxf" copy /y "%%f" "%OUT%\overlays\" >nul
set D=%~dp0
rem (where it was built: the debug report)
%PY% -c "import os;os.makedirs('build',exist_ok=True);open('build/BUILD_INFO','w').write(('GitHub '+os.environ['GITHUB_SHA'][:7]) if os.environ.get('GITHUB_ACTIONS') else 'local build')" || exit /b 1
%PY% -m PyInstaller --noconfirm --onefile --windowed --name Vectrex_EMU --distpath "%OUT%" --workpath build\pyi --specpath build --paths src\frontend ^
  --add-binary "%D%core\vectrex_core.exe;core" ^
  --add-data "%D%sounds\on.wav;sounds" --add-data "%D%sounds\off.wav;sounds" --add-data "%D%icons\vectrex_emu.png;icons" --add-data "%D%VERSION;." --add-data "%D%build\BUILD_INFO;." ^
  --icon "%D%icons\vectrex_emu.ico" src\frontend\vectrex_emu.py || exit /b 1
echo built %OUT%\Vectrex_EMU.exe
