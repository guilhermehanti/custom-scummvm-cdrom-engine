# Ferramenta de Geracao Standalone — ScummVM-BR

Esta ferramenta automatiza a compilacao de executaveis standalone independentes
para Windows 10/11 x64 a partir do codigo-fonte do ScummVM-BR, incorporando
somente o motor necessario para cada jogo, com leitura direta de ISO 9660/Joliet,
linkagem estatica completa (zero DLLs externas do MSYS2/MinGW) e menu completo
de configuracoes do ScummVM.

## Uso

Compilar todos os jogos cadastrados em `tools/standalone/games/`:

```powershell
python tools\standalone\build_standalone.py --all
```

Compilar apenas um jogo especifico:

```powershell
python tools\standalone\build_standalone.py tools\standalone\games\franklin.json
```

Recompilacao incremental rapida (sem rodar `./configure` novamente):

```powershell
python tools\standalone\build_standalone.py --all --skip-configure
```

Compilacao limpa:

```powershell
python tools\standalone\build_standalone.py --all --clean
```

## Adicionando novos jogos no futuro

1. Crie um arquivo JSON em `tools/standalone/games/<meu_jogo>.json` seguindo o modelo:

```json
{
  "app_id": "meujogo",
  "executable_name": "Meu Jogo.exe",
  "output_folder": "Meu Jogo",
  "display_name": "Meu Jogo (Edicao Brasileira)",
  "engine": "mediastation",
  "game_id": "meujogo_id",
  "target_name": "meujogo-win-br",
  "expected_language": "br",
  "expected_platform": "windows",
  "expected_extra": "",
  "runtime_icon_file": "",
  "icon": "",
  "extra_configure_flags": []
}
```

- `icon`: caminho opcional para um arquivo `.ico` proprio a ser incorporado no executavel. Se vazio, usa `icons/scummvm.ico`.
- `runtime_icon_file`: caminho relativo opcional dentro da ISO original do usuario (`.ico` ou `.exe`) para aplicar dinamicamente o icone da janela em tempo de execucao sem distribuir nenhum arquivo do jogo.
- `extra_configure_flags`: flags adicionais passadas ao `./configure` do ScummVM (por exemplo, desabilitar codecs nao usados pelo motor ou habilitar recursos extras como `--enable-fluidsynth`).

2. Execute:

```powershell
python tools\standalone\build_standalone.py tools\standalone\games\<meu_jogo>.json
```

Os pacotes finais prontos para distribuicao (sem nenhum arquivo dos jogos e com
todas as licencas GPLv3+ exigidas) sao gerados em `standalone-dist/`.
