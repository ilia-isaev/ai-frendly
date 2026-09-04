# uSockets 0.8.8 — adotar `uv_loop_t` externo (shared loop)

Componentes: module `usurl` (`src/usurl.cxx`) + uSockets 0.8.8
(`../../third_party/uSockets-0.8.8`).

## API real de `us_create_loop`
- Assinatura: `us_create_loop(hint, wakeup_cb, pre_cb, post_cb, ext_size)`
  - `src/eventing/libuv.c:134`.
- `hint` = `uv_loop_t*` externo:
  - `loop->uv_loop = hint ? hint : uv_loop_new();` — `src/eventing/libuv.c:137`.
  - `loop->is_default = hint != 0;` — `src/eventing/libuv.c:138`.
- Os callbacks são guardados em `loop->data` — `src/loop.c:36-37`.

## GOTCHA 1 — pre_cb/post_cb/wakeup_cb NÃO podem ser NULL
- Sintoma: segfault (SIGSEGV) no primeiro `uv_run`.
- Causa: o prepare-handle do uSockets é criado e fica **sempre ativo**; a cada
  iteração de `uv_run` ele chama `us_internal_loop_pre` que faz
  `loop->data.pre_cb(loop)` — `src/loop.c:189` (e `post_cb` em `src/loop.c:194`).
  Com NULL → chamada de função-pointer nulo → SIGSEGV.
- O prepare/check handle é `uv_unref`'d (`src/eventing/libuv.c:143,148`) — não mantém o
  loop vivo — MAS continua disparando a cada iteração.
- Correção: passar stubs no-op para os três slots.
  - `static void usurl_noop_cb(struct us_loop_t*) {}` — `src/usurl.cxx:688`.
  - `us_create_loop(loop, usurl_noop_cb, usurl_noop_cb, usurl_noop_cb, 0)`
    — `src/usurl.cxx:695`.

## GOTCHA 2 — `us_loop_free` em loop adotado NÃO deleta nem roda o `uv_loop_t`
- `us_loop_free` só faz `uv_run`/`uv_loop_delete` quando `!is_default`:
  - `if (!loop->is_default) { uv_run(...); uv_loop_delete(...); }`
    — `src/eventing/libuv.c:180-182,192`.
- Consequência: com `hint` (loop externo), `us_loop_free` **não** dispara as
  close-callbacks pendentes (uv_close é assíncrono) e **não** deleta o loop.
- Correção no `~Client` (ordem):
  1. fechar contextos + destruir records (`us_socket_context_close` / `us_socket_close`);
  2. `us_loop_free(usloop)` — `src/usurl.cxx:723` (só libera estado interno);
  3. o CONSUMIDOR deve rodar o loop mais uma vez (`uv_run(loop, UV_RUN_DEFAULT)`) para
     disparar as close-callbacks pendentes;
  4. depois `uv_loop_delete(loop)`.
- No teste: `uv_run(loop, UV_RUN_DEFAULT)` antes de `server.shutdown_server()` e
  `uv_loop_delete(loop)` — `tests/test_main.cpp:207-209`.

## Regra de posse do loop (contrato com qpid-proton)
- O `uv_loop_t` pertence ao proactor do proton (donor). uSockets só ADOTA via `hint`.
- O `usurl::Client` NUNCA chama `uv_loop_delete` nem `uv_run` "para sempre" sozinho:
  - em produção, o `pn_proactor_wait` (uv_run) do proton dirige o loop e dispara os
    watchers do uSockets (prepare/check + socket + timers).
  - `us_loop_free` no `~Client` é seguro porque `is_default==1`.
  - o dono decide quando `uv_loop_delete`.
