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
> Se você já gerou a pasta `build` com outro generator (ex.: Visual Studio), apague `build/` antes de trocar para Ninja ou use outro diretório (ex.: `build-ninja`). Caso contrário o CMake reclamará de generator diferente e pode misturar flags (ex.: `/O2` com `/RTC1`).
> Para continuar com o generator anterior, execute o mesmo `-G` que foi usado da primeira vez (ex.: `-G \"Visual Studio 17 2022\"`) ou remova `CMakeCache.txt` e a pasta `CMakeFiles` dentro de `build/`.

## Uso
```bash
# Sintaxe: ./coinminer <comando> [opções]

# Executar o minerador (opcional: --progress 50000). Modo sempre infinito (pare com Ctrl+C).
./build/coinminer run "hello" 4 0 --progress 50000

# Benchmark (medir hashrate)
./build/coinminer bench 500000 --progress 100000

# Carteira (criar/mostrar saldo)
./build/coinminer wallet --wallet wallet.dat

# Testar Stratum (subscribe/authorize + notify parsing)
./build/coinminer stratum pool.exemplo.com 3333 worker userpass --coin bitcoin

# Versão
./build/coinminer version

# Ajuda
./build/coinminer help
```

### Parâmetros
- `data`: string base que será concatenada com o nonce.
- `dificuldade_hex`: número de zeros à esquerda (em hexadecimal) exigidos no hash (0-64).
- `max_tentativas`: ignorado (modo infinito). Parar apenas manualmente com Ctrl+C; valores enormes são aceitos (tratados como infinito).
- `iteracoes`: quantidade de hashes para medir hashrate (benchmark), default 500000.
- `--progress N`: exibe progresso a cada `N` tentativas (run) ou hashes (bench).
- `--wallet caminho`: define o arquivo de carteira (default: `wallet.dat`).
- `--reset-wallet`: recria a carteira (novo endereço, saldo zerado).

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

Reward: 50 coins adicionados. Novo saldo:
Wallet address: e3f9... (exemplo)
Balance: 50 coins
Mined blocks: 1

Mineracao encerrada apos 2000000 tentativas.
Time: 1.234s | Hashrate medio: 1620000.00 H/s
Blocos encontrados nesta sessao: 1
Carteira apos a sessao:
Wallet address: e3f9... (exemplo)
Balance: 50 coins
Mined blocks: 1
```

## Carteira
- Arquivo simples (`wallet.dat` por padrão) contendo endereço, saldo e blocos minerados.
- O comando `wallet` cria o arquivo se não existir (ou recria com `--reset-wallet`) e exibe o saldo.
- O comando `run` carrega/cria a carteira antes de minerar e, ao encontrar um nonce válido, adiciona `50` coins ao saldo e incrementa o contador de blocos.
- Use `--wallet <caminho>` para manter carteiras separadas ou evitar sobrescrever a padrão.

## Stratum (progresso rumo a Bitcoin/pools)
- Comando `stratum <host> <port> <user> [password] [--retries N] [--delay SECS] [--coin NAME]`.
- Mantém conexão viva, envia subscribe/authorize, loga pings e estatísticas a cada 30s.
- Captura e conta mensagens `mining.notify`, armazenando o último payload e extraindo campos principais (`job_id`, `prevhash`, `coinb1/coinb2`, `merkle_branch`, `version`, `nbits`, `ntime`, `clean_jobs`).
- Reconexão com retentativas e atraso configuráveis via `--retries` e `--delay`.
