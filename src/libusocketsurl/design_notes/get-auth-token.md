# Design note — GET/POST com token de autorização (prompt item 2.2)

Contexto: `PROJECT_PROMT.md` item 2.2 — "Depois de receber token de autorização ...
consulta HTTP GET deve ter possibilidade de usar esse token de autorização".
O JWT chega no header de resposta `Set-Cookie: authorization: <JWT>` (ver
`docs/auth_token_response.md`). Os exemplos de erro adicionados em `./docs`
(`auth_token_error_*.json`) definem como o caller detecta falha: JSON com
`response: null` + `errors[].errorCode`, e **sem** `Set-Cookie`.

## Decisões

### D1 — O token viaja em `Authorization: Bearer <token>`
MOSIP/Keycloak: o JWT do `Set-Cookie` é um JWT puro; nas chamadas subsequentes
ele viaja no header padrão `Authorization: Bearer <jwt>`. A lib adiciona esse
header quando `token` é não-vazio (`src/usurl.cpp:256`). Formato fixo em v1;
esquema alternativo (ex.: cookie) ficaria para v2.

### D2 — Parâmetro opcional, sem overload
`get<T>(url, payload, token = {})` e `post<T>(url, body, payload, token = {})`
(`src/usurl.hpp:162`, `src/usurl.hpp:171-172`). Chamadas antigas de 2/3 args
continuam compilando — compatibilidade de fonte total.

### D3 — `build_request(p, body, token)` é o builder central
`body` vazio ⇒ head de GET; `body` não-vazio ⇒ head de POST; `token` não-vazio ⇒
`Authorization: Bearer <token>` (`src/usurl.cpp:236-258`). As formas de 1 e
2 args viraram wrappers finos (`src/usurl.cpp:226-234`) — evita o problema de
ambiguidade de overload: `build_request(p, token)` e `build_request(p, body)`
teriam assinatura idêntica `(const ParsedUrl&, const std::string&)`.
`Record<T>` sempre chama a forma de 3 args (`src/usurl.hpp:121,129`).

### D4 — A lib não interpreta a resposta de auth
Fronteira mantida (ver `design_notes/post-auth.md`): a lib expõe `status`,
`body` e `headers` (`Finished<T>`); quem decide é o caller:
- sucesso ⇔ `Set-Cookie: authorization: <JWT>` presente (extração do caller);
- falha ⇔ cookie ausente/vazio, com o motivo em `body` (`errors[].errorCode`).
A lib não faz parse de JSON e não conhece os `errorCode`s.

### D5 — Semântica de erro do endpoint de auth (aprendido com os exemplos)
O endpoint de auth responde **HTTP 200** também em erro (padrão MOSIP): o
status HTTP **não** distingue sucesso de falha — a distinção está no body
(`response: null` + `errors[]`) e na ausência de `Set-Cookie`.
`errorCode`s documentados nos exemplos: `KER-ATH-006` (cookie vazio),
`KER-ATH-401` (token inválido), `KER-ATH-026` (appId/realm incorreta),
`500`/`401 Unauthorized`. Consequência prática: o fluxo de re-auth do caller
não pode ser disparado pelo status do POST; tem que inspecionar a extração do
JWT (vazio ⇒ falhar/reautenticar).

### D6 — Extração do JWT (padrão do caller, implementado no teste)
`Set-Cookie: authorization: <JWT>` → localizar `authorization:` no valor,
pegar o resto e trimar whitespace à esquerda. Implementado como
`extract_token()` no teste (`tests/test_main.cpp:77-93`) — espelha o que a
aplicação fará; de propósito **não** está na lib (fronteira D4).

## Cobertura de teste (novo)
- T8 (`tests/test_main.cpp:536`): GET com token ⇒ 200 e o servidor viu
  `Authorization: Bearer <jwt>`; GET sem token no mesmo endpoint ⇒ 401.
- T9 (`tests/test_main.cpp:571`): POST com token ⇒ header presente + body íntegro.
- T10 (`tests/test_main.cpp:593`): fluxo completo — POST `/token` ⇒ extrair
  JWT do `Set-Cookie` ⇒ GET autenticado com o JWT ⇒ 200.
- T11 (`tests/test_main.cpp:638`): falha de auth — POST responde 200 com JSON de
  erro (`KER-ATH-401`) e sem `Set-Cookie` ⇒ extração vazio ⇒ GET sem token ⇒ 401.
- Harness: `TcpServer` ganhou `received_head` (head do request) e
  `require_auth` (se o Bearer esperado não aparecer ⇒ responde 401)
  (`tests/test_main.cpp:139-140,155,217-224`).

## Resultado
Build verde; `usurl_tests` com **49 checks PASS**, exit 0.
Resumo da etapa: `docs/etapa7-auth-token-get.md`.
