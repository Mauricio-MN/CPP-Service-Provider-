# Plano de melhoria

Este documento registra os planos solicitados e seus resultados. A ordem e intencional: estabelecer e validar o contrato antes de mudar semantica ou otimizar.

## Fase 1: qualidade sem mudar o comportamento pretendido

Objetivo: tornar a implementacao compilavel e confiavel, mantendo as assinaturas publicas sempre que forem utilizaveis e preservando o modelo de instancias agrupadas por contexto.

- [x] Corrigir pontuacao, clareza e navegacao do README; referenciar `docs/API.md`.
- [x] Documentar o contrato publico, requisitos, fabrica, ciclo de vida e limitacoes em `docs/API.md`.
- [x] Corrigir erros de compilacao, construcao/destruicao do thread e sincronizacao de acesso aos mapas.
- [x] Preservar a API publica existente, mantendo a assinatura historica da fabrica e oferecendo uma sobrecarga `const`.
- [x] Criar testes executaveis para contexto, fabrica, registro, despacho, retorno e remocao.
- [x] Validar com GCC C++17, `-Wall -Wextra -Wpedantic`; testes passaram.

Criterios de aceite: o header compila sozinho, os testes passam e os clientes continuam observando agrupamento por nome, despacho em ordem de registro, fabrica virtual e remocao explicita/automatica.

## Fase 2: comportamento, concorrencia e desempenho

Objetivo: reduzir riscos de deadlock e contenção sem tornar o comportamento imprevisivel, avaliando mudancas em praticidade e performance.

- [x] Adquirir sob mutex um snapshot imutavel compartilhado e executar callbacks fora do mutex; reentrada e remocao concorrente estao cobertas por teste.
- [x] Fazer uma unica busca por contexto em cada despacho e reservar capacidade do vetor de resultados.
- [x] Definir e testar erros claros para contexto nulo e contexto sem instancias.
- [x] Medir despacho em tamanhos diferentes antes de introduzir ajuste adaptativo.
- [x] Avaliar autotuning em runtime; nao foi adicionado porque o snapshot imutavel e o unico caminho testado que combina leitura barata, reentrada e remocao segura. Nao ha uma segunda estrategia segura com ganho demonstrado para selecionar.
- [x] Executar testes funcionais, de reentrada, de concorrencia basica e um benchmark comparativo.

Criterios de aceite: callbacks podem reentrar sem deadlock; remocao concorrente nao invalida memoria usada por uma chamada em andamento; a estrategia escolhida tem justificativa mensuravel; contratos novos estao documentados.

## Execucao e resultados

A execucao de cada fase sera registrada aqui depois dos testes. Nenhuma melhoria adaptativa sera adicionada sem evidencias de que supera uma estrategia unica em cargas representativas.

### Resultados desta execucao

- Fase 1: concluida. O header e os testes compilam em C++17 com `-Wall -Wextra -Wpedantic`; os testes passaram.
- Fase 2: concluida. Callbacks rodam fora do mutex; snapshots imutaveis tornam a aquisicao do grupo O(1), enquanto registro de instancias fica O(n).
- Benchmark local apos snapshots imutaveis: aproximadamente 33 ns por dispatch com 1 instancia, 37 ns com 8, 135 ns com 64 e 448 ns com 256. O loop direto e um piso teorico e nao uma comparacao equivalente de API. Os numeros variam por maquina.
- Decisao: sem autotuning em runtime. Uma estrategia dinamica exigiria outro caminho seguro, telemetria e um limiar estavel; as medicoes disponiveis nao demonstram que essa complexidade compense. A arquitetura foi otimizada estaticamente para despachos frequentes e registros menos frequentes.

## Fase 3: concorrencia previsivel sem perder vazao

Este plano responde aos riscos descritos em `docs/API.md`. Ele ainda nao foi executado. A regra de compatibilidade e manter o comportamento padrao: registros protegidos, snapshots para despacho, callbacks fora do mutex, ordem de registro preservada e coleta eventual de contextos. Novas politicas de serializacao ou destruicao devem ser opt-in.

### 3.1 Fixar o contrato e os testes

- [x] Documentar que o provider sincroniza seus mapas, mas nao serializa chamadas aos metodos de `T`; implementacoes mutaveis compartilhadas devem sincronizar seu proprio estado.
- [x] Documentar que `erase` e `clear` removem registros, enquanto snapshots de chamadas ja iniciadas podem terminar; handles externos mantem objetos vivos.
- [x] Documentar que o provider precisa sobreviver a todas as chamadas e que sua destruicao nao pode concorrer com operacoes publicas.
- [x] Expandir testes concorrentes para `registerContext`, `findContext`, `addInstance`, despacho, `erase`, `clear` e coletor; inclui chamadas simultaneas na mesma instancia compartilhada com estado atomico.
- [ ] Adicionar uma execucao de ThreadSanitizer em CI/ambiente compativel. O runtime atual do container falhou ao iniciar o TSan, portanto os testes locais nao substituem essa verificacao.

