# Guia Android

Este checkout compila o CTR Mobile para Android. A interface de build fica em
`android/`; o CMake raiz é chamado pelo Gradle para compilar a biblioteca nativa
e não é um fluxo de build desktop.

## Requisitos

- Android Studio e Android SDK instalados.
- NDK `27.2.12479018` e CMake `3.22.1`, instaláveis pelo SDK Manager.
- Dispositivo com OpenGL ES 3.0 ou emulador compatível.

O projeto inclui as ABIs `armeabi-v7a`, `arm64-v8a` e `x86`.

## Compilar e instalar

Abra `android/` no Android Studio, aguarde a sincronização Gradle e execute a
configuração `app`. No Windows, também é possível compilar pelo PowerShell:

```powershell
cd android
.\gradlew.bat :app:assembleDebug
```

No macOS/Linux, use `./gradlew :app:assembleDebug`. O APK de debug é gerado em
`android/app/build/outputs/apk/debug/app-debug.apk`. Para instalar em um
dispositivo conectado com ADB:

```powershell
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Execute o comando de instalação dentro de `android/`.

## Importar os arquivos do jogo

O jogo precisa de uma imagem NTSC-U compatível. No launcher, escolha `ctr-u.bin`
ou `ctr-u.chd` pelo seletor de arquivos. O app importa o arquivo para o
armazenamento privado; mantenha espaço livre suficiente para uma cópia completa.
Os arquivos do jogo não fazem parte deste repositório nem do APK.

## Controles

- Controles de toque aparecem quando não há gamepad conectado; também podem ser
  configurados no app.
- Gamepads Android são encaminhados pelo SDL3.
- A vibração depende do suporte do dispositivo e do controle.

## Diagnóstico

Veja os logs do app com:

```sh
adb logcat -s CTR-Native:V CTRNativeInput:V SDL:V AndroidRuntime:E
```

Se houver tela preta ou encerramento ao iniciar, confirme que a imagem é NTSC-U,
que a cópia terminou e que o dispositivo oferece OpenGL ES 3.0. Um build bem
formado confirma compilação; valide abertura, controles e orientação no aparelho
ou emulador de destino.
