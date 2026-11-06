# CPP Service Provider

Biblioteca C++ header-only para organizar instâncias de serviços por contexto.

## O que faz

`ServiceProvider<T>` combina um registro de serviços, uma fábrica selecionada pelo tipo solicitante e despacho multicast. O código cliente depende da interface `T`; uma chamada pode alcançar todas as instâncias daquele contexto, em ordem de registro. Contextos diferentes mantêm grupos independentes, e `callMethodGet` reúne os resultados de cada instância.

O padrão está mais próximo de um **service registry com fábrica tipada e dispatcher multicast** do que de um container DI tradicional: ele não monta um grafo de dependências para injetar um objeto por consumidor. Em vez disso, associa instâncias a contextos, pode selecionar implementações pelo tipo solicitante e encaminha chamadas ao grupo.

## Como funciona

No modo multithread padrão, as operações do registro são protegidas por mutex e cada despacho captura um snapshot imutável. Os callbacks executam fora do mutex global, e remover um contexto não invalida uma chamada já iniciada. Chamadas concorrentes podem executar ao mesmo tempo, mas uma chamada individual percorre suas instâncias sequencialmente; o provider não cria uma thread por serviço.

A sincronização protege o registro, não o estado interno de `T`. Se duas threads chamarem simultaneamente a mesma instância, o próprio serviço precisa ser seguro para isso.

## Como usar

Registre as instâncias no contexto desejado e chame o método pela interface. Cada chamada abaixo executa `update` nas duas instâncias, em ordem de registro:

```cpp
#include "serviceProv.h"
#include <memory>

struct RenderStage {
	void update(float) {}
};

int main() {
	service::ServiceProvider<RenderStage> stages;
	auto frame = stages.registerContext("frame");
	stages.addInstance(frame, std::make_shared<RenderStage>());
	stages.addInstance(frame, std::make_shared<RenderStage>());
	constexpr float deltaSeconds = 1.0f / 60.0f;
	stages.callMethod(frame, &RenderStage::update, deltaSeconds);
}
```

## Início rápido

Inclua `serviceProv.h` no projeto. O requisito mínimo é C++17. O modo padrão usa threads; em GCC/Linux, compile com `-pthread`. Aplicações estritamente single-thread podem definir `SERVICE_PROVIDER_SINGLE_THREAD` em todas as unidades de tradução para remover locks e a thread de coleta; nesse modo, chame `collectGarbage()` quando quiser coletar contextos sem handles externos.

A biblioteca não possui dependências externas. O [guia da API](docs/API.md) descreve os contextos, as fábricas, o despacho de métodos e a integração. O [`example.cpp`](example.cpp) é um roteiro comentado para quem está conhecendo o padrão.

O exemplo separa responsabilidades em `examples/service_provider/`: `iuser.h` define o contrato, `user_staff.*` e `user_normal.*` contêm as implementações, e `user_service.*` escolhe qual implementação criar. Assim, o código cliente depende da interface e da fábrica, não dos tipos concretos. A macro `SERVICE_PROVIDER(UserService, IUser)` resume a declaração do provider, mantendo a regra da fábrica em `.cpp`. Alterações apenas nos corpos `.cpp` recompilam somente a unidade alterada; mudanças em headers compartilhados recompilam seus dependentes, como é normal em C++.

Para compilá-lo com GCC:

```sh
g++ -std=c++17 -pthread -I. \
	example.cpp \
	examples/service_provider/user_service.cpp \
	examples/service_provider/user_staff.cpp \
	examples/service_provider/user_normal.cpp \
	-o /tmp/serviceProv_example
/tmp/serviceProv_example
```

Os testes ficam em [`tests/serviceProv_test.cpp`](tests/serviceProv_test.cpp). Os benchmarks de despacho e contenção ficam em [`benchmarks/dispatch_benchmark.cpp`](benchmarks/dispatch_benchmark.cpp) e [`benchmarks/collector_benchmark.cpp`](benchmarks/collector_benchmark.cpp); os comandos estão no [guia](docs/API.md).

Para executar os testes:

```sh
g++ -std=c++17 -pthread -Wall -Wextra -Wpedantic -I. tests/serviceProv_test.cpp -o /tmp/serviceProv_test
/tmp/serviceProv_test
g++ -std=c++17 -Wall -Wextra -Wpedantic -I. tests/serviceProv_single_thread_test.cpp -o /tmp/serviceProv_single_thread_test
/tmp/serviceProv_single_thread_test
```

O segundo teste usa `SERVICE_PROVIDER_SINGLE_THREAD` e compila sem `-pthread`. Essa macro precisa ser definida igualmente em todas as unidades de tradução; não use esse modo se o provider puder ser acessado concorrentemente.

## Como modificar

O exemplo separa responsabilidades em `examples/service_provider/`: `iuser.h` define a interface, `user_staff.*` e `user_normal.*` contêm implementações, e `user_service.*` implementa a fábrica. A macro `SERVICE_PROVIDER(UserService, IUser)` resume a declaração do provider, mantendo a regra de seleção no `.cpp`. O caller passado a `addInstance(context, caller)` não é retido; [`registration_modes.cpp`](examples/service_provider/registration_modes.cpp) demonstra registro por cópia e por `shared_ptr`.

Alterações nos corpos `.cpp` exigem recompilar essa unidade e relinkar a aplicação. Alterações em headers compartilhados, incluindo `serviceProv.h`, exigem recompilar as unidades que os incluem. Consulte o [guia da API](docs/API.md) para detalhes de overloads, ownership, concorrência e benchmarks.

## Estado da implementação

O projeto e a API ainda estão em desenvolvimento. Consulte o [roadmap](ROADMAP.md) para acompanhar as melhorias.

## Licença

Consulte [`LICENSE`](LICENSE).
