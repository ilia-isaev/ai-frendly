## Geral
Não instale nada e use só as ferramentas já instaladas.
Use CMake com Ninja para compilar o projeto.
Use `find_library` no CMakeLists.txt, exemplo: `find_library(LIB_UV NAMES uv HINTS ../../lib REQUIRED)`.
Use C++23 com cabelçalhos convencionais.
Crie, se não existirem, os diretórios `./tests` e `./scratch`.
Ponha o código-fonte no diretório `./src`.
Ponha a documentação no diretório `./docs`.
Ponha informações sobre o desenho no diretório `./design_notes`.
Pode ver os diretórios `./`, `../../include`, `../../lib` e `../../third_party`.
Veja como foram compiladas as dependências em `../../third_party/TIPS`.

## Gotchas de build (C++23 + uSockets)
Erros que custam tempo real de debugging. O "porquê" e o detalhe completo estão em `design_notes/`.
- Forward-decl de tipo exportado: declare `export` no site de definição e redeclare **não-export** no importador (consistência), senão o GCC quebra a instantiation do template.
- Membro de classe-base **não** pode ir no member-init-list; assigne no corpo do ctor.
- Nome de função não pode shadowear um membro de dados do mesmo tipo (ex.: `loop` vs `loop_`).
- uSockets: `pre_cb`/`post_cb`/`wakeup_cb` **não** podem ser NULL (o prepare-handle fica sempre ativo e chama o callback a cada `uv_run`); use stubs no-op.
- uSockets: `us_create_loop(hint)` com `hint != NULL` adota o `uv_loop_t*` externo (`is_default=1`); `us_loop_free` nesse caso **não** deleta nem roda o `uv_loop`.

## Build e teste
- Compilar: `ninja -C build` (target da lib = `usurl`; target de teste = `usurl_tests`).
- Rodar o teste: `./build/usurl_tests` (não usa CTest; espera exit 0 + `ALL TESTS PASSED`).
- O teste é um executável (`build/usurl_tests`); ainda não está ligado ao CTest.

## Documentação
Depois de finalizar cada busca sobre implementação e arquitetura,
coloque as informações relevantes encontradas em notas no diretório `./design_notes`.
Em notas, anote o nome do componente, o arquivo de código-fonte e as linhas a onde se
encontra a informação relevante no código-fonte.
Depois de concluir cada etapa, crie um resumo, com o contexto importante aprendido,
no diretório `./docs`.

Antes de começar, veja o que foi feito nos resumos que estão em `./docs`;
veja se existe alguma divergência no projeto atual, se há novas informações
para etapas já feitas e se houve alguma correção externa. Se precisar, faça
ajuste nos documentos em `./docs` para eliminar divergências.

Veja o que precisa ser feito para este projeto em `PROJECT_PROMT.md`.
