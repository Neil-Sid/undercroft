# Builds web/undercroft.js + web/undercroft.wasm with the MSYS2 Emscripten package
# (pacman -S mingw-w64-ucrt-x86_64-emscripten). Serve the web folder over http to play.
$ErrorActionPreference = 'Continue'  # compiler warnings arrive on stderr; only the exit code matters
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$env:BINARYEN_CORES = '1'  # wasm-opt's thread pool deadlocks here when it may use every core
Set-Location $PSScriptRoot

# Desktop-only files: the Win32 main loop, the GL loader, GDI/GDI+ and the native tools.
$skip = 'main.cpp', 'gl.cpp', 'paint.cpp', 'sim.cpp', 'audio_test.cpp'
$sources = Get-ChildItem src\*.cpp | Where-Object { $skip -notcontains $_.Name } | ForEach-Object { $_.FullName }
& C:\msys64\ucrt64\bin\python.exe -E tools\emxx.py -std=c++20 -O3 -Wall -Wextra @sources -o web\undercroft.js `
    -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=2MB -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 `
    '-sEXPORTED_FUNCTIONS=_main,_audio_pull,_malloc' --preload-file sounds@/sounds
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-Item web\undercroft.js, web\undercroft.wasm, web\undercroft.data | Format-Table Name, Length
