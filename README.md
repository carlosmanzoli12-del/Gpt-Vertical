# Vertical Canvas Lite — OBS 27.2.4

Plugin para Windows x64 com seleção de cenas existentes do OBS em dois modos:

- **Automático:** acompanha a cena ativa no OBS.
- **Manual:** permite selecionar uma cena existente.

O painel lista os itens/fontes da cena selecionada e acompanha mudanças da lista de cenas e trocas de cena pelo frontend API do OBS.

## Compatibilidade

- OBS Studio 27.2.4 (64 bits)
- Qt 5.15.2 (MSVC 2019 x64)
- Visual Studio 2022 / CMake

## Build e instalador

O workflow `.github/workflows/build.yml` compila as bibliotecas do OBS, compila a DLL e gera `dist/VerticalCanvasLite-Setup-x64.exe` com Inno Setup. Execute o workflow em GitHub Actions e baixe o artefato `VerticalCanvasLite-Windows-x64`.

Consulte `LEIA-ME-COMPILACAO.txt` antes de instalar.

## Limitação importante

Esta versão reutiliza as cenas e fontes originais do OBS. Alterações de visibilidade, ordem, remoção e transformação feitas no painel afetam a cena original. Ainda não há uma cópia vertical independente da composição horizontal.
