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
rem the core, the BIOS and the power sounds go inside the exe: Vectrex_EMU.exe is all you need
set D=%~dp0
%PY% -m PyInstaller --noconfirm --onefile --windowed --name Vectrex_EMU --distpath . --workpath build\pyi --specpath build --paths src\frontend ^
  --add-binary "%D%core\vectrex_core.exe;core" --add-data "%D%bios\bios.bin;bios" ^
  --add-data "%D%sounds\on.wav;sounds" --add-data "%D%sounds\off.wav;sounds" src\frontend\vectrex_emu.py || exit /b 1
echo built Vectrex_EMU.exe
