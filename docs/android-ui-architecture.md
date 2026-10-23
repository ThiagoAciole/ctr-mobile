# Interface Android e fluxos de toque

Este documento complementa o [mapa dos fluxos do jogo](game-flow-map.md). Ele descreve como as telas Android criadas nesta conversa se conectam ao jogo nativo, onde ajustar cada parte e quais decisões visuais e de navegação devem ser preservadas.

## Limites entre as interfaces

| Área | Implementação | Responsabilidade |
|---|---|---|
| Launcher | `android/app/src/main/java/com/ctrnative/CTRNativeLauncherActivity.java` | Importar BIN/CHD, mostrar estados sem/com jogo carregado, iniciar jogo e abrir configurações do launcher. |
| Host Android do jogo | `android/app/src/main/java/com/ctrnative/CTRNativeActivity.java` | Mostrar a tela de loading sobre o jogo, abrir configurações durante a partida, manter o atalho de configurações e acompanhar o estado nativo. |
| Controles de toque por tela | `android/app/src/main/java/com/ctrnative/CTRTouchOverlay.java` | Desenhar ações sobre o jogo, determinar áreas de toque e encaminhar controles para a entrada nativa. |
| Editor de controles | `android/app/src/main/java/com/ctrnative/CTRControlsActivity.java` | Abrir a tela imersiva de controles, mostrar o preview da corrida, permitir arrastar os HUDs e mudar seu tamanho. |
| Estado e eventos de toque | `game/ctrds/ctrds.c`, `include/ctrds.h`, `platform/native_input.c` | Resolver o estado atual do jogo, receber eventos de menu e expor a ponte JNI para Android. |
| Menus e seleções desenhados pelo jogo | `game/230/`, `game/RECTMENU.c`, `game/SubmitName.c` | Renderizar os menus originais, transições e seleções que continuam sob controle do jogo. |

O Android desenha controles e ações auxiliares sobre a imagem do jogo. A navegação original ainda pertence ao jogo: prefira ligar um botão ao caminho de entrada já existente em vez de recriar a regra de progresso em Java.

## Estado comunicado ao overlay

`CTRTouchOverlay` usa estes valores inteiros, definidos também em `Ctrds_TouchScreenState()`:

| Valor | Constante Java | Quando usar |
|---:|---|---|
| 0 | `SCREEN_MENU` | Menus sem tratamento de toque especializado. |
| 1 | `SCREEN_RACE` | Corrida ativa. |
| 2 | `SCREEN_PAUSE_RESULTS` | Resultado de corrida. |
| 3 | `SCREEN_WAITING` | Inicialização, carregamento ou estado sem controles de menu. |
| 4 | `SCREEN_DEMO` | Demonstração/attract mode; toque pode pular. |
| 5 | `SCREEN_ROOT_MENU` | Menu principal. |
| 6 | `SCREEN_GAME_MENU` | Menu textual identificado pelo snapshot nativo. |
| 7 | `SCREEN_CHARACTER_SELECT` | Seleção de personagem. |
| 8 | `SCREEN_NAME_ENTRY` | Entrada de nome; o Android pode abrir o diálogo de texto. |
| 9 | `SCREEN_TRACK_SELECT` | Seleção de pista de Arcade, Batalha ou Contra o Tempo. |
| 10 | `SCREEN_CUP_SELECT` | Seleção de copa. |
| 11 | `SCREEN_PAUSE_MENU` | Menu de pausa sobre a corrida. |
| 12 | `SCREEN_CUTSCENE` | Cena que aceita avanço/pulo por toque. |
| 13 | `SCREEN_PAUSE_QUIT_CONFIRM` | Confirmação de saída da pausa. |
| 14 | `SCREEN_PAUSE_OPTIONS` | Configurações abertas pela pausa. |
| 15 | `SCREEN_ADVENTURE_GARAGE` | Garagem de seleção do modo aventura; fontes no overlay e comandos direcionais nativos. |
| 17 | `SCREEN_MASK_HINT` | Fala da máscara; tela cheia aceita toque para pular. |
| 18 | `SCREEN_ADVENTURE_ARENA` | Corrida no mapa do modo aventura. |
| 19 | `SCREEN_PAUSE_SUBMENU` | Submenus da pausa; listas e dicas do Aku Aku com seleção por toque. |
| 20 | `SCREEN_RACE_RESULTS_MENU` | Ações da tela de fim de corrida, usando as opções nativas do modo ativo. |
| 22 | `SCREEN_LOADING` | Carregamento/transição nativa, sem controles interativos. |

