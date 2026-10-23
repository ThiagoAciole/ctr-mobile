# Auditoria de transições Android — 21/10/2026

## Escopo e evidências

Checkout `ctr-native-android`, remote `ThiagoAciole/ctr-mobile`, base `8849df4bd`; árvore limpa antes da execução. Referências: arquitetura e guia Android, mapa Mermaid dos fluxos e capturas existentes de menu principal, personagem, pista e copa. As imagens são baseline visual, não prova de execução desta alteração. Nenhum dispositivo estava conectado em `adb devices`.

| Camada | Responsabilidade preservada |
|---|---|
| Launcher | Disco, configuração, inicialização; geometria recalculada quando os bounds mudam. |
| Activity | Loading por sinal nativo, diálogos e aplicação do pacote na thread Android. |
| Overlay | Canvas, hitboxes e encaminhamento de entrada; selecionar personagem continua separado de confirmar. |
| Engine/menus | Estado, navegação, animações, áudio e dados da seleção permanecem em C. |
| Renderer/JNI | SDL apresenta a cena; o pacote da UI é publicado depois do swap e copiado por uma chamada JNI. |

## Diagnóstico e correções

| Evidência no código anterior | Correção |
|---|---|
| `updateTouchControls()` lia estado, lista, personagem, resultado e outros dados em chamadas JNI independentes. O lock individual de cada string não protegia a combinação. Alguns getters percorriam estruturas vivas da engine na thread Android. | `Ctrds_PublishAndroidTouchFrame()` captura o pacote na thread do jogo. `nativeTouchSnapshot()` copia uma publicação inteira sob lock; `CTRTouchSnapshot` valida o envelope e mantém campos imutáveis. |
| Buffers de menu/seleção permaneciam disponíveis até algum produtor sobrescrevê-los, inclusive quando nenhum menu os renovava. | Limpeza no início de cada frame Android e publicação somente dos campos pertinentes à tela atual. |
| O produtor de fantasmas incluía a seleção no corpo, e o envelope a prefixava novamente. Isso transformava o número/título em linhas visuais e deslocava escolhas. | Seleção aparece uma única vez no envelope. O teste do cabeçalho falhou antes da correção e passou depois. |
| Activity aplicava textos antes de `setScreenState()`, que limpava `selectionTextLabels` para qualquer destino diferente de pista. Copa/garagem perdiam dados no primeiro frame aplicado. | `beginSnapshot()` troca preset/libera inputs primeiro; os dados são aplicados depois. Removida a limpeza de texto do setter de preset. |
| `MM_Title_ThTick()` removia `HIDE_MODEL` a cada tick e só voltava a ocultar a marca 3D em `IN_MENU`. Durante saída/retorno ela ficava visível. | A marca Android permanece substituída após o marco original de prontidão do título, incluindo saída e retorno. A intro anterior a esse marco conserva seu tratamento. |
| A proteção do menu raiz em `RECTMENU_ProcessState()` dependia de `IN_MENU`; `Ctrds_PrepareTouchMenu()` podia tratar o menu raiz em saída como lista genérica. | Propriedade da arte do menu raiz permanece Android após a prontidão; o menu raiz não alimenta o snapshot genérico. |
| Callback só reconhecia mudança do inteiro de tela. Submenus podem compartilhar esse inteiro; comandos de toque não identificavam a tela que os originou. | Geração considera tela, folha, linhas e lista/detalhe das dicas. Notificação por geração; comandos antigos são rejeitados sob o mesmo lock da publicação. |
| Uma escolha podia ser consumida depois de instalar outro `ptrDesiredMenu`, antes da próxima publicação. | O consumo valida também a folha/linhas originais da fila, incluindo substituição antes de publicar. |
| O ramo de VRAM retornava logo após o swap, sem passar pela notificação. Leitura somente do pacote publicado tornaria esse ramo uma regressão. | Cena normal e VRAM usam o mesmo helper de publicação/notificação; VRAM limpa dados da cena substituída. Há teste dos dois caminhos e do retângulo VRAM customizado. |

Essas são inconsistências comprovadas por leitura e testes de estado. Ainda não há gravação que atribua cada flash observado a uma causa específica, nem comprovação de eliminação de todo flicker visual.

## Tipografia, responsividade e performance

