## Especifico nesse projeto
Esse projeto deve usar C++. Esse projeto vai ser uma lib.
Esse projeto vai usar libuv e openssl 3.x.x
Esse projeto vai ser integrado com `uv_loop_t*` externo.

Veja codigo uSocket. Objetivo é usar uSocket para fazer consultas HTTP GET.
Codigo deve funcionar em unico só thred.

Um modulo deve esconder toda logica. Modulo deve receber `uv_loop_t*` de fora.
Na hora de fazer requisição HTTP GET metodo deve reseber: `url` e mais um objeto
com tipo especificado em template. Esse metodo, deve retornar imediatamente.
Deve existir uma outro metodo para consultar os requisições finalizadas. Deve
existir um metodo para remover os requisições finalizadas.

Quando consultar os requisições prontos, alguns request não vão ser tratados na
hora, por isso preciso uma função que só remove requisições escolidos.

Veja considerações sobre ambiente em `PROJECT_PROMT_EXT.md`.

Caminho para código fonte uSockets: `../../third_party/uSockets-0.8.8`
Caminho para código fonte libuv: `../../third_party/libuv-v1.52.0`

1 Analiza código fonte.
2 Veja se da para fazer consultas HTTP GET asyncrono.
2.1 Veja se da para fazer consultas HTTP POST asyncrono.
3 Cria esboço de arquitetura desse projeto e plano de implementação. Ponha resultado em `./docs`
4 Revisa arquitetura e implementação.
5 Implemente projeto usando passos anteriores.
6 Cria documentação.
7 Revise código.
8 Implemente testes.
