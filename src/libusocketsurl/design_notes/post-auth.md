# Design notes — POST para auth token

Caso de uso: `Client::post<T>(url, body, payload)` — POST JSON a endpoint
MOSIP/Keycloak; JWT retorna no header `Set-Cookie: authorization: <JWT>` da
resposta (ver `docs/auth_token_request.json` / `docs/auth_token_response.md`).

## Decisões

### 1. Escolha GET vs. POST por `body.empty()`
`Record<T>` (src/usurl.hpp:128-129): `body.empty() ? build_request(parsed) :
build_request(parsed, body)`.
- POST com body vazio cai para GET — edge case documentado e aceitável para v1
  (auth token sempre envia body JSON).
- Mantém o ctor de `Record<T>` com parâmetro opcional; `get<T>` continua
  inalterado.

### 2. `Content-Type: application/json` hardcoded
`build_request(p, body)` (src/usurl.cpp:237) fixa o content-type. Decisão de v1:
o único caso de uso é JSON. Se um dia precisar de outro type, vira parâmetro ou
headers customizáveis no `post`.

### 3. Corpo na mesma string `request` — sem mudança no send path
O head POST (`Content-Length: N`) + o corpo são concatenados na string `request`
do `RecordBase`. Consequência: o mecanismo existente de partial write
(`on_open`/`on_writable` com `request_written`) já trata head e corpo juntos —
nenhuma mudança em `us_socket_send`/chain/writev. Alternativa rejeitada:
`us_socket_send` com dois buffers (head + body) exigiria track de dois offsets e
rework dos callbacks.

### 4. Headers de resposta expostos como vector de pares
`Finished<T>::headers` (src/usurl.hpp:24) é
`std::vector<std::pair<std::string, std::string>>` — simples para o caller fazer
`find_if`/loop para extrair `Set-Cookie`. Alternativa rejeitada: mapa
`std::map<std::string,std::string>` (perde headers duplicadas, ex.: dois
`Set-Cookie`).
- Parsing em `parse_headers(head)` (src/usurl.cpp:55): pula a status line,
  itera as linhas `Name: value`, trimming de espaços.
- Preenchido em `detect_framing` (src/usurl.cpp:475) — o ponto único em que o
  head da resposta está completo; copiado no commit para `fin.headers`
  (src/usurl.hpp:141).

### 5. `Content-Length` é obrigatório no POST
Sem chunked request (fora do escopo do caso de uso). O server de teste do
harness lê o corpo por `Content-Length` (`request_content_length`,
tests/test_main.cpp:97) e guarda em `TcpServer::received_body` para o check de
integridade do body (T6).

## Testes
- T6 (tests/test_main.cpp:433): POST 200 com `Set-Cookie` — body de request
  íntegro no server, headers expostos, payload preservado.
- T7 (tests/test_main.cpp:466): POST 401 — status reportado, body de resposta.
- Case 1 GET ganhou check de `Connection: close` em headers (regressão do
  parsing).

## Fronteira / próximos passos
- JWT em `Set-Cookie`: a lib **não** interpreta o cookie — o caller extrai de
  `Finished<T>::headers` (passo do prompt: "extração do JWT fica no caller").
- Sem retry, sem cookie jar, sem keep-alive (restrição `Connection: close` do
  design).
