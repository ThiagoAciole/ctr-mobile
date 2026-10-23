# Recursos do CTR Mobile

Este fork do [CTR Native](https://github.com/Simon358/ctr-native-android) é um
port Android de Crash Team Racing (PS1, 1999), baseado na descompilação do
[CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK).

## Recursos

- Tela companheira para handhelds Android de duas telas, com HUD e mapa.
- Limites de quadros acima de 30 fps, preservando a cadência da lógica do jogo.
- Resolução interna configurável até 8x (`240 × escala` linhas).
- Widescreen, arte HD para o HUD, imagens CHD e controles de toque.
- Launcher para escolher a imagem do jogo e ajustar configurações.

O Gradle está configurado para `armeabi-v7a`, `arm64-v8a` e `x86`. Isso descreve
as ABIs incluídas no APK; compatibilidade visual e de desempenho depende do
aparelho ou emulador.

## Compilar

Consulte o [guia Android](README_ANDROID.md) para requisitos, instalação e
importação dos arquivos do jogo. Com o Android SDK instalado:

```powershell
cd android
.\gradlew.bat :app:assembleDebug
```

O variant debug usa `-O2` no código nativo, configurado em `android/app/build.gradle`.

## Configurações

`ctrds.cfg` fica na pasta de assets do app. O launcher e o painel durante o jogo
salvam as opções reconhecidas e preservam outras linhas do arquivo.

| Chave | Valores | Efeito |
|---|---|---|
| `target_fps` | 30, 60, 90, 120, … | Limite de quadros; VBlanks são emitidos nessa taxa. |
| `internal_scale` | 1–8 | Altura do alvo de renderização: `240 × escala` linhas. |
| `aspect_mode` | 0, 1 | 0 mantém 4:3; 1 acompanha a proporção da tela. |
| `fxaa`, `crt` | 0, 1 | Ativam os filtros correspondentes. |
| `touch_controls` | 0, 1, 2 | Desligado, ligado ou automático. |
| `swap_face_buttons` | 0, 1 | Troca os botões de ação principais. |

Opções adicionais reconhecidas pelo código:

| Chave | Efeito |
|---|---|
| `aspect_mode=2`, `aspect_w`, `aspect_h` | Define manualmente a proporção da tela. |
| `vblank_mult` | Ajusta o multiplicador de VBlank em aparelhos compatíveis. |
| `hd_art` | Usa arte substituta em `assets/hd/`. |
| `hd_dump`, `hd_log` | Auxiliam a criação de arte HD, exportando ícones e índices. |
| `skip_av` | Ignora vídeo de abertura e áudio XA; também pode ser detectado automaticamente. |
| `prim_reject` | Descarta primitivas grandes; valor `2` registra os descartes. |
| `perf` | Gera `perf/frame_times.csv` com tempos por etapa. |
| `widescreen` | Sobrescreve a proporção derivada de `aspect_mode`. |
| `online`, `online_host`, `online_port`, `online_name`, `online_room` | Configuram conexão OnlineCTR; não há servidor público configurado. |

## Diagnóstico

Uma linha de desempenho pode mostrar FPS, limite alcançável, estado do jogo,
escala, consumo de áudio e atividade do disco. Em menus, a cadência da lógica
continua em 30 fps mesmo quando a tela atualiza mais rápido.

O watchdog encerra o processo se nenhum VBlank for emitido por doze segundos
com a janela em primeiro plano, para produzir um tombstone com backtrace. A
variável `CTRDS_WATCHDOG=0` desativa esse watchdog.

## Limitações registradas

- Há relatos de quadriláteros lilás em algumas geometrias e de um retângulo no
  canto inferior esquerdo da tela; a causa ainda não está identificada.
- No modo de duas telas, quadros podem ser reapresentados nos menus enquanto a
  lógica continua em 30 fps.
- O código de jogo online existe, mas não há servidor público configurado.

Essas observações devem ser reavaliadas em aparelho ao investigar problemas de
imagem ou desempenho.
