# GCC / g++ 14 — C++23 modules build gotchas (usurl)

Componente: module `usurl` (fonte `src/usurl.cxx`), importado por `tests/test_main.cpp`.
Toolchain: g++ 14.2.0, flags `-fmodules-ts -std=c++2b` (CMakeLists.txt).

## Regras que causaram erro de compilação e o workaround

### 1. Forward declarations na interface DEVEM ser `export`
- Sintoma: erro no IMPORTER (`test_main.cpp`) ao usar `RecordBase`/`Client`, embora o
  module TU compilasse.
- Causa: entity usada no interface (template `finished<T>` itera `std::list<RecordBase*>`)
  precisa ter a mesma "exportedness". Uma forward decl **não-exportada** não é visível
  como exportada no importer.
- Correção: `export class Client;` e `export class RecordBase;`
  - `src/usurl.cxx:69` e `src/usurl.cxx:70`.
- Regra geral: **toda entity referenciada por uma declaration `export`ed também deve
  ser `export`ed** (export-consistency).

### 2. Membros de classe-base NÃO podem ir no member-init-list da derived
- Sintoma: `Record<T> : RecordBase` com `client(url)` / `url(url_)` no init-list → erro
  ("no declaration matches" / member de classe-base).
- Correção: `Record` define ctor **sem** init-list e atribui os membros de `RecordBase`
  no corpo (`client = c; url = url_; type_id = tid;`).
  - `src/usurl.cxx:123-148` (ctor de `Record<T>`).

### 3. Nome de método == nome de membro (`loop`) quebra depend-name lookup
- Sintoma: `loop` como método (`uv_loop_t* loop()`) colidia com o membro `loop` →
  "declaration uses bounded name" / lookup errado em templates.
- Correção: renomear o membro para `loop_` e expor `loop()` como accessor.
  - membro `loop_`: `src/usurl.cxx:195`; accessor `loop()`: `src/usurl.cxx:160`.

### 4. Iterador NOMINADO sobre container de type de module → instantânea quebrada
- Sintoma: `auto it = records.begin(); ... records.erase(it)` dentro de template
  `export`ed falha no MODULE TU: entidades std ausentes
  (`is_nothrow_convertible_v`, `std::ranges::iter_move`, `__detail::__iter_move`).
- Causa: dar NOME a um iterator força o GCC a instantiar a especialização completa do
  iterator (`std::list<X*>` onde `X` é type de module) no contexto de module, e o
  stdlib header não está no module context com as entidades C++23 que `stl_iterator.h`
  espera.
- Correção: **nunca nomear iterators** sobre container de type de module. Usar
  range-for (iterator anônimo) + `std::erase_if`.
  - `Client::remove_finished`: `src/usurl.cxx:779-795`
    (`for (auto* r : records)` + `std::erase_if(records, ...)`).
- Regra prática: em código `export`ed, evitar `std::list`/`std::vector` com iterators
  nomeados; preferir range-for / `std::erase_if` / algoritmos que não exigem iterator
  nomeado.

### 5. O IMPORTER precisa dos MESMOS std-headers, ANTES do `import`
- Sintoma: `test_main.cpp` com `import usurl;` na linha 1 e std-headers depois → erro
  "incomplete type" / entidades ausentes ao instantiar templates exportados.
- Regra: o TU importador deve incluir no **global fragment** (antes do `import`) todos os
  headers std que a interface do module referencia (a interface "assume" que o importer
  já viu esses tipos).
- Correção: reordenar `tests/test_main.cpp` — blocos de `#include <...>` std primeiro
  (linhas 1-12), depois `import usurl;` (`tests/test_main.cpp:26`).
