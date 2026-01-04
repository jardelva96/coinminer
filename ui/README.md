# Interface (C# / WPF)

Este diretório contém um esqueleto de interface em C# (WPF) para futura integração com o minerador.

## Projetos
- `Coinminer.Ui.csproj` (WPF, .NET 6, Windows): protótipo de dashboard estático para visualização.
- `Coinminer.Ui.sln`: solução do Visual Studio apontando para o projeto acima.

## Como abrir
1. Abra `Coinminer.Ui.sln` no Visual Studio 2022 (ou mais recente) com suporte a .NET 6 e WPF.
2. Execute em Debug/Release. O layout é apenas um mock; não há integração com o binário C.

## Observação
- O layout atual replica um dashboard (saldo, blocos, hashrate, pools Stratum) apenas para referência visual. Ainda não há eventos ou dados reais.
