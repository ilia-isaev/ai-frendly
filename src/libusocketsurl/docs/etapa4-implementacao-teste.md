# Etapa 4 — Implementação e Teste (concluída)

Status: concluída — build verde, executável `usurl_tests` compila, todos os 20 checks PASS, exit 0.

## O que foi entregue
- `usurl` — target estático C++23: header `src/usurl.hpp` + implementação `src/usurl.cpp` (antes: module `src/usurl.cxx`, removido).
- Teste (`tests/test_main.cpp`) — consumidor do header (`#include <usurl.hpp>`); `TcpServer` (HTTP local em
  thread) + `StallServer` (para timeout); **20 checks** cobrindo: content-length
  (sucesso / `remove_finished` / URL inválida / refused / múltiplos), chunked,
  close-delimited, multi-type (type-erasure) e timeout.
- `CMakeLists.txt` — Ninja, target estático (`src/usurl.cpp`), includes PUBLIC com
   generator expressions; `find_library` para `uSockets`, `uv`, `ssl`, `crypto`.
- Build: `cmake -B build -G Ninja` → `cmake --build build` → `./build/usurl_tests`.

## API pública como IMPLEMENTADA (header `usurl.hpp`, SEM namespace)
```
// src/usurl.hpp (entidades globais, sem namespace)

export template <typename T> struct Finished {
    std::string url; int status; std::string body;
    std::string error; T payload; const void* handle;  // + handle (ausente no plano)
};
export class Client {
    explicit Client(uv_loop_t* loop);  ~Client();
    uv_loop_t* loop() const;
    template <typename T> void get(const std::string& url, const T& payload);
    template <typename T> std::vector<Finished<T>> finished() const;  // COPIAS, não ponteiros
    void remove_finished(const void* handle);                          // NÃO-template, por handle
};
```
- Type-erasure: `std::list<RecordBase*>` + vtable (`Record<T>` derive de `RecordBase`),
  `commit`/`destroy` como `std::function<void(RecordBase*)>`, `type_id` via
  `type_tag<T>::id()`.
- `SocketState { Client*; RecordBase*; int ssl; }` (POD) como ext de `us_create_socket`.
  - `ext_size = sizeof(SocketState)`; NÃO guarda buffer de write (retry fica em
    `RecordBase::request` + `request_written`).
- 7 callbacks por contexto (14 no total): on_open, on_data, on_end, on_writable,
  on_timeout, on_close, on_connect_error × {tcp,ssl}.
- Parser HTTP inline: request line `GET <path> HTTP/1.1` + `Host`; framing
  content-length / chunked / close-delimited; `Connection: close`.

## Divergências corrigidas vs. o plano (etapa2-3)
- module name `libusocketsurl` → **`usurl`** (bate com o target CMake; hoje o header é `src/usurl.hpp`).
- `namespace usurl` → **sem namespace** (no header as entidades ficam globais, como no module).
- PIMPL (`struct Impl`) → **membros diretos** em `Client`.
- `Finished<T>` ganha `const void* handle`.
- `finished<T>()` devolve **cópias** `std::vector<Finished<T>>` (não ponteiros).
- `remove_finished<T>()` → `remove_finished(const void* handle)` (não-template).
- `on_drain` → `on_writable`; `SocketState` perde `wdata/wlen/woff`.

## Aprendizado importante (ver design_notes/)
- `design_notes/gcc-cxx23-modules.md` — 5 gotchas de build com g++14 C++23 modules
  (export-consistency, member-init de classe-base, nome loop, iterator nomeado,
  importer-include).
- `design_notes/usockets-shared-loop.md` — pre/post/wakeup_cb NÃO podem ser NULL
  (prepare-handle sempre ativo); `us_loop_free` em loop adotado não deleta/roda o
  `uv_loop_t` → consumidor roda `uv_run(UV_RUN_DEFAULT)` antes de `uv_loop_delete`.

## Gotcha do harness de teste (join)
- Thread de servidor bloqueada em `accept()` NÃO é acordada por `close(listen_fd)` →
  `std::thread::join()` trava para sempre.
- Correção: `SO_RCVTIMEO` (100 ms) no socket de listen (`tests/test_main.cpp:124-126`);
   no loop `run()`, `accept < 0` com `!stop` → `continue` (`:141`). Assim o worker
  observa o `stop` e sai; `join()` completa.

## Restrição de stdout buffered (diagnóstico)
- Sintoma: "sem saída" + exit 124 (timeout) embora os checks passassem.
- Causa: `check()` usava `std::cout` (bufferizado); ao travar em `join()` e ser morto
  pelo `timeout`, o buffer nunca era flushado → linhas PASS perdidas.
- Diagnóstico feito com marcadores em `stderr` (unbuffered). Marcadores removidos depois.

## Timeout (S1 + T5)
- `constexpr int kTimeoutSec = 4;` (`src/usurl.cpp:344`).
- Armada em `on_open` (`src/usurl.cpp:358`) e re-armada no `connect_record`
   (`src/usurl.cpp:582`), via `us_socket_timeout(ssl, s, kTimeoutSec)`.
- `on_timeout` (`src/usurl.cpp:399`) só age se `!finished`; finaliza com
  `error="timeout"`, `status=0`.
- T5: `StallServer` aceita a conexão e fica 8000 ms em silêncio → timeout em ~4 s.

## UAF — record destruído com o socket aberto (crash flaky no ctest)
- Sintoma: `std::out_of_range` em `std::string::substr` (`__pos = npos` sobre string
  vazia) → SIGABRT, INTERMITENTE no `ctest`, ausente no run direto.
- Causa: `on_timeout` finaliza sem fechar o socket; `destroy_record` deletava
  `rec`/`st` com o socket aberto → callback tardio via `state_of` fazia UAF.
- Correção: `destroy_record` (`src/usurl.cpp:585`) desanexa o ext-slot (p/ `nullptr`)
  e chama `us_socket_close` antes de deletar, guardado por `socket_gone`
  (`src/usurl.hpp:84`). Detalhes em `design_notes/usockets-shared-loop.md` (GOTCHA 3).

## Decoder chunked — operar sobre o body, não sobre o buffer inteiro
- `chunked_complete(buf, start)` (`src/usurl.cpp:96`) e `decode_chunked(buf, start)`
  (`src/usurl.cpp:114`) tomam `start = body_start`. Antes, começavam em `pos=0` e a
  status-line `HTTP/1.1 200 OK` era lida como "chunk size" → `ok=false` para sempre
  → o record do chunked nunca finalizava (T2 travava).
