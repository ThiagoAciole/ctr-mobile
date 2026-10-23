# Replays de diagnóstico

O scheduler nativo contém suporte interno a gravação e reprodução por argumentos
de processo (`--record` e `--replay`). A implementação gera relatórios com
estado, checkpoints e dados da partida.

## Limite no app Android

O launcher Android não oferece controles para iniciar gravação, abrir replay ou
enviar esses argumentos ao processo SDL. Portanto, as antigas instruções com
`build/ctr_native`, teclas F5–F10 e caminhos desktop não se aplicam ao APK atual.

Para investigar falhas no app, capture os logs Android conforme o
[guia Android](../README_ANDROID.md). Não use um relatório de replay como
reprodução disponível no APK até que exista uma interface Android para iniciar
e abrir esses arquivos.

## Alterações futuras

Antes de expor o scheduler no Android, defina onde os relatórios serão salvos,
como serão compartilhados pelo Storage Access Framework e como o usuário
selecionará uma reprodução. Preserve as verificações de identidade e os saves
reais descritos na implementação em `platform/native_replay_scheduler.c`.
