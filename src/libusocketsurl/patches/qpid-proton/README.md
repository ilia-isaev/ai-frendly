# Patches: qpid-proton-0.40.0

## expose-uv-loop.patch

Expor o `uv_loop_t` interno do proactor (build `PROACTOR=libuv`) para que o
uSockets (build `LIBUS_USE_LIBUV`) compartilhe o mesmo loop, via
`us_create_loop(hint)`.

- Função: `void *pn_proactor_loop(pn_proactor_t *p)` — retorna `&p->loop`
  (casting para `uv_loop_t *` no lado do app).
- Arquivos: `c/include/proton/proactor.h`, `c/src/proactor/libuv.c`
- Requisito: proton compilado com `-DPROACTOR=libuv` e libuv disponível.

### Aplicar

```sh
cd third_party/qpid-proton-0.40.0
patch -p1 -i ../../src/libusurl/patches/qpid-proton/expose-uv-loop.patch
```

### Uso no app

```c
pn_proactor_t *p = pn_proactor();
uv_loop_t *shared = (uv_loop_t *)pn_proactor_loop(p);
/* uSockets integra no mesmo loop; não chama us_loop_run/us_loop_pump */
static void us_pre_noop(struct us_loop_t *) {}
static void us_post_noop(struct us_loop_t *) {}
/* pre_cb/post_cb NÃO podem ser NULL: us_internal_loop_pre/post
   (uSockets src/loop.c:189/194) chamam loop->data.pre_cb/post_cb
   incondicionalmente a cada iteração do uv_run (segfault com NULL).
   wakeup_cb pode ser NULL (só usada via us_wakeup_loop). */
struct us_loop_t *us = us_create_loop(shared, NULL, us_pre_noop, us_post_noop, 0);
```

### Regras

- O proactor é dono do loop: não chamar `uv_loop_delete`.
- Single-thread: o loop só avança enquanto o app chama
  `pn_proactor_wait`/`pn_reactor_process` (modelo leader do proactor).
- Teardown: liberar o loop do uSockets **antes** de `pn_proactor_free`;
  o `uv_run` final do `pn_proactor_free` dispara os close callbacks do
  uSockets (`uv_walk` + `uv_safe_close` pulam handles já em CLOSING).
