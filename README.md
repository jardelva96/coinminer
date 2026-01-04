# coinminer (SHA-256 + PoW local, Stratum, Solo RPC)

<<<<<<< HEAD
Implementação simples de prova-de-trabalho em C usando SHA-256. O binário gera hashes de `data|nonce` até encontrar um valor com zeros iniciais em hexadecimal (dificuldade).
=======
Implementacao em C de mineracao SHA-256 com modos local, Stratum (pool) e solo via RPC (node). A UI WPF integra controle de modo, carteira local e monitoramento.
>>>>>>> codex/create-readme-for-project-vn5azj

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

<<<<<<< HEAD
## Uso
```bash
# Sintaxe: ./coinminer <comando> [opções]

# Executar o minerador (opcional: --progress 50000)
./build/coinminer run "hello" 4 2000000 --progress 50000
=======
## Uso (CLI)
```bash
# Sintaxe: ./coinminer <comando> [opcoes]

# Executar o minerador (opcional: --progress 50000). Modo sempre infinito (pare com Ctrl+C).
./build/coinminer run "hello" 4 0 --progress 50000
>>>>>>> codex/create-readme-for-project-vn5azj

# Benchmark (medir hashrate)
./build/coinminer bench 500000 --progress 100000

<<<<<<< HEAD
# Versão
=======
# Carteira (criar/mostrar saldo)
./build/coinminer wallet --wallet wallet.dat

# Stratum (pool) com SHA-256 + submit de shares
./build/coinminer stratum pool.exemplo.com 3333 worker userpass --coin bitcoin

# Solo via RPC (node local)
./build/coinminer solo 127.0.0.1 8332 rpcuser rpcpass --coin bitcoin

# Versao
>>>>>>> codex/create-readme-for-project-vn5azj
./build/coinminer version

# Ajuda
./build/coinminer help
```

<<<<<<< HEAD
### Parâmetros
- `data`: string base que será concatenada com o nonce.
- `dificuldade_hex`: número de zeros à esquerda (em hexadecimal) exigidos no hash (0-64).
- `max_tentativas`: limite de nonces testados antes de abortar (>= 1).
- `iteracoes`: quantidade de hashes para medir hashrate (benchmark), default 500000.
- `--progress N`: exibe progresso a cada `N` tentativas (run) ou hashes (bench).

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

Benchmark concluido: 500000 hashes
Time: 0.418s | Hashrate: 1196080.37 H/s
```
=======
### Parametros
- `data`: string base que sera concatenada com o nonce.
- `dificuldade_hex`: numero de zeros a esquerda (em hexadecimal) exigidos no hash (0-64).
- `max_tentativas`: ignorado (modo infinito). Parar manualmente com Ctrl+C.
- `iteracoes`: quantidade de hashes para medir hashrate (benchmark), default 500000.
- `--progress N`: exibe progresso a cada `N` tentativas (run) ou hashes (bench).
- `--wallet caminho`: define o arquivo de carteira (default: `wallet.dat`).
- `--reset-wallet`: recria a carteira (novo endereco, saldo zerado).

## Carteira
- Arquivo simples (`wallet.dat` por padrao) contendo endereco, saldo e blocos minerados (modo local).
- No modo Stratum/solo, o pagamento depende do endereco configurado no pool/node.

## Stratum (pool)
- Comando `stratum <host> <port> <user> [password] [--retries N] [--delay SECS] [--coin NAME]`.
- Faz subscribe/authorize, processa notify/difficulty e tenta submeter shares.
- Usa target por difficulty do pool (mining.set_difficulty) quando disponivel.

## Solo (node RPC)
- Comando `solo <host> <port> <user> <password> [--coin NAME]`.
- Usa `getblocktemplate` e `submitblock` via RPC.
- Requer node local ativo (bitcoind/litecoind/dogecoind) com RPC habilitado.

## Interface (WPF)
- Projeto WPF em `ui/` com dashboard, modo Local/Stratum/Solo, carteira local e controle Start/Stop.
- Abra `ui/Coinminer.Ui.sln` no Visual Studio para rodar.
>>>>>>> codex/create-readme-for-project-vn5azj
