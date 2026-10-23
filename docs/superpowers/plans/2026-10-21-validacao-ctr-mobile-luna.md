# Validação do CTR Mobile — execução pelo GPT 6 Luna

## Objetivo e limites

Validar a refatoração atual sem corrigir código. Produzir evidências para uma revisão posterior pelo modelo principal. Tempo previsto: 40–60 minutos com Android disponível; 5–10 minutos somente para testes/build.

Execute diretamente neste checkout: `C:\Users\Thiago Aciole\Projetos\ctr-native-android`. Não crie worktree: as mudanças a validar ainda não foram commitadas.

Não altere fontes, testes ou documentação existente; não faça commits, limpeza de dados, desinstalação, exclusão de saves ou download de discos. Crie apenas a pasta de evidências abaixo e arquivos normais do build. Pode instalar o APK debug com `install -r`, operar o app e alterar temporariamente configurações visuais, restaurando-as no final. Não avance em confirmação de exclusão, sobrescrita ou reinício de campanha existente.

Use a skill `verify-and-stop` e consulte `.agents/skills/ctr-mobile-ui/SKILL.md`. Sem subagentes. Não faça auditoria ampla nem releia todo o histórico. Se faltar dispositivo, disco válido ou ferramenta, registre a limitação e conclua as etapas independentes. Depois de três tentativas sem sucesso para a mesma falha, pare aquela etapa e registre a suposição possivelmente errada.

## 1. Preparação e identidade — 3 minutos

- [ ] Criar pasta exclusiva e guardar estado inicial, sem imprimir o diff completo:

```powershell
Set-Location -LiteralPath 'C:\Users\Thiago Aciole\Projetos\ctr-native-android'
$evidence = Join-Path $env:TEMP ('ctr-validation-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $evidence | Out-Null
git rev-parse HEAD | Set-Content (Join-Path $evidence 'head.txt')
git status --short | Set-Content (Join-Path $evidence 'status-before.txt')
git diff --binary --output="$evidence\worktree-before.patch"
git diff --check *> (Join-Path $evidence 'diff-check.txt')
$diffCheckExit = $LASTEXITCODE
Get-Command python,java,javac -ErrorAction SilentlyContinue | Select-Object Name,Source
$adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
if (Test-Path -LiteralPath $adb) { & $adb devices -l }
```

- [ ] Ler apenas `docs/android-ui-transition-audit.md` como mapa de hipóteses. Os resultados antigos desse arquivo NÃO são aprovação da execução atual.
- [ ] Registrar também SHA-256 dos arquivos de produção não rastreados `CTRFont.java` e `CTRTouchSnapshot.java` e dos três arquivos de `tests/`; usar `Get-FileHash`. Conferir novamente ao final.

