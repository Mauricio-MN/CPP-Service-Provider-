# Guia da API

Este guia descreve a API publica de `serviceProv.h` e o contrato pretendido da biblioteca. O codigo ainda esta em desenvolvimento; consulte a secao [Estado e compatibilidade](#estado-e-compatibilidade) antes de integrar.

## Instalacao e compilacao

A biblioteca e header-only. Inclua `serviceProv.h` no projeto e compile em C++17 ou superior. Por padrao, o provider usa mutexes e um coletor em thread separada; em GCC/Linux, compile e vincule com `-pthread`.

```cpp
#include "serviceProv.h"
```

Exemplo de verificacao do header:

```sh
printf '#include "serviceProv.h"\nint main() { return 0; }\n' \
  | g++ -std=c++17 -pthread -I. -x c++ -fsyntax-only -
```

## Conceitos

- **Servico (`T`)**: tipo da interface ou classe base cujas instancias serao guardadas pelo provedor.
- **Contexto (`service::Context`)**: identificador nomeado que agrupa instancias. Contextos com o mesmo nome sao destinados a compartilhar o mesmo grupo no mesmo provedor.
- **Provedor (`service::ServiceProvider<T>`)**: mantem instancias de `T` por contexto, cria instancias por meio de uma fabrica virtual e despacha chamadas de metodo para todas as instancias do contexto.
- **Fabrica**: uma classe derivada pode sobrescrever `getNewInstance` e escolher uma implementacao concreta a partir do tipo de quem solicitou o servico.

`CONTEXT` e um alias legado definido como macro para `std::shared_ptr<service::Context>`.

## Context

`service::Context` aceita um nome opcional. O construtor sem argumentos usa `"Default"`.

```cpp
service::Context defaultContext;
service::Context requestContext("request-42");
auto sharedContext = std::make_shared<service::Context>("request-42");
```

`getContext()` e `getName()` retornam o identificador do contexto. `registerContext(name)` cria o contexto quando necessario e retorna o contexto ja registrado quando o nome existe.

`findContext(name)` apenas consulta o registro. Retorna um `CONTEXT` nulo quando nao encontra o nome e nunca cria um contexto. Use essa funcao para recuperacao; reserve `registerContext` para criacao-ou-reutilizacao.

## Criacao do provedor

O construtor sem argumentos usa o intervalo padrao do coletor de contextos. O construtor que recebe um inteiro configura esse intervalo em milissegundos. O provedor deve permanecer vivo enquanto for usado; sua destruicao interrompe e aguarda o coletor. Handles `CONTEXT` mantem o contexto vivo, mas nao estendem a vida do provedor.

```cpp
service::ServiceProvider<IMyService> provider;
```

Para uma aplicacao estritamente single-thread, defina `SERVICE_PROVIDER_SINGLE_THREAD` em todas as unidades de traducao que incluem o header. Isso remove mutexes reais e a thread do coletor; o provider deixa de ser seguro para uso concorrente. No GCC, por exemplo:

```sh
g++ -std=c++17 -DSERVICE_PROVIDER_SINGLE_THREAD -I. \
  tests/serviceProv_single_thread_test.cpp -o /tmp/serviceProv_single_thread_test
/tmp/serviceProv_single_thread_test
```

A macro deve ser uniforme no programa inteiro: mistura entre unidades de traducao com configuracoes diferentes pode causar incompatibilidade de ODR/ABI. Nesse modo, o argumento de intervalo do construtor e ignorado e a coleta automatica nao e iniciada.

Para interfaces abstratas, sobrescreva a fabrica antes de solicitar instancias: a fabrica padrao tenta construir `T` diretamente.

## Adicionar instancias

As sobrecargas de `addInstance` aceitam um contexto por referencia ou por `CONTEXT`. Para escolher a fabrica por um tipo solicitante, prefira a forma parametrizada:

```cpp
auto context = provider.addInstance<StaffRequest>(requestContext);
provider.addInstance<NormalRequest>(context);
```

`StaffRequest` e `NormalRequest` sao apenas tipos usados como tags. Nenhum objeto precisa ser criado ou mantido vivo. A sobrecarga `addInstance(context, caller)` tambem existe para selecionar pela classe de um objeto que ja existe; esse objeto so e consultado durante a chamada e nao e retido pelo provedor.

As outras formas disponiveis sao:

- `addInstance(context, caller)` escolhe pela classe de um objeto existente, usado apenas durante a chamada.
- `addInstance(context, object)` copia o objeto; requer que `T` seja concreto e copiavel.
- `addInstance(context, shared_ptr)` compartilha uma instancia existente, recomendado para interfaces abstratas e para evitar copia ou slicing.

Cada chamada adiciona uma instancia ao grupo do contexto; nao e uma operacao de registro singleton. O valor retornado e o `CONTEXT` registrado, que deve ser mantido enquanto o cliente precisar manter o grupo ativo. `CONTEXT` e o macro legado para `std::shared_ptr<service::Context>`; por exemplo, uma funcao pode receber `const CONTEXT&` sem repetir o tipo longo.

Os objetos de servico sao mantidos por `std::shared_ptr` dentro do provedor. Nao ha `new` nem ponteiro bruto na API do exemplo. Solte os handles `CONTEXT` quando terminar ou chame `erase` para remover o grupo imediatamente; handles ainda vivos impedem a coleta automatica daquele contexto.

## Despachar metodos

`callMethod(context, method, args...)` chama o metodo em cada instancia do contexto. `callMethodGet(context, method, args...)` faz o mesmo e devolve os resultados em um `std::vector`, preservando a ordem de registro das instancias.

Exemplo de chamada, supondo um contexto que ja tenha instancias:

```cpp
provider.callMethod(context, &IMyService::reset, 7);
auto results = provider.callMethodGet(context, &IMyService::value);
```

O metodo deve ser invocavel em `T` com os argumentos informados. O tipo de retorno de `callMethodGet` e deduzido do metodo.

## Fabrica de implementacoes

A fabrica virtual oferece a assinatura historica e uma variante que aceita `const std::type_info&`; a variante com `const` delega para a historica por compatibilidade:

```cpp
virtual std::shared_ptr<T> getNewInstance(std::type_info& requester);
virtual std::shared_ptr<T> getNewInstance(const std::type_info& requester);
```

Uma implementacao derivada pode comparar `requester` com `typeid(CallerType)` e retornar uma instancia concreta convertivel para `std::shared_ptr<T>`. Os tipos concretos devem implementar `T` e ser destruidos corretamente por meio da interface, que deve ter destrutor virtual quando for polimorfica.

Para reduzir o boilerplate da classe derivada, `serviceProv.h` define a macro `SERVICE_PROVIDER(Nome, Interface)`. Ela declara a subclasse e o override da fabrica; a politica de selecao continua implementada pelo usuario, de preferencia no `.cpp`:

```cpp
namespace app {
SERVICE_PROVIDER(UserService, IUser);
}
```

```cpp
std::shared_ptr<app::IUser> app::UserService::getNewInstance(std::type_info& requester) {
  if (requester == typeid(StaffRequest)) {
    return std::make_shared<UserStaff>();
  }
  throw std::invalid_argument("Unsupported requester");
}
```

A macro nao cria implementacoes automaticamente; ela apenas padroniza a declaracao do provider e mantem a fabrica fora do header.

## Remover contextos

`erase(context)` e `erase(contextId)` removem do provedor as instancias e o contexto associados ao identificador. `clear()` remove todos os registros e pode ser chamado mais de uma vez. No modo multithread, o destrutor para o coletor e chama `clear()` antes de destruir o estado interno do provedor. No modo single-thread, use `collectGarbage()` para remover os contextos cujo ultimo handle externo ja foi liberado; `clear()` continua disponivel para remover tudo imediatamente.

A mesma instancia `shared_ptr<T>` pode ser registrada em mais de um contexto. Apagar um contexto libera apenas a referencia daquele grupo; a instancia continua viva enquanto outro contexto ou o chamador mantiver uma referencia. `clear()` remove todas as referencias mantidas pelo provedor, mas nao invalida `shared_ptr`s externos. Compartilhar uma instancia nao causa comportamento indefinido por si so; chamadas concorrentes a metodos mutaveis dessa mesma instancia precisam de sincronizacao implementada pelo proprio servico.

## Concorrencia e desempenho

No modo multithread, as alteracoes do registro sao protegidas por mutex. Cada despacho captura um snapshot imutavel compartilhado e executa os metodos sem manter o mutex do provedor. Assim, callbacks podem chamar novamente o provedor; se um contexto for removido durante uma chamada, as instancias do snapshot permanecem vivas ate o despacho terminar. `SERVICE_PROVIDER_SINGLE_THREAD` mantem os snapshots e essa semantica de vida durante o despacho, mas substitui os mutexes por um lock no-op e desativa o coletor automatico. Nao use essa configuracao se qualquer operacao do provider puder ocorrer concorrentemente.

Adicionar uma instancia cria um novo snapshot, com custo proporcional ao numero de instancias daquele contexto. O despacho compartilha o snapshot em tempo constante, sem copiar o vetor. O benchmark comparativo pode ser compilado e executado assim:

```sh
g++ -std=c++17 -O2 -pthread -I. benchmarks/dispatch_benchmark.cpp -o /tmp/serviceProv_dispatch_benchmark
/tmp/serviceProv_dispatch_benchmark
g++ -std=c++17 -O2 -DSERVICE_PROVIDER_SINGLE_THREAD -I. benchmarks/dispatch_benchmark.cpp -o /tmp/serviceProv_dispatch_single
/tmp/serviceProv_dispatch_single
```

Os resultados sao dependentes do hardware e servem para comparar estrategias neste ambiente, nao como garantia de latencia. As execucoes locais variaram bastante e nao estabeleceram um ganho estavel de latencia por despacho. O beneficio garantido do modo single-thread e estrutural: nao cria a thread do coletor e nao adquire mutexes reais.

O coletor examina no maximo quatro contextos por trecho exclusivo do mutex e faz uma pausa curta entre lotes. Isso reduz o tempo de trabalho continuo sob o lock, mas nao estabelece um limite de latencia real: o escalonador, a concorrencia e o tamanho total do mapa afetam cada varredura. `garbageWait` e um intervalo de espera, nao um prazo maximo para coleta. O benchmark de contenção pode comparar coleta frequente com uma coleta praticamente inativa:

```sh
g++ -std=c++17 -O2 -pthread -I. benchmarks/collector_benchmark.cpp -o /tmp/serviceProv_collector_benchmark
/tmp/serviceProv_collector_benchmark 1
/tmp/serviceProv_collector_benchmark 2147483647
```

Metodos de `T` continuam concorrentes e podem ser chamados simultaneamente. A sincronizacao do estado interno do servico e responsabilidade de `T`. O ultimo `shared_ptr` pode ser liberado pela thread do coletor, por uma thread chamadora, ou durante `clear()`/destruicao; servicos com afinidade de thread devem controlar a destruicao explicitamente. A destruicao do provider nao pode concorrer com chamadas publicas.

O plano para testar e reduzir pausas do coletor, definir garantias de concorrencia e tratar servicos com afinidade de thread esta em [`../ROADMAP.md`](../ROADMAP.md), Fase 3. Essas melhorias ainda nao foram implementadas.

## Exemplo completo

O arquivo [`example.cpp`](../example.cpp) e um roteiro comentado para acompanhar o fluxo. As partes estao separadas em `examples/service_provider/`:

- `iuser.h` declara a interface compartilhada.
- `user_staff.h/.cpp` e `user_normal.h/.cpp` declaram e implementam as classes concretas.
- `request_types.h` declara tipos vazios usados para identificar cada solicitante.
- `user_service.h/.cpp` implementa a fabrica; este e o unico componente que inclui as classes concretas.
- `example.cpp` cria o provedor, registra as duas implementacoes no mesmo contexto, demonstra o despacho e tambem usa o overload baseado em objeto caller.
- `registration_modes.cpp` demonstra as sobrecargas que copiam um objeto concreto ou compartilham um `std::shared_ptr<T>`.

Depois do primeiro despacho, o exemplo tambem mostra como passar `UserService` por referencia para uma funcao, recuperar um contexto pelo nome e criar um segundo contexto independente no mesmo provider. Ele usa `addInstance<TipoSolicitante>` para evitar instancias de tags que precisariam ficar no escopo.

Os corpos das implementacoes ficam em arquivos `.cpp`, para que alterar a logica de uma classe concreta nao exija recompilar os outros arquivos-fonte. Alteracoes em headers compartilhados ainda exigem recompilar seus dependentes.

Compile e execute na raiz do repositorio:

```sh
g++ -std=c++17 -pthread -I. \
  example.cpp \
  examples/service_provider/user_service.cpp \
  examples/service_provider/user_staff.cpp \
  examples/service_provider/user_normal.cpp \
  -o /tmp/serviceProv_example
/tmp/serviceProv_example
```

Saida esperada:

```text
Staff ID: 21
Normal ID: 42
Access through a function: 21, 42
Found named context: 21, 42
Missing context was not created by lookup.
Independent context: 14
After caller leaves scope: 13
```

O caller usado em `addInstance(context, caller)` nao e armazenado; o exemplo o destroi antes de despachar metodos e comprova que o servico continua disponivel.

Para executar o exemplo de copia versus ownership compartilhado:

```sh
g++ -std=c++17 -pthread -I. examples/service_provider/registration_modes.cpp -o /tmp/serviceProv_registration_modes
/tmp/serviceProv_registration_modes
```

Ele imprime `Provider copy: 4`, `Original object: 3` e `Shared object: 11`: a primeira sobrecarga copia o valor, enquanto a segunda compartilha a instancia fornecida.

## Estado e compatibilidade

O header original nao compila em C++17. Os planos de trabalho e o resultado de sua execucao estao registrados em [`../ROADMAP.md`](../ROADMAP.md). Ate que a compilacao e os testes passem, as descricoes acima registram o contrato pretendido, nao uma garantia da implementacao.
