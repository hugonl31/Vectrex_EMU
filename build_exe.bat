@echo off
rem Build Vectrex_EMU.exe (one file, no console window) from src\frontend with PyInstaller
rem (python -m pip install pyinstaller). Build the core first (build_core.bat).
setlocal
cd /d "%~dp0"
set PY=python
if exist "C:\Program Files\Python312\python.exe" set PY="C:\Program Files\Python312\python.exe"
rem the core, the BIOS and the power sounds go inside the exe: Vectrex_EMU.exe is all you need
set D=%~dp0
%PY% -m PyInstaller --noconfirm --onefile --windowed --name Vectrex_EMU --distpath . --workpath build\pyi --specpath build --paths src\frontend ^
  --add-binary "%D%core\vectrex_core.exe;core" --add-data "%D%bios\bios.bin;bios" ^
  --add-data "%D%sounds\on.wav;sounds" --add-data "%D%sounds\off.wav;sounds" src\frontend\vectrex_emu.py || exit /b 1
echo built Vectrex_EMU.exe
