# usurl

Biblioteca em C++23 (module `usurl`) que faz **HTTP GET assíncrono** sobre
uSockets + libuv, desenhada para rodar dentro de um `uv_loop_t` **compartilhado**
(uma única thread). É o ponto de integração para o qpid-proton: o loop é
fornecido por fora (o proactor do proton) e nunca é criado nem destruído por
esta biblioteca.

Este projeto foi criado com auxílio do modelo de IA **Qwen3.8 27B**.

## O que é
- HTTP GET assíncrono, single-thread, sobre uSockets 0.8.8 + libuv 1.52.0 (+ OpenSSL 3.x para TLS).
- `get` retorna imediatamente (não bloqueia); o resultado aparece depois, via `finished<T>()`.
- Type-erasure: um único `Client` serve payloads de qualquer tipo `T` (identificados por `type_tag<T>`).
- Framing do corpo da resposta: `content-length`, `chunked` e `close`-delimited.
- Loop compartilhado: a lib **não** chama `uv_run`/`us_loop_run`; quem dirige o loop é o consumidor (ex.: `pn_proactor_wait` do qpid-proton).

## API pública
Os tipos são globais (o module se chama `usurl`; não há namespace). Após `import usurl;`,
use `Client`, `Finished<T>`, `Framing` etc. diretamente.

| Item | Assinatura | Papel |
|---|---|---|
| `Client` | `explicit Client(uv_loop_t* loop)` | Cria o cliente sobre o loop (delega a `us_create_loop(loop)`). |
| `~Client()` | (destrutor) | Fecha os socket-contexts e limpa os records. |
| `loop()` | `uv_loop_t* loop() const` | Devolve o `uv_loop_t*` compartilhado. |
| `get` | `template <T> void get(const std::string& url, const T& payload)` | Dispara um GET assíncrono; retorna imediatamente. |
| `finished` | `template <T> std::vector<Finished<T>> finished() const` | Devolve (como cópias) os `Finished<T>` finalizados do tipo `T`. |
| `remove_finished` | `void remove_finished(const void* handle)` | Remove um `Finished` já consumido, pelo `handle`. |

Estrutura de `Finished<T>`:

```
struct Finished<T> {
  std::string url;     // a URL solicitada
  int status;          // status HTTP (0 = erro de conexão)
  std::string body;    // corpo da resposta
  std::string error;   // vazio = sucesso
  T payload;           // o payload enviado no get
  const void* handle;  // identificador, para remove_finished
};
```

## Como usar

```cpp
import usurl;
#include <uv.h>

struct MyPayload { int id; };

Client cli(loop);                 // loop = uv_loop_t* (fornecida pelo proton)
cli.get<MyPayload>("http://127.0.0.1:80/hello", MyPayload{42});
// ... o loop segue sendo dirigido por pn_proactor_wait (fora da lib) ...

auto done = cli.finished<MyPayload>();
for (const auto& f : done) {
    // usar f.status, f.body, f.error, f.payload
    cli.remove_finished(f.handle);
}
```

## Build
```
cmake -GNinja -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_INSTALL_PREFIX=`pwd`/../.. -S .
ninja -v -C build/
ninja -v -C build/ install
```

## Testar
O teste é um executável (`build/usurl_tests`); ainda não está ligado ao CTest.
```
ninja -v -C build usurl_tests   # compila o executável de teste
./build/usurl_tests             # roda: imprime [PASS]/[FAIL] por check
```
Saída esperada: `ALL TESTS PASSED (0 failure(s))` e exit code 0.

## Docs
- Visão geral: `docs/overview.md`
- Etapas: `docs/etapa1-analise-fonte.md`, `docs/etapa2-3-arquitetura-plano.md`, `docs/etapa4-implementacao-teste.md`
- Notas de desenho: `design_notes/`
