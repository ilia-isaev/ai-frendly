# Etapa 6 — HTTP POST assíncrono (auth token)

## Contexto
Passo 2.1 do `PROJECT_PROMT.md`: adicionar suporte a **POST** para o caso de uso de
auth (token JWT). O caller (chamada a MOSIP/Keycloak) monta o JSON de request
(formato em `docs/auth_token_request.json`) e a lib só envia a string; a resposta
traz o JWT no header `Set-Cookie: authorization: <JWT>` (ver
`docs/auth_token_response.md`), então a lib precisou expor os headers da resposta.

## O que mudou

### Header `src/usurl.hpp`
- `Finished<T>` ganhou `std::vector<std::pair<std::string, std::string>> headers;`
  (src/usurl.hpp:24) — headers da resposta, para o caller extrair o `Set-Cookie`.
- Nova overload `std::string build_request(const ParsedUrl& p, const std::string& body);`
  (src/usurl.hpp:60) — monta request POST.
- `RecordBase` ganhou `resp_headers` (src/usurl.hpp:96); o ctor de `Record<T>`
  aceita `body` opcional e escolhe GET/POST por `body.empty()`
  (src/usurl.hpp:128-129).
- Commit copia `resp_headers` → `fin.headers` (src/usurl.hpp:141).
- `Client::post<T>(url, body, payload)` (src/usurl.hpp:171) — despacha e retorna
  imediatamente, idêntico a `get<T>`.

### Implementação `src/usurl.cpp`
- `build_request(p, body)` (src/usurl.cpp:237) — `POST <path> HTTP/1.1` + `Host` +
  `Connection: close` + `Content-Type: application/json` + `Content-Length` +
  blank line + body. O head e o corpo ficam **na mesma string `request`**, então
  o send path existente (`on_open`/`on_writable` com partial write) funciona
  sem nenhuma mudança.
- `parse_headers(head)` (src/usurl.cpp:55) — extrai `name: value` de cada linha
  de header da resposta (skip da status line).
- `detect_framing` popula `resp_headers = parse_headers(headers)`
  (src/usurl.cpp:475) — mesmo ponto onde o framing é detectado, quando o
  head da resposta já está completo.

### Teste `tests/test_main.cpp`
- `build_response` agora aceita `extra_headers` e `status` (tests/test_main.cpp:75).
- `TcpServer` lê o request completo (head + body por `Content-Length`) e guarda o
  corpo recebido em `received_body`; helper `request_content_length`
  (tests/test_main.cpp:97).
- Case 1 (GET) ganhou check de `Connection: close` em `fins[0].headers`.
- **T6** (tests/test_main.cpp:433): POST 200 — body de request chegou íntegro no
  server (`received_body == auth_req`), status 200, body de resposta, payload
  preservado, e `Set-Cookie: authorization: jwt-secret-123` exposto em
  `Finished<int>::headers`.
- **T7** (tests/test_main.cpp:466): POST 401 — status 401 reportado + body
  recebido.

## Resultado
- `ninja -C build` — verde.
- `./build/usurl_tests` — **28/28 PASS** (20 anteriores + 8 novos), exit 0,
  `ALL TESTS PASSED (0 failure(s))`.

## Decisões
Detalhadas em `design_notes/post-auth.md`. Resumindo: body vazio → GET (fallback),
`Content-Type: application/json` hardcoded (v1), body na string `request`
(sem mudança no send path), headers expostos como vector de pares.
