---
name: ctr-mobile-native
description: Use when changing CTR Mobile's native C/C++ engine, platform integration, JNI boundary, game states, rendering, or native input. Do not use for Android-only layout work with no native contract changes.
---

# CTR Mobile: engine nativa

Preserve as regras e os contratos do jogo, fazendo a alteração na menor camada nativa responsável.

1. Consulte `docs/ARCHITECTURE.md`, `docs/CHANGE_CONTRACT.md` e o guia específico do subsistema. Confirme no código o estado descrito em documentos antigos.
2. Localize chamadores e fronteira de plataforma antes de editar. `CTR_NATIVE` cobre o port; use `__ANDROID__` somente para comportamento Android.
3. Mantenha alinhados produtor de estado, snapshot, JNI, consumidor Android e entrada quando a mudança atravessar a fronteira. Não leia estruturas vivas da engine pela thread Android.
4. Preserve layouts de dados, macros de plataforma, memória e persistência do jogo. Evite alterações em `externals/` sem necessidade direta.
5. Valide com teste host específico e/ou build Android conforme a área. O CMake raiz não é um alvo desktop; mudanças visuais ou de input também precisam de evidência em aparelho para aceite de runtime.
6. Atualize arquitetura, fluxo ou contrato especializado quando um estado ou interface entre camadas mudar.

Fontes centrais: `docs/ARCHITECTURE.md`, `docs/CHANGE_CONTRACT.md`, `docs/MEMORY_MODEL.md`, `docs/DATA_SECTIONS.md` e `docs/game-flow-map.md`.
