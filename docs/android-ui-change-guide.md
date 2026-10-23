# Guia rápido para novos ajustes de telas

Use este roteiro para corrigir ou ampliar telas do CTR Mobile sem quebrar navegação, áreas de toque ou o fluxo original do jogo.

## Antes de editar

1. Consulte [android-ui-architecture.md](android-ui-architecture.md) e [game-flow-map.md](game-flow-map.md).
2. Verifique o estado atual do checkout: `git status --short`. Preserve alterações pré-existentes e não assuma que especificações antigas já foram implementadas.
3. Localize o dono da interface com `rg`: `CTRNativeLauncherActivity`, `CTRNativeActivity`, `CTRTouchOverlay`, `CTRControlsActivity` ou o arquivo C da tela.
4. Leia o ramo de desenho e o ramo de toque lado a lado. Uma tela visualmente ajustada com hitbox antiga continuará difícil de usar.

## Escolha o fluxo certo

| Se o pedido mexe em… | Comece por… | Cuide também de… |
|---|---|---|
| Importar/iniciar jogo ou configurações antes da corrida | `CTRNativeLauncherActivity.java` | estados de arquivo, seletor, validação e retorno ao mesmo estado. |
| Loading, ícone persistente, diálogo dentro do jogo | `CTRNativeActivity.java` | callback nativo, contexto da corrida e estado depois de fechar a tela. |
| Botões que aparecem sobre menus/corrida/pausa | `CTRTouchOverlay.java` | estado JNI, desenho, hitboxes e liberação de toques. |
| Reposicionar ou redimensionar HUDs | `CTRControlsActivity.java` | preferências compartilhadas, preview, limites e restaurar. |
| Texto, seleção, transição ou navegação original do jogo | `game/230/`, `game/RECTMENU.c`, `game/SubmitName.c` | caminho de confirmação/retorno e macros de plataforma. |

## Padrão de implementação

- Prefira uma alteração localizada no proprietário atual da tela.
- Para telas diferentes, defina layouts e regiões de toque diferentes; não compartilhe coordenadas apenas porque os botões têm o mesmo rótulo.
- Derive tamanhos da largura/altura útil, com limites mínimos para toque e margens que evitem o notch.
- Em qualquer evento que troca o estado visual, descarte ponteiros e botões pressionados antigos antes de aplicar o novo estado.
- Mantenha texto e ações como elementos separados do fundo; isso permite escalar, localizar e mudar estado sem trocar a arte.
- Para uma ação nova, conecte o botão a uma função/callback nativo existente sempre que possível. Não replique a máquina de estados do jogo em Java.
- Se uma Activity secundária for aberta do jogo, confirme o registro no manifest e o caminho de retorno. O diálogo de configurações do jogo deve continuar no contexto de `CTRNativeActivity`.

## Quando extrair um componente

- Antes de criar uma classe, procure uma implementação equivalente com `rg` e compare estrutura, dimensões, estados e ações.
- Extraia quando duas telas montarem o mesmo elemento com o mesmo contrato. Mantenha diferenças pequenas em modos nomeados; evite coordenadas genéricas e parâmetros booleanos sem significado claro.
- Passe valores e callbacks para o componente. A Activity que abre a tela deve continuar dona de estado persistente, navegação e chamadas JNI.
- Se apenas a aparência se repete, compartilhe uma primitiva visual. Não compartilhe a geometria/hitbox quando os controles servem a estados diferentes.
- Depois de extrair, confirme que cada chamador manteve seus tamanhos, descrições de acessibilidade, toque curto/longo e forma de salvar.

Exemplo atual: `CTRSettingsOptionRow` compõe as linhas de opções cíclicas do launcher e das configurações in-game. O valor é fornecido pelo chamador, e `CycleListener` devolve `-1`/`1`; assim cada tela mantém seu próprio salvamento e formato do texto. A pausa com sliders não usa essa linha.

## Lista curta antes de concluir

- [ ] A tela abre a partir do estado esperado e retorna ao mesmo contexto.
- [ ] Botões visíveis e áreas de toque coincidem em tamanhos/orientações relevantes.
- [ ] Seleção e confirmação continuam sendo ações distintas quando previsto.
- [ ] A UI não encobre conteúdo selecionável nem fica sob notch/barras de sistema.
- [ ] Valores persistidos continuam usando as chaves e limites já adotados.
- [ ] A validação relata build e teste visual separadamente.

Para alterações puramente documentais, confira links relativos e diff. Para mudanças de código, execute a validação mais específica disponível e registre o que ainda exige teste em dispositivo.
