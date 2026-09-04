# Etapa 1 — Análise do código fonte

Status: concluída

## Escopo analisado
- uSockets 0.8.8 (`../../third_party/uSockets-0.8.8`)
- libuv v1.52.0 (`../../third_party/libuv-v1.52.0`)
- qpid-proton 0.40.0 (`../../third_party/qpid-proton-0.40.0`) + headers/libs instalados
- Patch local: `patches/qpid-proton/expose-uv-loop.patch`

## uSockets 0.8.8
- API pública relevante (`include/libusockets.h`) — assinaturas reais da 0.8.8:
  - `us_create_loop(hint, wakeup_cb, pre_cb, post_cb, ext_size)` → `us_loop_t*`.
  - `us_create_socket_context(int ssl, us_loop_t*, int ext_size, us_socket_context_options_t options)` → `us_socket_context_t*` (4 args).
  - Socket de cliente: `us_socket_context_connect` (não existe `us_create_socket` na 0.8.8; server usaria `us_socket_context_bind`).
  - `struct us_socket_context_options_t` = **apenas** parâmetros de TLS por arquivo (`key_file_name`, `cert_file_name`, `passphrase`, `dh_params_file_name`, `ca_file_name`, `ssl_ciphers`, `ssl_prefer_low_memory_usage`). **Sem callbacks e sem timeout**: preencher o struct diretamente (não existe `us_create_socket_context_options` nem `..._options_set`).
  - Callbacks são **setters separados**: `us_socket_context_on_pre_open/on_open/on_close/on_data/on_writable/on_timeout/on_long_timeout/on_connect_error/on_end`.
  - `us_socket_write` retorna `int` (bytes escritos); write parcial dispara `on_writable`. `us_socket_close` e `us_socket_close_connecting` existem.
  - Timeout é **per-socket**: `us_socket_timeout(ssl, s, segundos)` / `us_socket_long_timeout(ssl, s, minutos)`; **não** está no options, e a impl 0.8.8 nunca o arma (caminho morto — ver etapa 3).
  - SNI: `us_socket_context_add_server_name` + `us_socket_context_on_server_name` são a variante **server-side**; não há API client-side de SNI no uSockets (usar `SSL_set_tlsext_host_name` se necessário).
- **pre_cb/post_cb/wakeup_cb NÃO podem ser NULL**: o prepare-handle fica sempre ativo e chama `loop->data.pre_cb(loop)` a cada `uv_run` → NULL causa SIGSEGV. Usar stubs no-op (ver `design_notes/usockets-shared-loop.md`).
- Callbacks de socket context (0.8.8): `us_socket_context_on_open/on_data/on_end/on_writable/on_timeout/on_close/on_connect_error` (não existe `us_request`/`on_message`).
- **Não existe `us_request`**: a API high-level de HTTP foi removida na 0.8.8. HTTP GET (request line, headers, parsing de status/headers/body, chunked, keep-alive) deve ser implementado por nós sobre a API de socket.
- **Compartilhamento do loop**: `us_create_loop(hint)` com `hint != NULL` usa o `uv_loop_t*` fornecido e marca `is_default=1` (o loop não é criado nem deletado pelo uSockets). Confirmado em `src/eventing/libuv.c`.
- `us_loop_free` sobre loop compartilhado: não deve liberar o `uv_loop_t` (is_default=1), apenas estado interno do uSockets.
- **DNS bloqueante**: `getaddrinfo` dentro de `bsd_create_connect_socket` (`src/bsd.c:712`) roda na thread que chama `us_socket_context_connect`.
- TLS client: `us_create_socket_context(is_tls=1, ...)` com options de TLS (via `create_ssl_context_from_options`, em `src/crypto/openssl.c`); SNI client não é exposto pela uSockets (ver acima).
- **TLS/verificação**: `SSL_CTX_new(TLS_method())` sem `SSL_VERIFY_PEER`/CA padrão; verificação só ocorre se `options.ca_file_name` for passado (carrega bundle + `SSL_VERIFY_PEER`). Sem `ca_file_name` → OpenSSL padrão (sem verificação de cert). Sem check de hostname pela uSockets (limitação conhecida; ver etapa 3).
- Link: `libuSockets.a` referencia símbolos `uv_*` (21) e `SSL_*` (30) → o consumidor precisa linkar `libuv` + OpenSSL.

## qpid proton 0.40.0
- Proactor (C API): `pn_proactor()`, `pn_proactor_wait(p)`, `pn_proactor_done(p, batch)`, `pn_proactor_set_timeout(p, ms)`.
- Eventos: `PN_PROACTOR_TIMEOUT` (timer de `set_timeout`), `PN_PROACTOR_INACTIVE` (nada ativo), `PN_PROACTOR_ACTIVE`.
- Loop é dirigido por `pn_proactor_wait` → `uv_run` (`c/src/proactor/libuv.c`, `leader_lead_lh`); timers do proton usam `uv_timer` no mesmo `uv_loop_t`.
- **Patch aplicado** (`patches/qpid-proton/expose-uv-loop.patch`): `void *pn_proactor_loop(pn_proactor_t*)` retorna `&p->loop` (`uv_loop_t*`). Verificado: presente no header instalado `include/proton/proactor.h:104` e exportado em `lib/libqpid-proton-proactor.so` (não está em `libqpid-proton-core.so`).
- Container C++ **não expõe** o proactor → usar C API `pn_proactor()` diretamente.

## Loop compartilhado (decisão central)
- O proactor do proton **é o dono** do `uv_loop_t` (loop único, single-thread).
- uSockets compartilha via `us_create_loop(hint=&proactor->loop)`.
- **Não** chamar `us_loop_run`/`us_loop_pump`; `pn_proactor_wait` (uv_run) dirige o loop e aciona watchers do uSockets (prepare/check + socket watchers) e timers do proton.
- `pn_proactor_set_timeout` serve de "schedule": mantém o loop vivo para processar GETs pendentes.
- Regras de teardown: ver `patches/qpid-proton/README.md` (donor do loop decide quando liberar).

## Ferramentas
- `gcc-14` (14.2.0), CMake 3.28.3 + Ninja, OpenSSL 3.0.13 (sistema), libuv v1.52.0 instalado.
- C++23 modules (`FILE_SET CXX_MODULES`), extensão `.cxx`, fontes em `./src`.
- Template de projeto: `../cxx_project_example` (CMakeLists + fluxo Ninja).

## Restrições
- **Single-thread** (exigência do projeto; DNS do uSockets é bloqueante e o proactor é de loop único).
- **GET retorna imediatamente** (futuro); o loop do proton resolve.
- uSockets não tem keep-alive/http2 built-in nesse uso → parser próprio (escopo da etapa 3).

## Pendências
