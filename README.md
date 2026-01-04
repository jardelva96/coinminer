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
# Sintaxe: ./coinminer <comando> [opções]

# Executar o minerador
./build/coinminer run "hello" 4 2000000

# Benchmark (medir hashrate)
./build/coinminer bench 500000

# Ajuda
./build/coinminer help
```

### Parâmetros
- `data`: string base que será concatenada com o nonce.
- `dificuldade_hex`: número de zeros à esquerda (em hexadecimal) exigidos no hash (0-64).
- `max_tentativas`: limite de nonces testados antes de abortar (>= 1).
- `iteracoes`: quantidade de hashes para medir hashrate (benchmark), default 500000.

Se nenhum parâmetro for informado, o comando `./coinminer run` usa:
- `data` = `hello-from-coinminer`
- `dificuldade_hex` = `4`
- `max_tentativas` = `2000000`

## Exemplo de saída
```
Data: "hello"
Difficulty (hex zeros): 4
Max attempts: 2000000

FOUND!
Nonce: 34677
Hash: 0000a1c3...
Time: 0.527s | Hashrate: 65.82 H/s
```
