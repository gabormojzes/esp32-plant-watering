# HTTP API

The device serves an HTTP API when `Http` is listed in `config::MESSAGING` (see
[Configuration](configuration.md#messaging)). It carries [telemetry](telemetry.md) out and
[commands](commands.md) in.

The server listens on `config::HTTP_PORT`, on every interface the device has: `http://<device-ip>/`
on the network it joined, `http://192.168.4.1/` on the one it serves itself, or both under `ApSta`.

## Routes

| Route | Response |
|---|---|
| `GET /` | The dashboard, gzip-compressed. A browser without a session is sent to `/login` |
| `GET /login` | The login page, gzip-compressed. A browser with a session is sent to `/` |
| `POST /login` | `204` and the session cookie, or an error (below) |
| `POST /logout` | `204`, closing the session and clearing the cookie. A browser without a session is sent to `/login` |
| `GET /api/telemetry` | The most recent telemetry frame as JSON, or `503` until the first one |
| `POST /api/command` | `202` once accepted, or an error (below) |
| `OPTIONS /api/telemetry`, `OPTIONS /api/command` | `204`, carrying CORS preflight headers |

Every answer from an endpoint above carries `X-Content-Type-Options: nosniff`, which keeps a browser
from reading it as any type but the one it declares.

`POST /api/command` answers with one of:

| Status | Cause |
|---|---|
| `202` | The command is accepted |
| `400` | The request could not be read, or the connection failed before the whole body arrived |
| `408` | The body stopped arriving |
| `413` | The body is too large |
| `415` | The body is not `application/json` |
| `503` | The device cannot take another command right now |

`202` means accepted for handling, not that the body is valid JSON or its values passed validation.
`GET /api/telemetry` shows whether a value landed.

## Credentials

A browser reaches the dashboard and the API with a session, which the dashboard's own credentials
open. Every other client reaches the API with a token. Both credentials are set in
`main/config.hpp`, and the build refuses one that is missing or too short.

### The dashboard: a session

`GET /` is served to a browser holding a session, and to nothing else. A session is opened by
posting the credentials to `/login`:

```bash
curl -c jar.txt -H 'Content-Type: application/json' \
  -d '{"user":"<user>","password":"<password>"}' http://<device-ip>/login
```

`POST /login` answers `204` and a `Set-Cookie` on the right credentials, `401` on wrong ones or a
body that is not valid JSON, and `400`, `408`, `413` or `415` for a body it could not read, with the
same meanings as on `/api/command` above. The cookie is `HttpOnly` and `SameSite=Strict`, which
keeps it out of the dashboard's own scripts and off every origin but the device's.

The session also opens `/api/telemetry` and `/api/command`. On those routes and on `POST /logout` it
counts for nothing on a request a browser marks as made by another origin, with a `Sec-Fetch-Site`
of `cross-site` or `same-site`.

A session lasts four weeks from the login. It ends sooner on `POST /logout`, on a reboot, or when a
fifth login takes the oldest of four slots.

`config::DASHBOARD_USER` is 5 to 31 characters, and `config::DASHBOARD_PASSWORD` 8 to 31.

### The API: a token

Any client reaches `/api/telemetry` and `/api/command` with `config::HTTP_API_TOKEN`, whatever its
origin. The token never expires; it changes only with the firmware.

```bash
curl -H 'Authorization: Bearer <token>' http://<device-ip>/api/telemetry
```

A `GET` or `POST` with neither the right token nor a session is answered `401` with a `Bearer`
challenge.

`config::HTTP_API_TOKEN` is 32 to 63 characters.

Neither credential is encrypted in transit or stored hashed. Both are compiled into the firmware as
they are written.

## Cross-origin requests

`/api/telemetry` and `/api/command` send `Access-Control-Allow-Origin: *` and allow `Authorization`
in a preflight. A page served from another origin reaches the API with the token, like any other
client, and never with a session.

Requiring `application/json` on `POST /api/command` puts every cross-origin command behind a
preflight. A command looks like this:

```bash
curl -H 'Authorization: Bearer <token>' -H 'Content-Type: application/json' \
  -d '{"pump":{"duration":60}}' http://<device-ip>/api/command
```

`/login` claims no preflight at all, and no sharing headers, since a login is only ever offered by
the page the device serves itself.

## Dashboard

The dashboard and the login page are served from flash. They load nothing from another host, send
data only to the device, and cannot be shown in another site's frame. The dashboard uses
`/api/telemetry` and `/api/command` with its session, and a `401` from either sends it back to the
login page.
