# coinminer (SHA-256 + PoW local)

Projeto em C para Windows, build com MSVC (Visual Studio Build Tools) + CMake + Ninja.

## Build (PowerShell)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

## Run
.\build\coinminer.exe "hello" 4 2000000