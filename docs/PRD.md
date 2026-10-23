# PRD — CTR Mobile

## Visão do produto

CTR Mobile é um port de Crash Team Racing para Android, baseado no CTR Native e no CTR-ModSDK. O app combina o jogo nativo em C/C++ com uma camada Android para importar o jogo, configurar a experiência e oferecer controles por toque.

## Usuários e necessidades

- Jogadores em dispositivos Android que querem iniciar o jogo usando uma imagem compatível que já possuem.
- Jogadores que precisam usar controles de toque configuráveis ou um gamepad Android.
- Jogadores que querem ajustar vídeo e áudio no launcher ou durante a partida.

## Escopo documentado

- Importar uma imagem BIN/CHD compatível usando o seletor de arquivos do Android e armazenar a cópia no espaço privado do app.
- Inicializar e executar o jogo por meio do launcher Android e da engine nativa.
- Disponibilizar controles touch e suporte a gamepad, configurações e editor de controles.
- Preservar os modos, progressão e regras originais do jogo sempre que a UI Android encaminhar ações para a engine.
- Gerar APK Android pelo Gradle, para as ABIs configuradas no projeto.

## Fora de escopo

- Executar o jogo como aplicativo desktop Windows, macOS ou Linux.
- Distribuir imagens de disco ou outros assets protegidos por direitos autorais.
- Prometer servidor público ou serviço online: o código relacionado a OnlineCTR não comprova que exista infraestrutura disponível.
- Reimplementar em Java a progressão e as regras que já pertencem ao jogo nativo.

## Requisitos de produto

1. O launcher deve distinguir a ausência de imagem de jogo de uma imagem carregada e oferecer o fluxo de seleção/importação documentado.
2. O jogo deve manter as regras de navegação e confirmação da engine; a camada Android apresenta controles e encaminha entrada.
3. Controles touch devem corresponder ao estado visível, liberar entradas em transições e respeitar a preferência salva e o estado do gamepad.
4. Configurações editáveis devem manter as chaves e formatos persistidos existentes, salvo migração planejada.
5. Recursos dependentes de dispositivo devem ser descritos como compatíveis ou não verificados, conforme evidência real.

## Restrições e dependências

- Android SDK, NDK `27.2.12479018` e CMake `3.22.1` são os requisitos documentados para build.
- O jogo requer imagem compatível fornecida pelo usuário e dispositivo/emulador com OpenGL ES 3.0.
- A lista de ABIs, `minSdk`, `targetSdk` e versões do produto são configurações de build que podem mudar; confirme os arquivos Gradle antes de publicar números aqui.

## Critérios de aceite

- O fluxo afetado compila no alvo Android aplicável.
- Testes host relevantes passam quando existem para a área alterada.
- Interação, apresentação visual e compatibilidade com dispositivos são aceitas por teste em aparelho/emulador quando a mudança afeta esses aspectos.
- O relato distingue claramente inspeção estática, teste/build e evidência em dispositivo.

## Estado e incertezas

Este PRD registra o produto e os contratos descritos nos guias atuais; não é uma declaração de que todos os fluxos foram recentemente testados. O [relatório de validação](validation/REPORT.md) contém achados parciais, hipóteses e limites de evidência. Reconfirme requisitos no código e em aparelho antes de tratar uma observação histórica como estado atual.
