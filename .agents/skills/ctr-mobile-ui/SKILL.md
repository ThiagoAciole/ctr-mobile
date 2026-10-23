---
name: ctr-mobile-ui
description: Use when changing CTR Mobile Android UI or game touch flows, including launcher/loading, settings dialogs, pause, character or track selection, race HUD, control editor, Android layout, touch targets, or navigation between these screens. Do not use for unrelated game logic or general Android questions.
---

# CTR Mobile UI e fluxos de toque

Faça o menor ajuste que conclua o pedido e preserve a navegação original do jogo.

## Workflow

1. Leia `docs/android-ui-architecture.md` e o trecho relevante de `docs/android-ui-change-guide.md`. Leia `docs/game-flow-map.md` se o destino ou origem da tela depender do modo de jogo.
2. Verifique `git status --short` e preserve trabalho já existente. Não reverta, formate em massa, faça stage ou commit sem pedido.
3. Confirme no código qual camada é dona da tela: launcher (`CTRNativeLauncherActivity`), host do jogo (`CTRNativeActivity`), overlay (`CTRTouchOverlay`), editor (`CTRControlsActivity`) ou UI nativa C.
4. Antes de alterar um controle de toque, encontre o desenho e a hitbox correspondentes. Mude ambos juntos e limpe ponteiros/inputs presos em transições.
5. Procure componentes reutilizáveis antes de duplicar. Extraia somente padrões com estrutura e contrato compartilhados; passe callbacks ao componente e mantenha persistência/navegação na tela proprietária.
6. Preserve contratos: seleção e confirmação podem ser ações separadas; telas de personagem e pista têm layouts/hitboxes próprios; configurações in-game permanecem no contexto do jogo; controles editáveis compartilham as chaves documentadas.
7. Use dimensões proporcionais, respeite insets/notch e mantenha botões fora do conteúdo selecionável. Use `__ANDROID__` para código nativo exclusivo do Android; `CTR_NATIVE` também abrange desktop.
8. Valide de acordo com a mudança: build Android para Java/recursos/JNI, testes host específicos quando aplicáveis e inspeção em dispositivo para aparência e interação. O CMake raiz rejeita builds desktop. Informe claramente qual nível de validação foi feito.
9. Atualize a documentação se um novo fluxo, estado, asset, componente compartilhado ou contrato persistente for introduzido.

## Locais de referência

- Arquitetura, estados, decisões e contratos: `docs/android-ui-architecture.md`.
- Regras comuns de alteração: `docs/CHANGE_CONTRACT.md`.
- Roteiro enxuto de edição: `docs/android-ui-change-guide.md`.
- Fluxos gerais do jogo: `docs/game-flow-map.md`.
- Configurações compartilhadas: `CTRDSSettings` e `CTRNativePrefs` conforme o recurso alterado.
- Linha cíclica compartilhada de configuração: `CTRSettingsOptionRow`; veja as regras de reutilização em `docs/android-ui-architecture.md`.

Não presuma que uma especificação em `docs/superpowers/specs/` representa o estado final: compare-a com a implementação atual antes de editar.
