# usurl — Visão geral

## O que é
Biblioteca em C++23 (module `usurl`) para **HTTP GET assíncrono** sobre uSockets +
libuv, desenhada para rodar dentro de um `uv_loop_t` **compartilhado** e de
**thread única**, fornecido por fora (o proactor do qpid-proton). É o ponto de
integração para fazer chamadas HTTP dentro do loop do proton, sem criar nem
destruir o loop.

- Dependências: uSockets 0.8.8, libuv 1.52.0, OpenSSL 3.x (TLS).
- Build: CMake 3.28 + Ninja, C++23 modules (`FILE_SET CXX_MODULES`), fontes em `src/` (`.cxx`).
- Artefatos: static library `usurl` (o module) e executável de teste `usurl_tests`.
- Este projeto foi criado com auxílio do modelo de IA **Qwen3.8 27B**.

## Restrições de design
| Restrição | Motivo |
|---|---|
| Single-thread | Exigência do projeto; DNS do uSockets é bloqueante e o proactor do proton é de loop único. |
| Loop compartilhado | O `uv_loop_t` pertence à aplicação (proton); a lib nunca chama `uv_run`/`us_loop_run` nem cria/deleta o loop. |
| `Connection: close` sempre | Simplifica: 1 socket = 1 record; sem keep-alive nem parser de reuso. |
| Type-erasure | Um `Client` único serve payloads de qualquer tipo `T` (identificados por `type_tag<T>::id()`). |
| `get` retorna imediatamente | `get` despacha e retorna; o loop (dirigido pelo proton via `pn_proactor_wait`) resolve. |

## API pública (module `usurl`, sem namespace)
Após `import usurl;`, os tipos são globais (`Client`, `Finished<T>`).

- `explicit Client(uv_loop_t* loop)` — cria o cliente sobre o loop (delega a `us_create_loop(loop)`).
- `uv_loop_t* loop() const`.
- `template <T> void get(const std::string& url, const T& payload)` — dispara o GET.
- `template <T> std::vector<Finished<T>> finished() const` — devolve **cópias** dos `Finished<T>` finalizados.
- `void remove_finished(const void* handle)` — remove um `Finished` consumido, pelo `handle`.

`Finished<T>`: `{ std::string url; int status; std::string body; std::string error; T payload; const void* handle; }`
(`status == 0` = erro de conexão; `error` vazio = sucesso).

## Arquitetura
- `Client` (membros diretos, sem PIMPL):
  - `us_loop_t*` via `us_create_loop(loop)` com `hint` → adota o loop externo (`is_default=1`).
  - 2 socket-contexts: `tcp_ctx` (`is_tls=0`) e `ssl_ctx` (`is_tls=1`) — callbacks separados, evita dependência circular.
  - `std::list<RecordBase*> records` — type-erasure: `RecordBase` (vtable) e `Record<T>` (derive); `commit`/`destroy` como `std::function<void(RecordBase*)>`; `type_id` via `type_tag<T>::id()`.
- `SocketState { Client*; RecordBase*; int ssl; }` (POD) como `ext` de `us_create_socket` (`ext_size = sizeof(SocketState)`). O retry de write fica em `RecordBase::request`/`request_written`, não no ext.
- 14 callbacks (7 eventos × 2 contextos): `on_open`, `on_data`, `on_end`, `on_writable`, `on_timeout`, `on_close`, `on_connect_error`.
- Parser HTTP inline: request line `GET <path> HTTP/1.1` + `Host`; framing `content-length` / `chunked` / `close`-delimited.
- Estados de request: `kIdle → kConnecting → kWriting → kReading → kDone`.
- Timeout de leitura: `us_socket_timeout(ssl, s, kTimeoutSec)` (4 s), armada em `on_open`
  e no `connect_record`; `on_timeout` finaliza com `error="timeout"`, `status=0`.

```
Aplicação (qpid-proton)
  cria uv_loop_t · get<T> · finished<T>/remove_finished · spina uv_run(UV_RUN_ONCE)
        │
libusocketsurl (module usurl)
  Client ── us_loop_t* · tcp_ctx · ssl_ctx · std::list<RecordBase*>
  RecordBase(vtable) ── Record<T> ── Finished<T>
  SocketState(POD) · 14 callbacks · parser inline
        │
uSockets 0.8.8 ── libuv 1.52.0 (event loop externa, dirigida pela aplicação)
```

## Status do plano (8 passos)
Todos os 8 passos do `PROJECT_PROMT.md` estão **concluídos**. Build verde; o
executável `usurl_tests` compila; os **20 checks** passam; exit 0.

| Passo | Conteúdo | Onde está registrado | Status |
|---|---|---|---|
| 1 | Análise do código fonte | `docs/etapa1-analise-fonte.md` | concluída |
| 2 | Arquitetura | `docs/etapa2-3-arquitetura-plano.md` | concluída |
| 3 | Plano de implementação | `docs/etapa2-3-arquitetura-plano.md` | concluída |
| 4 | Implementação | `docs/etapa4-implementacao-teste.md` | concluída |
| 5 | Testes | `docs/etapa4-implementacao-teste.md` | concluída |
| 6 | Documentação | este `overview.md` + `docs/` | concluída |
| 7 | Revisão | `docs/etapa4-implementacao-teste.md` ("Divergências corrigidas") | concluída |
| 8 | Teste final | `docs/etapa4-implementacao-teste.md` (20/20 PASS, verde) | concluída |

O mapeamento reflete onde cada conteúdo está registrado: passos 2-3 compartilham
um documento (arquitetura + plano), e 4-5-8 compartilham a etapa 4 (a revisão do
passo 7 é a seção "Divergências corrigidas vs. o plano").

## Índice de documentos
- `docs/etapa1-analise-fonte.md` — análise do fonte (uSockets 0.8.8, libuv 1.52, proton 0.40).
- `docs/etapa2-3-arquitetura-plano.md` — arquitetura + plano de implementação.
- `docs/etapa4-implementacao-teste.md` — implementação, teste e divergências corrigidas.
- `design_notes/gcc-cxx23-modules.md` — gotchas de build (C++23 modules + GCC 14).
- `design_notes/usockets-shared-loop.md` — loop compartilhado (stubs pre/post/wakeup; `us_loop_free` em loop adotado).

## Build e teste
- Compilar: `ninja -C build` (target da lib = `usurl`; de teste = `usurl_tests`).
- Rodar: `./build/usurl_tests` → saída `ALL TESTS PASSED (0 failure(s))`, exit 0.
- Ligado ao CTest: `add_test(NAME usurl_tests COMMAND usurl_tests)` — `ctest --test-dir build`
  roda o executável (1 teste). Também roda direto: `./build/usurl_tests`.
