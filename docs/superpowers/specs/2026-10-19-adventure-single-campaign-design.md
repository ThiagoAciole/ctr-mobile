# Modo Aventura com campanha única

## Objetivo

Trocar a seleção de vários espaços de save por uma campanha única no Android, com continuação direta e gerenciamento simples. A escolha do piloto acontece somente na criação da campanha.

## Decisões aprovadas

- Não migrar saves dos quatro espaços existentes.
- Preservar os dados legados sem sobrescrevê-los e sem mostrá-los no novo fluxo.
- Guardar a nova campanha em um arquivo Android separado, com um único registro de progresso.
- Não oferecer troca de piloto depois que a campanha for criada.

## Fluxo

### Nenhuma campanha

1. Ao selecionar Modo Aventura, abrir a criação de campanha.
2. Pedir o nome.
3. Abrir a seleção de piloto uma única vez.
4. Criar o progresso inicial e gravar a campanha antes de iniciar a aventura.
5. Se a gravação falhar, informar o erro e não iniciar uma campanha que não possa ser retomada.

### Campanha existente

O menu de Aventura apresenta:

- **Continuar:** carrega a campanha única e retoma o hub salvo.
- **Campanha:** abre as ações **Editar nome**, **Excluir campanha** e **Voltar**.

Editar nome altera apenas o nome salvo. Excluir pede confirmação, remove apenas o arquivo da campanha nova e retorna ao fluxo de criação. Nenhuma dessas ações troca o piloto.

## Persistência e compatibilidade

- O registro novo contém o progresso de Aventura usado pelo motor, incluindo nome, piloto, recompensas e hub atual.
- O arquivo usa versão e gravação atômica, reaproveitando as rotinas nativas disponíveis quando compatíveis.
- O save de Aventura atualiza sempre esse registro único nos mesmos pontos em que o fluxo atual persiste o progresso.
- Os quatro slots nativos antigos permanecem intocados. A ausência, corrupção ou incompatibilidade do arquivo novo não deve sobrescrevê-los nem convertê-los automaticamente.
- A exclusão da campanha nova não apaga o cartão nativo nem outros dados do jogo.

## Integração

- Adaptar a entrada Android do menu de Aventura e seu toque para distinguir **Continuar** de **Campanha**.
- Reutilizar a seleção de nome e piloto na criação.
- Criar operações nativas de ler, gravar, renomear e excluir o registro único, mantendo erros explícitos entre motor e interface.
- Manter o fluxo de Aventura de outras plataformas e os menus de save/load legados fora desta mudança.

## Critérios de aceite

1. Sem campanha, Modo Aventura solicita nome e piloto e cria uma campanha retomável.
2. Com campanha, aparecem **Continuar** e **Campanha**; continuar abre o hub salvo.
3. Gerenciamento permite renomear e excluir com confirmação, sem trocar o piloto.
4. Falhas de leitura/gravação são apresentadas e não destroem o arquivo anterior nem os saves legados.
5. Os saves antigos continuam byte a byte intactos durante criação, continuação, edição e exclusão da campanha nova.

## Fora de escopo

- Migrar ou importar campanhas dos quatro slots antigos.
- Suportar várias campanhas ou perfis de usuário.
- Trocar o piloto de uma campanha existente.
- Alterar a persistência de Arcade, recordes ou opções globais.
