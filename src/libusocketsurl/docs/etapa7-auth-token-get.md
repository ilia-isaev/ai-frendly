# Etapa 7 — GET/POST com token de autorização (prompt item 2.2)

## Contexto
Prompt item 2.2: depois de receber o token de autorização (POST no endpoint de
auth, JWT no header `Set-Cookie: authorization: <JWT>`), a consulta HTTP GET
deve ter possibilidade de usar esse token. O usuário adicionou em `./docs` os
exemplos de resposta de **erro** do endpoint de auth
(`auth_token_error_cookie_is_empty.json`, `auth_token_error_invalid_token.json`,
`auth_token_error_incorrect_application_id_responses.json`,
`auth_token_error_responses.json`) — todos com `response: null` +
`errors[].errorCode` (`KER-ATH-006`, `KER-ATH-401`, `KER-ATH-026`, `500`/
`401 Unauthorized`) e **sem** `Set-Cookie`.

## O que foi implementado
- `get<T>(url, payload, token = {})` e `post<T>(url, body, payload, token = {})`
  (`src/usurl.hpp:162-178`) — token opcional; chamadas antigas intactas.
- Builder central `build_request(p, body, token)` (`src/usurl.cpp:236-258`):
  `body` vazio ⇒ GET; `token` não-vazio ⇒ header `Authorization: Bearer <token>`
  (`src/usurl.cpp:256`). Overloads de 1/2 args viraram wrappers
  (`src/usurl.cpp:226-234`).
- `Record<T>` recebe `token` (`src/usurl.hpp:121`) e sempre chama o builder de
  3 args (`src/usurl.hpp:129`).
- Decisões e justificativas: `design_notes/get-auth-token.md` (D1–D6).
- Decisão-chave: a lib **não** interpreta a resposta de auth — expõe
  `status`/`body`/`headers`; sucesso/falha é decidido pelo caller pela presença
  do `Set-Cookie` (e, em falha, pelo `errors[].errorCode` no body).

## Teste
- Harness `TcpServer` estendido: `received_head` (head do request recebido) e
  `require_auth` (Bearer ausente/errado ⇒ responde 401)
  (`tests/test_main.cpp:139-140,155,217-224`).
- Novos casos:
  - T8 (`:536`) — GET com token ⇒ 200 + servidor viu o Bearer; GET sem token ⇒ 401.
  - T9 (`:571`) — POST com token ⇒ Bearer presente + body íntegro.
  - T10 (`:593`) — fluxo end-to-end: POST `/token` ⇒ `extract_token()`
    (`:77-93`) tira o JWT do `Set-Cookie` ⇒ GET autenticado ⇒ 200.
  - T11 (`:638`) — falha de auth: 200 + JSON de erro (`KER-ATH-401`) e sem
    cookie ⇒ token vazio ⇒ GET sem token ⇒ 401.

## Resultado
- Build: `ninja -C build` — verde.
- `./build/usurl_tests` — `ALL TESTS PASSED`, **49 checks**, exit 0
  (antes: 30).

## Aprendizado novo (para as próximas etapas)
- MOSIP responde **200** até em erro de auth — o caller **não** pode usar o
  status HTTP do POST para detectar falha de token; tem que inspecionar
  `Set-Cookie` (ausente) e/ou `errors[].errorCode` no body.
- `Finished<T>::headers` já cumpria seu papel: a extração do JWT é um loop
  simples sobre o vector (implementado no teste como `extract_token`); não
  houve necessidade de mudar a API de `Finished<T>`.
