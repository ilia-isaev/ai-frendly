# Etapa 2-3: Arquitetura e Plano de Implementação

## Arquitetura

```
┌─────────────────────────────────────────────────────────┐
│                    Aplicação (qpid-proton)              │
│  - Cria uv_loop_t                                     │
│  - Chama Client::get<T>(url, payload)                 │
│  - Consulta Client::finished<T>() / remove_finished<T> │
│  - Spina uv_run(UV_RUN_ONCE)                          │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│              libusocketsurl (module usurl, sem ns)     │
│                                                       │
│  Client                                                │
│  ├── us_loop_t*        (us_create_loop com hint)      │
│  ├── context_tcp       (us_create_socket_context 0)   │
│  ├── context_ssl       (us_create_socket_context 1)   │
│  ├── std::list<RecordBase*> records                   │
│  │                                                       │
│  │  RecordBase (vtable)                                │
│  │  ├── Record<int>                                     │
│  │  ├── Record<std::string>                            │
│  │  └── Record<MyStruct>                               │
│  │       └── Finished<T> { url, status, body, error,  │
│  │                         payload }                   │
│  │                                                    │
│  SocketState { Client* client; RecordBase* record;     │
│                 int ssl; }  (POD, ext de us_socket)     │
│  (retry de write fica em RecordBase::request, não aqui) │
│                                                       │
│  Callbacks (14 funções: 7 eventos × 2 contextos)      │
│  ├── on_open_tcp/ssl                                 │
│  ├── on_data_tcp/ssl                                 │
│  ├── on_end_tcp/ssl                                  │
│  ├── on_writable_tcp/ssl  (era "on_drain")         │
│  ├── on_timeout_tcp/ssl                              │
│  ├── on_close_tcp/ssl                                │
│  └── on_connect_error_tcp/ssl                         │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│              uSockets 0.8.8 (libuSockets.a)            │
│  - Socket context (TCP/TLS)                            │
│  - Poll dispatch via libuv                             │
│  - SSL handshake implícito (BIOs compartilhadas)       │
│  - Shared read buffer (512 KB per loop)                │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│              libuv 1.52 (libuv.a/so)                   │
│  - Event loop (externa, pertence à aplicação)         │
│  - uv_run() controlada pela aplicação                  │
└─────────────────────────────────────────────────────────┘
```

## API Pública (module `usurl` — sem namespace; module já escopa as entities)

> Ajustado à implementação real (etapa 4). Divergências do plano original:
> sem namespace, sem PIMPL, `Finished<T>` ganha `handle`, `finished<T>()` devolve
> cópias (não ponteiros), `remove_finished(const void* handle)` (não-template).

```cpp
export module usurl;

export template <typename T>
struct Finished {
    std::string url;
    int status;            // HTTP status code
    std::string body;      // response body
    std::string error;     // non-empty if failed
    T payload;
    const void* handle;    // ponteiro para o Finished<T> no record (estável)
};

export class Client {
public:
    explicit Client(uv_loop_t* loop);
    ~Client();

    // Async: retorna imediatamente; request despachado pro event loop
    template <typename T>
    void get(const std::string& url, const T& payload);

    // Cópias dos Finished<T> finalizados (type-erasure via type_tag<T>::id())
    template <typename T>
    std::vector<Finished<T>> finished() const;

    // Destroi o record dono do handle e remove da lista
    void remove_finished(const void* handle);

private:
    // Membros diretos (sem PIMPL)
    struct us_loop_t* usloop;
    struct us_socket_context_t* tcp_ctx;
    struct us_socket_context_t* ssl_ctx;
    std::list<RecordBase*> records;
    uv_loop_t* loop_;
};
```

## Decisões de Design

| Decisão | Justificativa |
|---------|---------------|
| `Connection: close` sempre | Simplifica: 1 socket = 1 record, sem keep-alive |
| RecordBase vtable + Record<T> | type-erasure para lista única no Client |
| SocketState POD com memset | Evita constructor issues em C callbacks |
| 2 contextos (TCP + SSL) | Separar callbacks, evitar circular dependency |
| `us_socket_context_on_timeout` | Timeout por contexto (callback on_timeout) |
| Parse inline (on_data) | Sem buffer extra; parser state em RecordBase |
| Body extraído em finalize | Memória eficientemente: só guarda o necessário |

## Plano de Implementação

### 1. `CMakeLists.txt` (root)
- `cmake_minimum_required(VERSION 3.28)`
- `project(libusocketsurl LANGUAGES CXX)`
- C++23
- `add_library(usurl STATIC src/usurl.cxx)`
- `FILE_SET CXX_MODULES BASE_DIRS src FILES src/usurl.cxx`
- `find_library` para `uSockets`, `uv`, `ssl`, `crypto`
- `add_executable(usurl_tests tests/test_main.cpp)`
- Link: `usurl`, `uSockets`, `uv`, `ssl`, `crypto`

### 2. `src/usurl.cxx` (module file)
Estrutura:
```
module;                          // global fragment
#include <libusockets.h>
#include <uv.h>
#include <openssl/ssl.h>
#include <string>
#include <vector>
#include <list>
#include <cstring>
#include <cstdio>
#include <memory>

export module libusocketsurl;   // module interface

export namespace usurl {
    // Finished<T>
    // Client class (public API)
}

// module implementation (internal)
namespace usurl {
    // RecordBase, Record<T>
    // SocketState
    // parse_url, build_request, parse_progress, decode_chunked
    // Callbacks (14 funções)
    // Client::get<T>, finished<T>, remove_finished<T>
}
```

### 3. `tests/test_main.cpp`
- Servidor TCP local em thread separada (accept → HTTP 200 + Content-Length + close)
- Testes:
  - GET válido (HTTP, porta 8080 local)
  - URL inválida (sem scheme)
  - Connection refused (porta não-bindada)
  - `remove_finished<T>()` limpa memória
- Spina `uv_run(loop, UV_RUN_ONCE)` até finished

### 4. Build & Test
- `cmake -B build -G Ninja`
- `cmake --build build`
- `./build/usurl_tests`

## Riscos / Mitigações

| Risco | Mitigação |
|-------|-----------|
| Shared buffer uSockets (512KB) sobrescreve entre callbacks | Copiar dados imediatamente em on_data |
| DNS blocking (getaddrinfo) | Aceitável: design single-thread |
| SSL handshake failure sem callback | on_error não existe; detectar via on_close com status 0 |
| Memory leak em Records | remove_finished() + destructor Client limpa restantes |