No Android, `Platform_BeginFrame()` limpa os buffers dos produtores de UI. Depois de apresentar o frame, `Ctrds_PublishAndroidTouchFrame()` resolve a tela e publica estado, geração, frame, timestamp monotônico e dados da tela sob um spinlock próprio. JNI copia apenas esse pacote publicado (`nativeTouchSnapshot()`); a thread Android não percorre menus ou drivers vivos da engine. Os estados de entrada/saída já fornecidos pelo jogo continuam decidindo quando as seleções estão disponíveis.

O cabeçalho JNI contém `screen|generation|frame|producedNs|inputReady`. Nas seleções, textos e coordenadas continuam publicados durante a animação; ações só aparecem e são aceitas quando `inputReady=1`. Essa flag participa da geração e é conferida tanto pelo overlay quanto pela engine. Mudanças apenas nos textos/coordenadas notificam a UI sem invalidar a geração a cada frame.

A geração muda quando a tela, o menu folha, suas linhas ou a fase lista/detalhe de dicas mudam. O callback JNI agenda uma atualização na thread de UI, agrupando notificações pendentes; o polling de 50 ms permanece para controles e dados dinâmicos. A Activity aplica o pacote na mesma tarefa da UI, sem intercalar desenho ou eventos de toque, e evita reprocessar conteúdo idêntico. O overlay libera ponteiros antes de aplicar os dados novos; a troca de preset não apaga mais os textos de copa/garagem recebidos.

Toques de menu e máscaras de botões carregam a geração apresentada pelo overlay. A engine rejeita gerações antigas e limpa comandos pendentes e botões touch ao publicar uma troca real, preservando entradas mantidas nos frames estáveis e o gamepad. Ao consumir uma escolha/coordenada, também confere a identidade da folha e das linhas capturadas no enqueue: uma troca de menu antes da próxima publicação não transfere uma escolha antiga ao novo menu. Liberação neutra continua permitida. `Platform_InputReleaseTouch()` libera botões/eixos sem ativar a camada touch quando ela está desabilitada.

Os caminhos de apresentação direta de VRAM também publicam e notificam após o swap; seus buffers de produtores são limpos, porque substituem a cena inteira. Isso evita congelar o pacote anterior durante logos ou Scrapbook. A arte nativa do menu raiz permanece suprimida também quando `RECTMENU_DrawSelf()` percorre a hierarquia; geometria, callbacks e desenho dos filhos continuam funcionando.

Se adicionar uma tela ou produtor, atualize `CtrdsAndroidTouchSnapshot`, a montagem JNI, `CTRTouchSnapshot`, o callback e os ramos de desenho/hit test. Um produtor deve renovar seu buffer em cada frame no qual seu conteúdo deve aparecer. Não reutilize getters separados da engine para montar a apresentação Android. O swap SDL e o Canvas Android ainda são superfícies distintas: este contrato garante consistência dos dados, mas a composição visual precisa de validação em aparelho.

## Fluxos criados e decisões de interface

### Carregamento do jogo

- `CTRNativeActivity` cobre a inicialização do jogo com imagem e barra de progresso. A barra usa 25 segundos como referência, desacelera depois disso e continua avançando enquanto o menu real não fica pronto.
- O jogo avisa o host pelo callback nativo `onNativeMainMenuReady()`. Na mesma tarefa de UI, o host aplica o snapshot pronto e remove a camada de loading, sem uma etapa preta intermediária.
- Ao alterar loading, preserve esse sinal de prontidão. Um temporizador sozinho pode esconder a tela antes de o menu estar pronto ou deixar a arte sobreposta ao menu.
- `raw/loading.mp3` reproduz em loop enquanto a imagem inicial está visível. A Activity pausa/retoma o player com seu ciclo de vida e o libera ao chegar ao menu ou encerrar. O stream SDL inicia com ganho zero, mantendo SPU/XA ativos; após parar o MP3, `nativeFinishStartupAudio()` restaura o ganho. Os carregamentos seguintes usam o áudio normal do jogo.
- Durante carregamentos internos, `SCREEN_LOADING` desenha **CARREGANDO** inteiro em Luckiest Guy; o Android suprime apenas o desenho das letras nativas desse loading. Controles e configurações ficam ocultos até o fim do carregamento e da cortina quadriculada de entrada na cena.

