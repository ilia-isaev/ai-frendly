# Etapa 5 — Migração module → cabeçalhos convencionais

Status: concluída — build verde; `./build/usurl_tests` → `ALL TESTS PASSED (0 failure(s))`, exit 0 (20/20 PASS).

## O que foi feito
- `src/usurl.cxx` (C++23 module, GMF+interface unitário) — **removido**.
- `src/usurl.hpp` — header LEVE: só includes + declarações + templates inline.
- `src/usurl.cpp` — implementação: `#include "usurl.hpp"` + definições não-template.
- `tests/test_main.cpp`: `import usurl;` → `#include <usurl.hpp>`.
- `CMakeLists.txt`: `FILE_SET ... TYPE CXX_MODULES` → `add_library(usurl STATIC src/usurl.cpp)`; includes PUBLIC com generator expressions; `install(TARGETS)` sem `FILE_SET`; `install(EXPORT)` sem `CXX_MODULES_DIRECTORY`; `install(FILES src/usurl.hpp)` flat.

## Decisões
- Templates no header (obrigatório, não separam): `Finished<T>`, `type_tag<T>`,
  `Record<T>`, `Client::get<T>`, `Client::finished<T>`. Explicit instantiation
  foi rejeitado (fecharia a API ao conjunto instantiado).
- Sem namespace (entidades globais, fiel ao module).
- `usurl_noop_cb` continua `static` (fiel ao original; static lib não é
  arquivada em consumidor real → sem risco de clash).
- `../../include` e `src/` como `$<BUILD_INTERFACE>`; `../../lib` via
  INSTALL_INTERFACE (p/ o export). `target_compile_features(usurl INTERFACE
  cxx_std_23)` mantido (`std::erase_if` C++20; CMake >= 3.23 no consumidor).
- Reinstall p/ consumidores: `cmake --install build --prefix ../../`.

## Contexto importante aprendido
- Module unitário (GMF+interface) migra 1:1 para header+cpp: interface → hpp,
  GMF → cpp; só as definições template precisam ficar no header.
- `FILE_SET CXX_MODULES` no `install(TARGETS)` e `CXX_MODULES_DIRECTORY` no
  `install(EXPORT)` são específicos de module → removidos.
- LTO em dependência estática: `libuSockets.a` compilado com LTO quebra o link
  em clang sem plugin LTO (`sni_tree.o` = objecto LTO; `sni_find` undefined).
  Rebuild do uSockets com `WITH_LTO=0` resolveu (rebuild do usuário:
  `WITH_LIBUV=1 WITH_OPENSSL=1 WITH_LTO=0`).

## Verificação
- `cmake -B build -G Ninja` + `ninja -C build` → ok.
- `./build/usurl_tests` → `ALL TESTS PASSED (0 failure(s))`, exit 0.