Luckiest Guy já era usada no Canvas das seleções, menus e diálogos customizados. `CTRFont` centraliza o carregamento duplicado no launcher, host, overlay, editor, linha de opções e entrada de nome. Status do launcher passa a usar a mesma fonte. Fontes de símbolos compatíveis são preservadas. HUD nativo, sprites numéricos e textos de fluxos que ainda pertencem à engine não receberam substituição global nem atlas novo.

O launcher deixa de depender de um único `post()` e recalcula a composição quando muda de tamanho. Não houve mudança de cores, proporções dos componentes, assets ou rotas. O overlay continua usando os mesmos cálculos compartilhados de desenho e hit-test para ações.

A Activity evita parsing, reconstrução de listas e invalidações dos setters quando o conteúdo publicado não muda. Notificações pendentes são agrupadas e o Handler é encerrado em `onDestroy()`. O polling continua em 50 ms; não foram adicionados sleeps, timers de transição ou bloqueios de espera de frames. O lock protege apenas cópias/entrada; não envolve desenho, callback JNI ou operações de GL.

## Instrumentação

Ative somente em APK debuggable:

```powershell
adb shell setprop log.tag.CTR-UI DEBUG
adb logcat -v monotonic -s CTR-UI:V SDL:V
```

O polling habilita a flag nativa. Há registros apenas em mudanças: `publish` (timestamp SDL monotônico em ns, frame, geração, tela anterior/nova, identidade do menu e propriedade da lista), `receive` (pacote recebido na UI), `apply` e primeiro `draw` daquela geração. Os timestamps Java usam `uptimeMillis`; correlacione as camadas por geração/frame, não subtraia relógios de plataformas diferentes. `draw` indica execução de Canvas, não apresentação física pelo compositor. A notificação indica publicação, não o instante inicial de cada solicitação interna da máquina de estados. Não há detector automático de pixels nativos indevidos.

Para desativar:

```powershell
adb shell setprop log.tag.CTR-UI INFO
```

## Validação e limites

Resultado final: `:app:assembleDebug` passou em **1 min 14 s**, com `arm64-v8a`, `armeabi-v7a` e `x86`. APK: `android/app/build/outputs/apk/debug/app-debug.apk`. Há avisos C em arquivos fora do diff (como `PlayLevel.c` e `native_gpu.c`) e avisos Java sobre source/target 8 obsoletos; não houve erro de build. Testes C/Java passaram e `git diff --check` não encontrou erros. Nenhum commit foi criado.

Os testes host executam o código C de publicação/fila extraído diretamente de `ctrds.c`, com tipos da struct de produção e stubs pequenos de SDL/engine, além de `Platform_EndScene()` real e do cabeçalho do produtor de fantasmas. Cobrem pacote incompleto ainda não publicado, limpeza de produtor ausente, geração de submenu e dica, descarte de comandos antigos antes/depois de publicar, preservação de botões nos frames estáveis e leitura concorrente de 20.000 publicações. O teste Java cobre envelope inválido, inicialização sem geração, metadados, imutabilidade e comparação de conteúdo. Controles negativos em arquivos temporários confirmaram falha dos testes ao desativar a validação do alvo da fila e a publicação VRAM. Eles não simulam o ciclo completo da engine nem o compositor.

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
python tests/run_android_touch_tests.py --cc C:/msys64/ucrt64/bin/gcc.exe
cd android
.\gradlew.bat :app:assembleDebug --console=plain
```

O GCC deve estar no PATH junto de suas DLLs; o runner também requer JDK. Não foi adicionada dependência ao app.

Permanecem pendentes em aparelho: todos os fluxos de launcher/menu, Arcade/seleções/carga/corrida, pausa/filhos/retomada e Aventura/campanha/garagem/diálogos; toque e gamepad; gravação de transições em 60/120 FPS; notch e proporções 16:9, 18:9, 19.5:9 e 20:9. Em especial, o renderer possui viewport com letterboxing para proporção fixa, enquanto as coordenadas normalizadas de conteúdo nativo no overlay ainda usam sua área inteira; o modo padrão acompanha a proporção da tela, mas a opção 4:3 precisa de validação e ajuste conjunto de geometria, não padding global. Nomes extensos de disco e redimensionamento dos painéis de editor/configuração também precisam de verificação visual. Não houve medição de FPS, latência ou consumo; custo baixo do pacote não substitui benchmark.
