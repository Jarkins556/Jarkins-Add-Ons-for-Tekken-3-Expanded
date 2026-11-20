@echo off
rem Jarkins for Tekken 3 Expanded - alpha 0.2.0
rem Lives inside the add-on: <Tekken 3 Expanded>\addons\jarkins\.
rem Drag the whole "jarkins" folder into Tekken 3 Expanded's own "addons"
rem folder, so it sits at addons\jarkins. This add-on builds on its own - TMST
rem is not needed, and if it is installed it is left alone. Then double-click
rem this file: it checks the place and opens the add-on's setup window
rem (setup\jarkins_install.py, the way Setup TMST.cmd opens TMST's), where
rem "Install & play" builds the game with the add-on in and then offers Play.
rem Two things of the Expanded's the window sets up, once each and only when
rem they are missing: the HUD hook in game.toml (the readouts and the plate
rem colours draw at its entry) and R1 as the sixth costume in the game's own
rem src\tekken3_native_moves.c. Both say so in the window. Delete addons\jarkins,
rem run Update & play again, and the game plays as it was.
setlocal EnableExtensions
cd /d "%~dp0"
title Jarkins for Tekken 3 Expanded - setup
set "HERE=%~dp0"
for %%i in ("%~dp0..\..") do set "ROOT=%%~fi"
set "PY=%ROOT%\.setup\venv\Scripts\python.exe"
set "PYW=%ROOT%\.setup\venv\Scripts\pythonw.exe"

echo.
echo   Jarkins for Tekken 3 Expanded - alpha 0.2.0
echo.

rem Dropped inside another add-on's folder instead of the addons folder?
if not "%HERE%"=="%HERE:\addons\tmst\=%" goto :inside_addon
if not "%HERE%"=="%HERE:\addons\TMST\=%" goto :inside_addon
goto :placed

:inside_addon
    echo   This copy is inside another add-on's folder - move it out.
    echo.
    echo   It belongs in Tekken 3 Expanded's own "addons" folder, as a folder
    echo   of its own, not inside TMST's or anyone else's:
    echo.
    echo       ^<Tekken 3 Expanded^>\addons\jarkins
    echo.
    pause
    exit /b 1

:placed
if not exist "%ROOT%\game.toml" (
    echo   This folder is not inside Tekken 3 Expanded yet.
    echo.
    echo   Drag the whole "jarkins" folder into Tekken 3 Expanded's own
    echo   "addons" folder, so it ends up at:
    echo.
    echo       ^<Tekken 3 Expanded^>\addons\jarkins
    echo.
    echo   Then run this file again. ^(Do not put it in the Expanded's root, in
    echo   mods\, or inside another add-on's folder. TMST is not needed here.^)
    echo.
    pause
    exit /b 1
)
if not exist "%ROOT%\addons\jarkins\addon.cmake" (
    echo   Hmm: "%ROOT%\addons\jarkins" is missing its addon.cmake.
    echo   Extract the whole download again, then drag the folder in again.
    echo.
    pause
    exit /b 1
)

rem The build replaces the game's .exe, and Windows will not let it while the
rem game is running: the link then fails with "Permission denied", which reads
rem like a broken add-on. Say it plainly instead.
tasklist /fi "imagename eq Tekken_3_Expanded.exe" 2>nul | find /i "Tekken_3_Expanded.exe" >nul
if not errorlevel 1 (
    echo   Tekken 3 Expanded is still running.
    echo.
    echo   Close the game first: building with the add-on replaces the game's
    echo   .exe, and Windows will not let it while the game holds the file.
    echo.
    pause
    exit /b 1
)

echo   In place at  "%ROOT%\addons\jarkins"
echo   ^(TMST is not needed; if it is there, it is left alone^)
echo.

rem The window needs the Expanded's own Python, which its setup makes.
if not exist "%PYW%" goto :no_setup

rem Tk, which the Expanded's setup asks Python for: without it the same setup
rem runs here, in this window, printing its progress instead of showing it.
"%PY%" -c "import tkinter" >nul 2>&1
if errorlevel 1 goto :in_window
start "" "%PYW%" "%~dp0setup\jarkins_install.py"
exit /b 0

:no_setup
echo   Tekken 3 Expanded has not been set up yet, so there is nothing to build
echo   the add-on into.
echo.
echo   Run  "%ROOT%\Setup Tekken 3.cmd"  first and let it finish - it downloads
echo   the tools, builds the game and starts it, and it builds this add-on in
echo   on the way. Close the game, then double-click this file again to see the
echo   add-on's own window ^(Install ^& play, then Play^).
echo.
pause
exit /b 1

:in_window
echo   No Tk in this Python: the same setup runs here, with its progress below.
echo   The Expanded is rebuilt with the add-on; its .setup\setup.log has more.
echo.
"%PY%" -u "%ROOT%\launcher\setup_backend.py" --update --text
if errorlevel 1 (
    echo.
    echo   The build stopped. "%ROOT%\.setup\setup.log" says why.
    echo.
    pause
    exit /b 1
)
echo.
echo   Built. Start the game, then tick what you want in the Mods page under
echo   Jarkins, and restart after toggling anything there.
echo.
pause
exit /b 0
