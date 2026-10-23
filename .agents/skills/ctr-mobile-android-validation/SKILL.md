---
name: ctr-mobile-android-validation
description: Use when selecting or reporting validation for CTR Mobile Android builds, native/Java tests, UI flows, touch input, gamepad, or device compatibility.
---

# CTR Mobile: validação Android

Escolha evidência proporcional à mudança e mantenha resultados distintos.

1. Consulte `docs/CHANGE_CONTRACT.md` e identifique a camada/fluxo afetado.
2. Para build do app, use o wrapper Gradle em `android/` (`:app:assembleDebug`). O CMake raiz é invocado pelo Gradle e rejeita builds desktop.
3. Para código com cobertura host, escolha o script específico em `tests/`; confira pré-requisitos do script antes de executá-lo.
4. Para UI, input, lifecycle, gamepad ou compatibilidade, valide no aparelho/emulador relevante e registre modelo, fluxo e configuração. Se não houver dispositivo, declare essa lacuna.
5. Relate inspeção estática, testes/build e runtime visual separadamente. Resultado histórico em `docs/validation/REPORT.md` não comprova a versão atual.

Não rode a suíte inteira quando uma verificação focada responder à pergunta. Não afirme teste ou aceite de dispositivo que não ocorreu.
