@echo off
rem Builds with MinGW-w64 g++ (MSYS2 ucrt64). No other dependencies.
rem   build.bat                     undercroft.exe and sim.exe
rem   build.bat out\a.exe out\b.exe  custom output names (lets parallel builds not collide)
setlocal
cd /d "%~dp0"
set GAME=%1
set SIM=%2
if "%GAME%"=="" set GAME=undercroft.exe
if "%SIM%"=="" set SIM=sim.exe
set LOGIC=src/content.cpp src/dungeon.cpp src/fov.cpp src/game.cpp
set VIEW=src/main.cpp src/app.cpp src/ui.cpp src/scene.cpp src/models.cpp src/textures.cpp src/render.cpp src/paint.cpp src/audio.cpp src/audio_sfx.cpp src/audio_music.cpp src/gl.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o %GAME% %VIEW% %LOGIC% -mwindows -static -lopengl32 -lgdi32 -lgdiplus -lwinmm || exit /b 1
g++ -std=c++20 -O2 -Wall -Wextra -o %SIM% src/sim.cpp %LOGIC% -static || exit /b 1
echo Built %GAME% and %SIM%