### Launcher e configurações

- O launcher diferencia arquivo ausente e carregado, mantém o seletor Android e a validação BIN/CHD e só permite iniciar com imagem válida. O recurso `drawable-nodpi/ctr_logo.png` fornece a logo CTR Mobile exibida pelo overlay no menu principal e nos submenus textuais; `MM_Title.c` oculta a marca 3D antiga durante esses menus.
- O nome exibido ao jogador é o nome legível do arquivo; o nome interno importado não é necessariamente o nome de exibição.
- As configurações do launcher e do jogo têm entradas visuais próprias. Dentro da partida, o diálogo é criado pelo host do jogo; não deve iniciar novamente o launcher.
- **Início rápido** é uma preferência Android compartilhada pelos diálogos do launcher e do jogo. Se estiver em **Sim** e o disco importado passar pela validação do launcher, uma inicialização nova abre o jogo diretamente; sem disco válido, o launcher continua disponível.
- No menu principal Android, **OPÇÕES** abre o mesmo diálogo de áudio usado na pausa. Fechá-lo retorna ao menu principal sem enviar uma seleção ao menu nativo.
- O menu principal oferece **MODO AVENTURA**, **ARCADE LOCAL**, **OPÇÕES** e **SAIR** no Android. Os modos seguem os destinos nativos; seleções em lista e menus de resultado usam o overlay responsivo.
- **SAIR**, abaixo de **OPÇÕES**, libera os controles de toque e encerra a tarefa Android com `finishAndRemoveTask()`. O ciclo de encerramento da Activity libera o player de loading e a engine SDL.
- O diálogo de áudio também oferece **CONTROLES**, que abre `CTRControlsActivity`; ao voltar, o diálogo permanece disponível.
- Launcher, host e editor suprimem animações de abertura/fechamento entre Activities e reaplicam o modo imersivo ao retornar.
- O atalho de configurações em jogo é instalado/atualizado pelo host e abre tanto o diálogo quanto a tela secundária de controles. Ao ajustar sua posição ou visibilidade, confira todos os estados e também a cena de corrida.
- Os valores gerais de vídeo são compartilhados pela classe `CTRDSSettings`; as posições/tamanho dos controles de corrida são preferências distintas, descritas abaixo.

### Seleção de personagem e pista

- A seleção de personagem tem uma ação textual **VOLTAR** separada de **CONTINUAR**. Tocar num retrato seleciona; a ação de continuar confirma e avança.
- No Android, `CTRTouchOverlay` desenha o título e o nome do personagem com Luckiest Guy, contorno e tamanhos calculados pela área disponível. `MM_Characters.c` oculta esses textos nativos somente sob `__ANDROID__`; o nome selecionado chega pelo snapshot JNI.
- A seleção de pista tem hitboxes e composição próprias. **VOLTAR** fica junto de **INICIAR CORRIDA**, fora da coluna/lista de pistas.
- Os hit-tests dos botões de personagem e pista usam os mesmos limites normalizados dos botões desenhados, com uma margem de toque pequena. Selecionar retrato/pista não confirma a etapa; a confirmação fica em **CONTINUAR** ou **INICIAR CORRIDA**.
- Não reutilize a mesma geometria ou hitbox para as duas telas: suas áreas de conteúdo e ações são diferentes. O desenho e o hit test correspondentes vivem em `drawCharacterSelectActions`/`drawTrackSelectActions` e nos caminhos de toque em `CTRTouchOverlay`.
- No Android, títulos e nomes das seleções de pista e copa usam Luckiest Guy no overlay. `MM_TrackSelect.c` e `MM_CupSelect.c` enviam strings localizadas e coordenadas nativas pelo snapshot JNI; cartões, destaques, seleção e transições continuam desenhados pelo jogo.
- Na copa, cada hitbox é calculada a partir da posição animada do nome do cartão e das dimensões do retângulo nativo; não divida a tela em quadrantes fixos.
- Os botões de voltar dessas duas telas usam azul; os de confirmação usam laranja. A lista e o fluxo de pistas permanecem sob responsabilidade dos arquivos `game/230/`.

### Garagem do modo aventura

