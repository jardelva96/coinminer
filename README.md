# coinminer (SHA-256 + PoW local)

Implementação simples de prova-de-trabalho em C usando SHA-256. O binário gera hashes de `data|nonce` até encontrar um valor com zeros iniciais em hexadecimal (dificuldade).

## Requisitos
- CMake 3.16+
- Ninja ou outro gerador suportado
- Compilador C11 (MSVC, clang ou gcc)

## Build
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> No Windows, execute os comandos acima no PowerShell dentro de um Developer Prompt do Visual Studio Build Tools.

## Uso
```bash
# Sintaxe: ./coinminer <data> <dificuldade_hex> <max_tentativas>
./build/coinminer "hello" 4 2000000
```

### Parâmetros
- `data`: string base que será concatenada com o nonce.
- `dificuldade_hex`: número de zeros à esquerda (em hexadecimal) exigidos no hash.
- `max_tentativas`: limite de nonces testados antes de abortar.

## Exemplo de saída
```
Data: "hello"
Difficulty (hex zeros): 4
Max attempts: 2000000

FOUND!
Nonce: 34677
Hash: 0000a1c3...
Time: 0.527s
```
