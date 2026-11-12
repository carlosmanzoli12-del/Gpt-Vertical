# Vertical Canvas Lite (OBS 27.2.4)

Plugin novo, escrito do zero para o **OBS 27.2.4** (Windows 64 bits, Qt 5.15).
Não é uma cópia do Vertical Canvas da Aitum e não tem todos os recursos dele,
mas faz o essencial: um canvas vertical com **cenas e fontes próprias**,
adicionadas como no OBS.

> **Importante:** este código foi escrito sem poder compilar nem testar.
> É normal aparecerem alguns erros na primeira compilação. Cole a saída
> completa do erro no chat que eu corrijo.

## O que ele faz

- Painel "Vertical Canvas Lite" (aparece à direita; também em Ferramentas).
- Preview vertical (1080x1920 por padrão; muda em Configurações).
- **Cenas**: adicionar, remover e renomear. As cenas ficam salvas na coleção
  de cenas do OBS (aparecem na lista normal do OBS com o prefixo `[V] `).
- **Fontes**: adicionar (qualquer tipo de fonte do OBS, ou uma fonte
  existente), remover, subir/descer, mostrar/ocultar (checkbox),
  propriedades (⚙ ou duplo clique) e "Ajustar" à tela (conter, cobrir,
  esticar, resetar).
- No preview: clique seleciona o item, arrastar move, roda do mouse muda o
  tamanho.
- **Gravar** (arquivo .mkv) e **Iniciar live** (RTMP: servidor + chave),
  usando o canvas vertical, com x264 + AAC.

## Limitações (comparado ao da Aitum)

- Sem alças de redimensionar/rotacionar no preview (use roda do mouse e Ajustar).
- A taxa de quadros é a mesma do OBS principal.
- O áudio das fontes vai para a mesma mixagem do OBS (não há áudio separado
  para o canvas vertical).
- Só RTMP personalizado (sem login em plataformas) e só encoder x264.
- Sem multi-live, sem hotkeys próprias, sem tradução.

## Pré-requisitos

1. **Git**
2. **Visual Studio 2022** (carga "Desenvolvimento para desktop com C++")
3. **CMake 3.20+**
4. **Qt 5.15.2**, versão `msvc2019_64` (o OBS 27 usa Qt 5)

## Passo 1: compilar o OBS 27.2.4 (só para gerar as bibliotecas)

```
git clone --recursive https://github.com/obsproject/obs-studio.git
cd obs-studio
git checkout 27.2.4
git submodule update --init --recursive
```

Veja na pasta `CI/` do repositório (scripts do Windows) quais pacotes de
dependências (obs-deps) e qual Qt são usados nessa versão e baixe exatamente
esses. Depois:

```
cmake -S . -B build64 -G "Visual Studio 17 2022" -A x64 ^
  -DDepsPath="C:\caminho\das\deps\win64" ^
  -DQTDIR="C:\Qt\5.15.2\msvc2019_64" ^
  -DENABLE_UI=ON -DBUILD_BROWSER=OFF
cmake --build build64 --config Release
```

Você só precisa que estes arquivos existam depois:

- `build64\libobs\Release\obs.lib`
- `build64\UI\obs-frontend-api\Release\obs-frontend-api.lib`

## Passo 2: compilar este plugin

Dentro da pasta `vertical-canvas-lite`:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" ^
  -DOBS_SOURCE_DIR="C:\caminho\obs-studio" ^
  -DOBS_BUILD_DIR="C:\caminho\obs-studio\build64" ^
  -DOBS_CONFIG=Release
cmake --build build --config Release
```

O arquivo gerado é `build\Release\vertical-canvas-lite.dll`.

## Passo 3: instalar

1. Feche o OBS.
2. Copie `vertical-canvas-lite.dll` para
   `C:\Program Files\obs-studio\obs-plugins\64bit\`
3. **Remova o Vertical Canvas da Aitum** da instalação se ele estiver lá
   (os dois não devem rodar juntos).
4. Abra o OBS. O painel aparece à direita. Se não aparecer, use
   Ferramentas > Vertical Canvas Lite.

## Como usar

1. Clique em **+** em *Cenas* e dê um nome.
2. Clique em **+** em *Fontes* > *Nova fonte* (ou *Fonte existente*).
3. Use **Ajustar** para encaixar uma fonte na tela vertical.
4. Em **Configurações**, defina a resolução, os bitrates, a pasta de
   gravação e o servidor/chave da live.
5. **Gravar** ou **Iniciar live**.

## Pontos onde o compilador pode reclamar

Estes são os trechos que dependem de detalhes exatos da API do OBS 27.2.4,
que eu não consegui conferir:

- `obs_display_create(&info, 0xFF1E1E1E)` em `preview-widget.cpp`: se der
  erro de número de argumentos, remova o segundo argumento.
- `obs_frontend_open_source_properties(...)` em `vertical-dock.cpp`: se der
  erro de símbolo não encontrado, avise (dá para trocar por outra abordagem).
- Nomes das variáveis `.lib`: confira se os caminhos do `CMakeLists.txt`
  batem com o que o seu build do OBS gerou.
- `obs_source_inc_active` / `obs_source_inc_showing`: se faltar, me avise.
