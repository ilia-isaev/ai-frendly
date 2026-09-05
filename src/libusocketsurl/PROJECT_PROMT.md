## Especifico nesse projeto
Esse projeto deve usar C++. Esse projeto vai ser uma lib.
Esse projeto vai usar libuv e openssl 3.x.x
Esse projeto vai ser integrado com `uv_loop_t*` externo.

Veja código uSockets. Objetivo é usar uSockets para fazer consultas HTTP GET.
Código deve funcionar em único só thread.

Um modulo deve esconder toda logica. Modulo deve receber `uv_loop_t*` de fora.
Na hora de fazer requisição HTTP GET método deve receber: `url` e mais um objeto
com tipo especificado em template. Esse método, deve retornar imediatamente.
Deve existir uma outro método para consultar os requisições finalizadas. Deve
existir um método para remover os requisições finalizadas.

Quando consultar os requisições prontos, alguns request não vão ser tratados na
hora, por isso preciso uma função que só remove requisições escolhidos.

Veja considerações sobre ambiente em `PROJECT_PROMT_EXT.md`.

Caminho para código fonte uSockets: `../../third_party/uSockets-0.8.8`
Caminho para código fonte libuv: `../../third_party/libuv-v1.52.0`

1 Analisa código fonte.
2 Veja se da para fazer consultas HTTP GET assíncrono.
2.1 Veja se da para fazer consultas HTTP POST assíncrono.
2.2 Depois de receber token de autorização (exemplo da resposta esta em `./docs/auth_token_response.md`) consulta HTTP GET deve ter possibilidade de usar esse token de autorização.
3 Cria esboço de arquitetura desse projeto e plano de implementação. Ponha resultado em `./docs`
4 Revisa arquitetura e implementação.
5 Implemente projeto usando passos anteriores.
6 Cria documentação.
7 Revise código.
8 Implemente testes.