## 2. Contratos C/Java e APK — 2–5 minutos

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
python tests/run_android_touch_tests.py --cc C:/msys64/ucrt64/bin/gcc.exe *> (Join-Path $evidence 'host-tests.log')
$hostExit = $LASTEXITCODE
Push-Location android
try {
    .\gradlew.bat :app:assembleDebug --console=plain *> (Join-Path $evidence 'build.log')
    $buildExit = $LASTEXITCODE
} finally { Pop-Location }
```

Capturar o exit code imediatamente. Não reportar sucesso apenas por existir APK antigo. Se GCC/JDK não existir, registrar caminho ausente; não instalar dependências.

**Aprovação:** testes C e Java com exit 0; Gradle com exit 0 e `BUILD SUCCESSFUL`. Registrar hash do APK recém-gerado. Esses testes cobrem publicação, geração, entrada antiga, botões mantidos, concorrência e parser; usam stubs e não comprovam a engine completa nem o compositor Android.

## 3. Preparar Android e evidências — 5 minutos

- [ ] Escolher um serial que apareça como `device`; se houver vários candidatos equivalentes, perguntar qual usar. Sempre passar `-s $serial`. Esperar `getprop sys.boot_completed` retornar `1`, com consultas limitadas e sem esperas maiores que 30 segundos.
- [ ] Sem dispositivo: listar AVDs com o executável do SDK. Pode iniciar um AVD existente usando `Start-Process -WindowStyle Hidden`; não criar, apagar ou aplicar wipe. Se nenhum servir, encerrar a parte visual como NÃO VERIFICADO.
- [ ] Guardar modelo, Android, resolução, densidade e proporção. Com build aprovado: `& $adb -s $serial install -r android/app/build/outputs/apk/debug/app-debug.apk`. Se falhar por assinatura, não desinstalar.
- [ ] Guardar o valor original de `getprop log.tag.CTR-UI`, ativar `setprop log.tag.CTR-UI DEBUG` e abrir o launcher com `& $adb -s $serial shell am start -W -n com.ctrnative/.CTRNativeLauncherActivity`. Usar disco já importado. Se faltar disco, não inventar caminho: registrar bloqueio dos fluxos do jogo.

### Como capturar sem perder tempo

Usar ferramenta disponível de controle Android ou `adb shell input tap/swipe/keyevent`. Antes de tocar, observar uma captura atual; nunca copiar coordenadas de outra resolução. Para menus em Canvas, `uiautomator` pode não expor botões.

PNG: `adb -s SERIAL shell screencap -p /sdcard/ctr-v01.png`, depois `adb -s SERIAL pull /sdcard/ctr-v01.png CAMINHO_LOCAL`. Evitar redirecionar bytes de `exec-out` pelo PowerShell. Inspecionar o PNG pela ferramenta de imagem.

Vídeo: iniciar `adb -s SERIAL shell screenrecord --time-limit 45 /sdcard/ctr-v01.mp4` em processo separado com `Start-Process -WindowStyle Hidden`, executar o fluxo durante a gravação e depois fazer `pull`. Preservar o arquivo no aparelho; não fazer limpeza recursiva. Não supor que screenrecord grava a 60/120 FPS: registrar metadados reais se houver ferramenta para lê-los.

Após cada cenário: `& $adb -s $serial logcat -d -v monotonic -s 'CTR-UI:V' 'SDL:V' *> (Join-Path $evidence 'v01-logcat.txt')`. Não limpar logcat. Anotar início/fim do cenário para distinguir registros antigos. Guardar um screenshot de antes/depois e do defeito quando houver.

Sem ferramenta capaz de extrair/assistir vídeo, guardar o MP4 para revisão posterior e marcar análise de flicker como NÃO VERIFICADO. Screenshots estáticos não aprovam transições.

## 4. Matriz de execução — 25–40 minutos

Rodar cada caminho uma vez. Repetir três vezes apenas transições suspeitas, incluindo um retorno rápido ou toque durante a saída. Registrar cada caminho percorrido e o que ficou inacessível; não aprovar uma linha inteira se testou só parte.

| ID | Caminho | Critério observável |
|---|---|---|
| V01 | Launcher → loading → menu; submenu → voltar | Loading sai por prontidão; sem logo/menu nativo duplicado ou texto do destino anterior. |
| V02 | Arcade → personagem → pista; voltar e avançar; copa se acessível | Retrato seleciona sem avançar; confirmação separada funciona. Títulos, nomes, cartões e hitboxes correspondem à imagem; copa mantém textos no primeiro frame visível. |
| V03 | Pista → loading → corrida → pausa → opções/submenu → retomar | Toque antigo não aciona destino; botões mantidos são liberados; sem controles presos, overlay anterior ou diálogo duplicado. Não pressupor que uma única execução prova todos os casos. |
| V04 | Aventura → campanha existente → garagem/dicas, quando acessíveis | Nome/atributos aparecem; listas e detalhe das dicas correspondem ao toque. Não alterar ou apagar campanha para acessar uma tela. |
| V05 | Repetir seleção em tela inteira e 4:3; configurações/editor; gamepad se disponível | Testar centro e bordas de pelo menos três alvos ligados ao conteúdo nativo. Conferir Luckiest Guy, cortes, nomes longos existentes, sobreposição e alinhamento sob letterboxing. Sem gamepad: NÃO VERIFICADO para gamepad. |

Para V05, guardar/restaurar a preferência original de proporção pela própria UI. Não alterar globalmente resolução/densidade do telefone. Usar outros AVDs já existentes para notch e proporções adicionais, se disponíveis; não inventar cobertura de 16:9, 18:9, 19.5:9 e 20:9 a partir de uma única tela.

### Leitura mínima dos logs

Correlacionar `publish → receive → apply → draw` por geração/frame. Contabilizar gerações publicadas observadas sem aplicação/desenho, mas não chamá-las automaticamente de bug: agrupamento pode pular estados intermediários. Investigar somente quando coincidir com falha visual ou de entrada.

`producedNs` nativo e `uptimeMillis` Java têm relógios distintos: NÃO subtrair entre plataformas. `draw` prova execução de Canvas, não apresentação física. Uma geração não precisa mudar em cada frame; frame pode mudar com geração estável.

Performance: se houver ferramenta que capture frames da superfície SDL, coletar 30 segundos de corrida com condições registradas e guardar dados brutos. `dumpsys gfxinfo` isolado não mede toda a renderização SDL. Sem ferramenta adequada, marcar FPS/latência/consumo como NÃO MEDIDOS; não instalar profiler nem inferir ganho por leitura de código.

## 5. Encerramento e pacote de revisão — 3 minutos

- [ ] Restaurar proporção e valor original de `log.tag.CTR-UI`, mesmo se a validação falhar. Não desinstalar nem limpar dados.
- [ ] Salvar `status-after.txt`, `worktree-after.patch` e repetir hashes. Confirmar que o patch e hashes de fontes/testes não mudaram; se mudaram inesperadamente, reportar sem reverter.
- [ ] Criar `$evidence\REPORT.md`, até 80 linhas, e `$evidence\INDEX.md`, somente lista dos arquivos e sua finalidade. Não colar logs completos na conversa.

Formato do REPORT:

```markdown
# Validação CTR Mobile
Estado: HEAD, hash APK, caminho absoluto das evidências
Ambiente: modelo/serial, Android, resolução/densidade, físico ou emulador
Gates: diff-check / host C / host Java / build — exit code e resultado
| ID/subcaso | PASSOU/FALHOU/NÃO VERIFICADO | Evidência e timestamp | Limite |
| ... | ... | ... | ... |

## Achados (máximo 5)
F01 — reprodução exata; esperado; observado; arquivo de evidência e tempo do vídeo.
Separar observação de hipótese de causa. Citar arquivo/linha somente se investigado.

## Handoff ao modelo principal
Até 3 pontos que exigem revisão; indicar MP4/PNG/log e intervalo exato.
Cobertura ausente; configurações restauradas; fontes preservadas.
```

**Pare ao terminar.** Não corrija bugs, não amplie a auditoria e não atualize a estimativa de progresso sem evidência. Na resposta, informar resultado dos gates, até três achados e link absoluto do REPORT.

## Revisão posterior pelo modelo principal

O modelo principal deve ler REPORT/INDEX antes de abrir código. Inspecionar diretamente as imagens e trechos de vídeo indicados, quando suportados, e os logs de até três achados. Priorizar: flicker na saída/retorno do menu, alinhamento 4:3 e entrada antiga/primeiro frame de copa/garagem. Conferir somente o código relacionado e repetir um cenário decisivo se necessário. Não aceitar PASSOU do Luna como prova sem conferir a evidência correspondente; não tratar hipótese como causa confirmada.