- `CS_Garage_MenuProc` mantém a cena e envia textos localizados e valores animados dos atributos. No Android, `CTRTouchOverlay` desenha um card preto com borda branca, barras em gradiente, nome e setas em Luckiest Guy; outras plataformas mantêm o desenho nativo.
- `SCREEN_ADVENTURE_GARAGE` é identificado pelo menu nativo da garagem. Toques sobre as setas inferiores enviam esquerda/direita, o centro confirma e **VOLTAR** envia cancelar; mantenha desenho e hitboxes alinhados.
- O Android usa um único arquivo de campanha e não apresenta mais a tela antiga de quatro slots. Os saves legados permanecem intactos; o seletor nativo compartilhado continua disponível fora desse fluxo Android.
- O nome é definido somente na criação da campanha. O gerenciamento mostra os dados salvos, **DELETAR** e **VOLTAR**; não oferece renomeação.
- Sem campanha, a lista mostra somente **NOVA CAMPANHA**; o **VOLTAR** superior retorna ao menu principal. A exclusão abre um diálogo modal com **SIM** e **NÃO** (seleção inicial); cancelar retorna ao gerenciamento e excluir retorna à lista vazia.
- No Android, se um callback de menu substituir `rows`, `RECTMENU_ProcessInput` preserva o `rowSelected` definido pelo callback. Restaurar o índice da lista anterior pode selecionar o terminador da lista ao voltar do gerenciamento.

### Pausa e cenas

- A pausa usa as mesmas linhas das subtelas textuais, sem painel azul central. **VOLTAR** ocupa o mesmo canto superior direito na pausa, nos submenus e na confirmação de saída. A área de toque cobre os cartões e o espaço entre linhas; mantenha desenho e hit-test ligados aos mesmos cálculos. Preserve os comandos originais e confirme o mapeamento dos itens pelo snapshot do menu e pelo fluxo nativo.
- Na pausa principal, **VOLTAR** aciona a mesma escolha nativa de **RETOMAR**. Toques discretos de confirmar, voltar, pausa e câmera mantêm um pulso mínimo de 140 ms para sobreviver a um down/up entre ticks; direção e aceleração continuam dependentes do dedo mantido.
- O cooldown inicial da pausa bloqueia comandos, mas permite atualizar os dados da lista, incluindo **PROGRESSO**, antes de publicá-la. Legendas de portais da Aventura ficam a 25% da altura no Android touch para não disputar os botões inferiores; títulos de início de corrida aguardam a cortina quadriculada sair.
- No modo Aventura, **PROGRESSO** abre um diálogo central com os mapas e recompensas da página nativa de pausa. As setas mudam de mundo e o botão superior **VOLTAR** fecha o diálogo para retornar à pausa.
- Os conselhos do Aku Aku seguem **lista → detalhe/fala → pular ou voltar**. Tocar numa linha abre a fala; deslizar verticalmente percorre linhas fora da área visível. No detalhe, apenas **VOLTAR** fecha a tela, evitando que um toque acidental no texto descarte a dica. Durante a fala da máscara, qualquer toque envia o comando de pulo e os controles do kart e o atalho de configurações ficam ocultos.
- Opções e confirmação de saída são estados separados: opções abrem o diálogo Android de áudio/controles; a confirmação oferece cartões próprios e o botão superior **VOLTAR** cancela e retorna à pausa.
- Demonstração e cutscenes têm estados distintos para que um toque possa avançar/pular sem aparecer como controle de direção.
- Mudanças em menus/fluxos devem preservar o destino de saída por modo (Arcade, copa, aventura, batalha e Contra o Tempo), definido no jogo.

### Editor de controles

- **GAMEPAD** abre `CTRGamepadActivity` a partir do editor. Cada ação captura um botão ou gatilho físico; bindings repetidos trocam de ação. **SALVAR** persiste `gamepad_binding_<índice>` em `CTRNativePrefs`; **VOLTAR** descarta o rascunho e **RESTAURAR PADRÃO** só é aplicado ao salvar. Os 16 índices seguem `CTRGamepadMapping.ACTIONS`, com valores lógicos SDL (gatilhos usam `0x4000 | eixo`). O mapeamento é compartilhado entre gamepads e carregado pelo host ao retomar, com cópia protegida por spinlock na entrada nativa.
- No menu principal Android, direcional/analógico muda o destaque e confirmar ativa a opção, incluindo **OPÇÕES** e **SAIR**. Nas demais telas do jogo a entrada continua na SDL; o analógico esquerdo também fornece direção digital nos menus. Diálogos de campanha encaminham eventos físicos para a SDL; diálogos Android de opções usam foco nos botões e sliders.
- Com gamepad conectado, corrida e arena da aventura ocultam botões de toque e atalho de configurações, mesmo no modo de toque sempre visível. O overlay libera entradas antigas e não mantém hitboxes invisíveis de corrida. Ao desconectar, a exibição volta a seguir a preferência de toque. Um analógico touch neutro não sobrescreve o analógico físico, nem altera seu modo de pad.

