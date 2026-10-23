# Contrato de alterações

Use este contrato em mudanças de código, configuração, interface ou documentação. Ele complementa o `AGENTS.md` e os guias especializados.

## 1. Escopo e preservação

- Confirme o comportamento atual no código e na documentação mais próxima da área afetada.
- Faça a menor mudança que conclua o pedido; preserve fluxos, preferências, compatibilidade e alterações preexistentes.
- Não trate propostas antigas, capturas ou hipóteses de QA como prova do estado atual.
- Se o checkout não tiver metadados Git, não alegue que a árvore está limpa nem dependa de `git status` para preservar trabalho.

## 2. Dono da mudança

- Android UI, lifecycle e preferências: camada Android proprietária.
- Regras, navegação, estados e renderização do jogo: engine C/C++.
- Integração entre as duas: interface explícita via JNI, snapshots e eventos de entrada.
- Alterações de toque devem alinhar desenho, hit-test, liberação de input e estado apresentado.
- Evite editar `externals/` sem necessidade demonstrável; não inclua imagem de disco, asset protegido, APK, logs pessoais, dados de dispositivo ou segredos.

## 3. Compatibilidade

- O alvo deste projeto é Android; Gradle chama CMake para o alvo Android. Não criar nem documentar build desktop como alvo do projeto.
- `CTR_NATIVE` não é sinônimo de Android. Para código exclusivo de Android, use `__ANDROID__`.
- Preserve contratos persistidos. Se for necessário mudar chave, formato ou significado, planeje leitura dos dados existentes e migração compatível.
- Não adicione dependência sem necessidade concreta.

## 4. Documentação

Atualize o documento dono quando mudar um fluxo, estado, interface pública entre camadas, preferência persistida, requisito de build ou procedimento operacional. Mantenha `PRD.md`, `ARCHITECTURE.md`, `DESIGN.md` e este contrato como referências concisas; detalhe de subsistema continua no guia especializado.

## 5. Evidência proporcional

Escolha a validação mais específica para o risco e relate-a por categoria:

1. **Inspeção estática:** arquivos/contratos verificados e diff revisado.
2. **Testes/build:** comandos executados e resultado; não afirme execuções que não ocorreram.
3. **Runtime/visual:** aparelho ou emulador, fluxo, configuração e evidência observada.

Uma compilação não comprova interação, acessibilidade, proporção de tela, desempenho ou compatibilidade física. Para mudanças puramente documentais, verifique links relativos, consistência e diffs; não execute build sem motivo.

## 6. Conclusão

Ao concluir, registre o que mudou, validação efetivamente realizada e pendências que afetam o aceite. Não converta limitações desconhecidas em requisitos nem hipóteses em causas confirmadas.