### 3.2 Reduzir pausas do coletor

- [x] Adicionar benchmark com 50 mil contextos, quatro threads de busca e intervalo configuravel; coletar p50/p95/p99/max de `findContext`.
- [x] Implementar varredura rotativa em lotes de quatro entradas, liberando o mutex e pausando 50 microssegundos entre lotes; servicos removidos sao destruidos fora do lock.
- [x] Testar progresso por varios lotes: 300 contextos sem handles sao removidos dentro do limite do teste.
- [ ] Repetir a comparacao em CI e com cargas de leitura/escrita variadas; os p95/p99 locais variaram bastante, portanto ainda nao ha evidencia estatistica de uma melhora geral da cauda.

### 3.3 Preservar concorrencia por padrao; oferecer serializacao opt-in

- [x] Manter callbacks fora de qualquer mutex global; testes cobrem reentrada e despacho paralelo.
- [ ] Prototipar serializacao opt-in por instancia somente quando houver um servico concreto que precise dela; hoje o comportamento padrao continua concorrente.
- [ ] Medir e testar qualquer politica opt-in antes de inclui-la.

### 3.4 Tratar destruicao com afinidade de thread

- [x] Definir e documentar que, no modo atual, o destrutor de `T` roda na thread que libera a ultima referencia; com coleta automatica, essa thread pode ser a do coletor.
- [ ] Criar um exemplo de servico com afinidade de thread para demonstrar o limite e o uso de `clear()`/`erase()` em um ponto controlado pelo chamador.
- [ ] Somente se esse caso for requisito real, projetar um executor/deleter opt-in que encaminhe destruicao para a thread proprietaria, incluindo regra para o executor sobreviver aos servicos.
- [x] Validar que referencias externas continuam validas apos `clear()`.

### Criterios de aceite da Fase 3

- [x] Nenhuma chamada de usuario ou destrutor de servico executa sob o mutex global.
- [x] O despacho em modo padrao permanece concorrente e mantem a ordem de registro por chamada.
- [ ] Testes de estresse e TSan passam em ambiente compativel; politicas opt-in ainda nao foram adicionadas.
- [ ] Benchmarks repetidos mostram reducao mensuravel da cauda sem regressao relevante no despacho comum.
- [x] A coleta continua eventual e limitada pelo lote; sem handles, o teste multi-lote confirma progresso. `garbageWait` nao e prazo maximo de coleta.

### Resultados parciais da Fase 3

- O coletor agora processa no maximo quatro entradas por trecho exclusivo do mutex e faz uma pausa de 50 microssegundos entre lotes. Esse limite reduz o trabalho continuo por aquisicao do lock, mas nao promete um limite temporal rigido em qualquer escalonador.
- Com 50 mil contextos, quatro threads e coleta a cada 1 ms, uma execucao anterior a mudanca mediu p50=240 ns, p95=3046 ns, p99=3296 ns e max=3.76 ms. Cinco execucoes com lotes mediram p50 entre 130-191 ns, p95 entre 360-2625 ns, p99 entre 2244-26430 ns e max entre 0.56-2.42 ms. A variabilidade impede afirmar melhora geral do p95/p99; o benchmark precisa de repeticoes em ambiente controlado.
- Testes passaram para 300 contextos coletados em varios lotes, chamadas paralelas a uma implementacao com `atomic`, remocao durante callback e ownership externo apos `clear()`.
- Serializacao por servico e executor de destruicao ficam adiados: nao ha ainda um caso de uso concreto que justifique mudar ou ampliar essas politicas.

## Fase 4: reduzir custo em aplicacoes single-thread

Objetivo: permitir que aplicacoes sem acesso concorrente removam o custo de mutexes e da thread de coleta sem alterar o modo multithread padrao.

- [x] Adicionar `SERVICE_PROVIDER_SINGLE_THREAD`, com mutex no-op e sem thread de coleta.
- [x] Preservar snapshots imutaveis e lifetime de instancias durante chamadas em andamento.
- [x] Adicionar `collectGarbage()` para limpeza explicita de contextos sem handles externos.
- [x] Testar registro, despacho, coleta manual e compilacao sem `-pthread`.
- [x] Documentar que a macro precisa ser uniforme em todas as unidades de traducao e que essa configuracao nao e thread-safe.
- [x] Medir o benchmark de despacho nos modos single-thread e multithread.

### Resultados

- O teste dedicado single-thread e o exemplo compilam e executam sem `-pthread`.
- As execucoes locais variaram bastante e nao estabeleceram um ganho estavel de latencia por despacho. O beneficio confirmado e estrutural: sem thread de coleta e sem mutexes reais; novas comparacoes precisam de ambiente controlado.
- O acesso interno ao identificador do contexto agora evita criar uma copia da string nos caminhos de registro, despacho e remocao; as assinaturas publicas existentes foram mantidas.
- O modo multithread continua sendo o padrao, com coletor automatico e mutexes reais.