- `CTRControlsActivity` é uma tela secundária imersiva, registrada no `AndroidManifest.xml`. Voltar termina a activity sem salvar alterações pendentes; **SALVAR** persiste e volta ao jogo/launcher.
- O preview usa `R.drawable.race_view`, com os controles arrastáveis por cima. A posição é normalizada entre 0 e 1 para acompanhar a resolução e o tamanho do preview.
- Preferências ficam em `CTRNativePrefs`: `race_control_<id>_x`, `race_control_<id>_y` e `race_control_scale_percent` (limitado a 70–150%). Corrida e arena de aventura usam o mesmo conjunto de controles, posições e escala; menus e diálogos mantêm suas ações próprias.
- O controle `weapon` usa `weapon.png` e envia Círculo para usar o item equipado. Sua posição inicial é `(0.875, 0.56)`, acima do acelerador; posição, escala e restauração seguem o editor. Toques curtos mantêm um pulso de 140 ms, inclusive enquanto acelera.
- Corridas e arena de aventura não oferecem botão de câmera no HUD nem no editor; o botão de pausa permanece no canto superior direito.
- A posição padrão da pausa fica no topo/direita com margem de 2% do lado curto, usando o mesmo cálculo no editor e no HUD. Posições personalizadas salvas continuam prevalecendo.
- Ao mudar o conjunto de botões, atualize os IDs, ícones, posições padrão, hit targets reais no overlay e a rotina de restaurar. O preview é uma ferramenta visual; não prova sozinho o comportamento em aparelho.
- A composição deve respeitar os insets seguros do notch, centralizar o preview no espaço disponível e manter uma distância visível entre o preview e os controles de tamanho/restauração.

## Regras práticas ao ajustar uma tela

1. Identifique se o elemento é do launcher, do host Android, do overlay de toque ou da UI C do jogo.
2. Para controles desenhados pelo overlay, ajuste desenho e região de toque juntos. Preserve suporte a múltiplos dedos e limpe entradas mantidas em transições.
3. Calcule dimensões a partir da área útil e do lado curto da tela; não fixe coordenadas para uma única captura. Em diálogos, centralize o painel como uma unidade e reserve padding para título e botões do cabeçalho.
4. Preserve ações explícitas: selecionar não deve confirmar automaticamente quando a tela tem botão **CONTINUAR**; voltar não pode cobrir a lista ou o alvo principal.
5. Para assets, confirme recurso e nome no projeto antes de referenciar. Mantenha texto editável fora da imagem de fundo.
6. Preserve o identificador Android `com.ctrnative`. Para comportamento exclusivo do Android no código nativo, use `__ANDROID__`; `CTR_NATIVE` identifica o código nativo do app.

## Componentes reutilizáveis e limites

As telas usam dois tipos de renderização; escolha o componente pelo dono do desenho, não apenas pela aparência:

| Padrão | Reutilização atual | Contrato |
|---|---|---|
| Linha de configuração com valor cíclico | `CTRSettingsOptionRow` | Compartilha rótulo, setas, pesos e dimensões entre launcher e jogo. O chamador fornece a view do valor e recebe a direção do ciclo; continua dono de carregar/salvar as configurações. `Style.LAUNCHER` preserva toque/pressionamento longo no valor; `Style.IN_GAME` mantém somente as setas. |
| Botão de ação em Canvas | `CTRTouchOverlay.drawSelectionActionButton()` | Desenha forma, contorno e texto dentro dos limites recebidos. Cada tela define seus próprios limites e deve manter seu hit test sincronizado; não compartilhar coordenadas de personagem, pista e pausa. |
| Texto e posicionamento de views | Helpers locais nas Activities | `makeSettingsText()` e `placeSettingsView()` servem aos diálogos do jogo; `text()` e `place()` servem ao editor. Permanecem locais enquanto fonte, comportamento ou hierarquia forem diferentes. |
| Fonte das interfaces Android | `CTRFont.get(context)` | Um único `Typeface` Luckiest Guy em cache, sem reter Activity/Context. Preserve fontes de símbolos que não existem no TTF, como o fechamento `×`. |
| Botões gráficos do launcher | Classes internas de `CTRNativeLauncherActivity` | `LauncherActionButton`, `LauncherGearButton` e `LauncherPanelView` pertencem ao launcher. Extraia-os somente quando outra tela precisar do mesmo desenho e dos mesmos estados. |

