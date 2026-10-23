# Design — CTR Mobile

## Princípios

1. **Preservar a identidade do jogo:** cenas, menus e progressão continuam pertencendo à engine. A camada Android complementa o jogo onde o produto precisa de interface nativa ou controles móveis.
2. **Manter responsabilidades claras:** Android cuida de launcher, preferências, editor de controles e apresentação touch; C/C++ cuida das regras e estados do jogo.
3. **Alinhar visual e interação:** elementos touch desenhados e suas hitboxes usam a mesma geometria e mudam juntos.
4. **Tratar estados explicitamente:** loading, seleções, corrida, pausa, demonstração e cutscenes têm necessidades de entrada distintas.
5. **Validar na superfície real:** o overlay Canvas e a imagem SDL são superfícies diferentes; proporção, insets, gamepad e desempenho precisam de avaliação no aparelho quando afetados.

## Diretrizes de interface Android

- Respeitar insets, notch, área útil e dimensões do dispositivo; usar proporções e limites de toque apropriados.
- Liberar ponteiros e comandos mantidos em transições de tela ou troca de geração do snapshot.
- Preservar seleções e confirmações como ações separadas onde o fluxo existente prevê isso.
- Manter as chaves de preferência atuais para configurações existentes. Mudanças de formato exigem migração planejada.
- Preservar descrição de acessibilidade, foco de gamepad e estados visuais de controles ao extrair ou reutilizar componentes.
- Não replicar em Java a máquina de estados da engine para produzir uma aparência equivalente.

## Mapa das superfícies

| Superfície | Fonte principal | Referência detalhada |
|---|---|---|
| Launcher e importação | `CTRNativeLauncherActivity` | [Guia Android](../README_ANDROID.md) e [guia de alteração de UI](android-ui-change-guide.md) |
| UI touch durante o jogo | `CTRTouchOverlay` e snapshots nativos | [Arquitetura UI Android](android-ui-architecture.md) |
| HUD e controles editáveis | `CTRControlsActivity`, preferências e overlay | [Sobreposições e HUD](OVERLAYS.md) |
| Menus, seleções e progressão | Engine em C/C++ | [Mapa de fluxos](game-flow-map.md) |
| Tokens de configuração | Configuração Android e `ctrds.cfg` | [Recursos e configurações](../README_CTRDS.md) |

Este arquivo define princípios e fronteiras; não substitui os guias específicos nem fixa um sistema completo de cores ou tipografia. Confirme a implementação atual antes de ampliar as regras visuais.
