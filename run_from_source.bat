@echo off
rem Run the emulator from the Python sources (Python 3 with tkinter), without a console window.
cd /d "%~dp0"
set PYW=pythonw
if exist "C:\Program Files\Python312\pythonw.exe" set PYW="C:\Program Files\Python312\pythonw.exe"
start "" %PYW% src\frontend\vectrex_emu.py %*