### Ao criar um componente

1. Procure duplicação real antes de extrair. A linha de configuração era montada separadamente em `CTRNativeLauncherActivity.buildSettingsPanel()` e `CTRNativeActivity.showInGameSettings()`; a geometria e o controle de ciclo eram duplicados, então agora usam `CTRSettingsOptionRow`.
2. Passe para o componente apenas os dados que variam. O componente organiza e desenha a linha; a tela continua responsável por persistência, navegação e regras do jogo.
3. Preserve diferenças de contexto em modos nomeados e poucos. Não passe coordenadas ou um conjunto extenso de flags para forçar telas distintas a parecerem iguais.
4. Para `Canvas`, mantenha desenho e hit test no mesmo dono da tela. Compartilhe primitivas visuais, mas calcule regiões de toque e layouts por estado.
5. Para novas telas, comece dentro do dono atual. Extraia uma classe de tela ou view quando houver uma unidade visual com estado/eventos próprios, ou quando um padrão concreto tiver dois ou mais chamadores.

### O que ainda não deve ser compartilhado

- O diálogo de volumes da pausa usa `SeekBar`, valores JNI e a geometria de tela cheia; não é a mesma linha cíclica de `CTRDSSettings`.
- O diálogo de configurações do launcher e o diálogo durante o jogo compartilham a linha de opção, mas têm janela, cabeçalho, botão de saída, dimensões e retorno diferentes. A extração atual intencionalmente para na linha.
- As telas de seleção continuam com listas/cards desenhados pelo jogo nativo e ações desenhadas pelo overlay. Não replique a tela inteira em Views Android para reutilizar apenas o botão.

## Verificação adequada

- Mudança de Java/recursos Android: `cd android; .\gradlew.bat :app:assembleDebug` (ou o wrapper equivalente do checkout).
- Mudança na ponte C/JNI ou em menus nativos: compile pelo fluxo Android. Use `__ANDROID__` para comportamento exclusivo do Android; `CTR_NATIVE` identifica também o código nativo compartilhado.
- Mudança visual/interativa: valide no aparelho/emulador em paisagem e no formato de tela alvo. Build bem-sucedido confirma compilação, não centralização, área de toque, notch ou transição visual.
- Ao documentar validação, diga se foi build, inspeção estática ou teste real no dispositivo.

## Histórico de direção visual desta conversa

As referências e correções convergiram para fundo azul quadriculado, títulos em Luckiest Guy, valores de configuração amarelos, botões de ação laranja e botões secundários azul-claro/azul. O painel de configurações deveria ser um retângulo central com espaço interno e opções em linhas; os controles avançados ficam em uma tela secundária imersiva. Nos menus em lista, opções sem foco usam preenchimento carvão translúcido e contorno claro; a opção focada usa o destaque laranja. Subtelas textuais e pausa não desenham painéis azuis nem faixas diagonais sobre o cenário. **VOLTAR** fica no topo, sem botão **CONTINUAR**; tocar numa opção seleciona e ativa essa opção. Nas telas de seleção, preservar a aparência original do jogo e posicionar as ações sem cobrir mapas/personagens. Use essas decisões como contexto, mas confira o código atual antes de presumir que cada detalhe visual continua implementado.

## Referências do checkout

- [Mapa completo dos fluxos do jogo](game-flow-map.md)
- [Especificação do launcher](superpowers/specs/2026-10-15-disc-launcher-redesign.md) — proposta de design; verifique o estado atual no código.
- [Especificação de layouts de toque](superpowers/specs/2026-10-15-touch-layouts-by-screen.md) — proposta inicial de presets; o overlay atual tem estados mais específicos.
- [Notas de build Android](../README_ANDROID.md)
