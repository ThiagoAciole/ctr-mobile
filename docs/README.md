# Índice da documentação

O app é Android. O Gradle em `android/` orquestra o app e a biblioteca nativa;
use o CMake raiz somente por esse fluxo.

## Começar

- [Build, instalação, importação e diagnóstico](../README_ANDROID.md)
- [Recursos e configurações do fork](../README_CTRDS.md)
- [Visão geral do projeto](../README.md)

## Documentos-base

- [PRD](PRD.md) — escopo, requisitos e critérios de aceite documentados.
- [Arquitetura](ARCHITECTURE.md) — camadas, fronteiras e fluxo de dados.
- [Design](DESIGN.md) — princípios e limites da interface Android.
- [Contrato de alterações](CHANGE_CONTRACT.md) — preservação, compatibilidade e evidência exigida.

## Arquitetura e manutenção

- [Arquitetura da interface Android](android-ui-architecture.md)
- [Guia para alterações de telas](android-ui-change-guide.md)
- [Mapa dos fluxos do jogo](game-flow-map.md)
- [Sobreposições e HUD](OVERLAYS.md)
- [Modelo de memória](MEMORY_MODEL.md)
- [Seções e dados nativos](DATA_SECTIONS.md)
- [Arte HD do CTR-DS](ctrds-hd-art.md)
- [Replays de diagnóstico](REPLAYS.md)

## Especificações e planos

Os arquivos abaixo registram propostas e decisões de implementação. Eles não
garantem que o produto atual ainda corresponda ao desenho: confira o código e a
interface antes de usá-los como especificação vigente.

- [Especificações](superpowers/specs/)
- [Planos](superpowers/plans/)

## Evidências e relatórios

- [Relatório visual e funcional consolidado](validation/REPORT.md) —
  observações dos vídeos, correções posteriores e limites de validação.

Para mudanças visuais, valide build e comportamento em aparelho/emulador como
checagens separadas; uma compilação bem-sucedida não prova a experiência visual.
