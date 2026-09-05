# Migração module → cabeçalhos convencionais (usurl)

Componentes: `src/usurl.hpp`, `src/usurl.cpp`, `CMakeLists.txt`, `tests/test_main.cpp`.

## Layout resultante
- `src/usurl.hpp` (header leve, `#pragma once`) — includes (cstddef, functional,
  list, string, utility, vector, uv.h, libusockets.h) + declarações + templates
  inline: `Finished<T>`, `type_tag<T>`, `Record<T>`, `Client::get<T>`,
  `Client::finished<T>`. Entidades globais (sem namespace).
- `src/usurl.cpp` — `#include "usurl.hpp"` + (cctype, cstdlib, list, string,
  openssl/ssl.h). Contém: anon-namespace (helpers chunked/`state_of`),
  `parse_url`, `build_request`, 14 callbacks, `register_callbacks`,
  `kTimeoutSec` (`:344`), `usurl_noop_cb` (static, `:501`), definições
  `RecordBase::*` e `Client::*`.

## Por que templates no header
Definições de template precisam ser visíveis no TU do consumidor (instantiation
no lado do importador/consumidor). Explicit instantiation
(`template class Record<...>`) fecharia a API ao conjunto instantiado —
rejeitado (mudaria a API/ABI).

## CMake (convenional)
- `add_library(usurl STATIC src/usurl.cpp)`.
- `target_include_directories(usurl PUBLIC
    $<BUILD_INTERFACE:.../src>
    $<BUILD_INTERFACE:.../../../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)`.
  - `src/` BUILD_INTERFACE: o teste in-tree acha `<usurl.hpp>`.
  - `../../include` BUILD_INTERFACE: `<uv.h>`/`<libusockets.h>` no build
    in-tree; consumidores recebem via INSTALL_INTERFACE + packages próprios.
- `target_link_libraries(usurl PUBLIC uSockets uv ssl crypto)` (PRIVATE
  quebrava o install/export — ver `docs/correcao-packaging-usurl.md`).
- `target_compile_features(usurl INTERFACE cxx_std_23)` — exporta o standard.
- Install: `install(TARGETS usurl ...)` +
  `install(FILES src/usurl.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})`
  (flat → `#include <usurl.hpp>`).

## Gotchas
- Target não-module: `FILE_SET` no `install(TARGETS)` e `CXX_MODULES_DIRECTORY`
  no `install(EXPORT)` só existem p/ modules.
- LTO em `.a` de dependência quebra link em clang sem LTO
  (`sni_tree.o` extensão LTO + `sni_find` undefined). Rebuild do uSockets com
  `WITH_LTO=0`.
- Migração 1:1 de module unitário: interface → `.hpp`, GMF → `.cpp`;
  `import usurl;` → `#include <usurl.hpp>`; entidades globais mantidas.
