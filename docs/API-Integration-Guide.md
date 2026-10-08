# API integration guide

Base URL: `http://<terminal>/api/v1`. JSON in, JSON out (`Content-Type: application/json`).
Full schema: [`openapi.yaml`](openapi.yaml).

## 1. Find the terminal

- mDNS service **`_rfid-terminal._tcp`**, TXT records `name`, `fw`, `api` (`/api/v1`)
- or `http://<hostname>.local` (default `rfid.local`), or the IP shown on the display
- `GET /info` needs no token: `{name, firmware, reader: {ok}, display: {ok}, paired}`

```bash
dns-sd -B _rfid-terminal._tcp          # macOS
avahi-browse -r _rfid-terminal._tcp    # Linux
```

## 2. Pair once

```bash
curl -X POST http://rfid.local/api/v1/pair/start
# -> {"expires_in_s":120}; a 6-digit code shows on the display for 2 minutes

curl -X POST http://rfid.local/api/v1/pair/confirm \
     -H 'Content-Type: application/json' -d '{"code":"042917","name":"Inventory app"}'
# -> 201 {"token":"<64 hex>","id":"1A2B3C4D","name":"Inventory app"}
```

- Store the token: it is returned only once (the terminal keeps a SHA-256 of it).
- Send it on every other call: `Authorization: Bearer <token>`.
- 5 wrong codes cancel the pairing (`403 bad_code`, then `410 pairing_expired`).
- Up to 8 paired devices; the oldest is dropped beyond that. `401` means the token was
  revoked or the terminal was reset: pair again.

## 3. Subscribe to events

```
ws://rfid.local/api/v1/events?token=<token>
```

Every message: `{"event": "<name>", "data": {...}, "t": <epoch or 0>}`

| Event | Data |
|-------|------|
| `hello` | `{info, card?, job?}`, sent on connect: full current state |
| `card.present` | Card: `{uid, type, kind, sak, atqa, ndef_capacity?, sectors?}` |
| `card.removed` | `{uid}` |
| `job.updated` | Job, without `result` |
| `reader.status` | `{ok, firmware}` |

Reconnect after a close (the terminal may reboot); `hello` gives you the state back.
A connection with a bad token is refused at the handshake.

## 4. Run jobs

The terminal is passive: you create a job, the display says "Place the badge", the job
runs on the first badge presented. **One job at a time**: a second one gets `409 busy`.

| Route | Body | Does |
|-------|------|------|
| `POST /read` | `{key?}` | full dump + decoded NDEF |
| `POST /write/ndef` | `{records: [{type: "url"\|"text", value, lang?}]}` | writes an NDEF message |
| `POST /write/raw` | `{block, hex \| text, key?}` | one Classic block / NTAG page |
| `POST /erase` | `{key?}` | clears the user area |
| `GET /jobs/{id}` | | job, with `result` for reads |
| `DELETE /jobs/{id}` | | cancels a job still `waiting` |

`key` is the MIFARE Classic key A as 12 hex digits (default `FFFFFFFFFFFF`).

Lifecycle: `waiting` → `running` → `done` | `error` | `timeout` (20 s without badge) |
`cancelled`. Follow it with `job.updated` events, or poll `GET /jobs/{id}`.

```json
{"id": 7, "kind": "write_ndef", "state": "done", "code": "ok",
 "message": "NDEF written (42 bytes)", "uid": "53:D8:55:42:14:00:01"}
```

**Code against `code`, show `message`** (it is in the display language).

| `code` | Meaning |
|--------|---------|
| `ok` | success |
| `card_removed` | badge left the field during the job |
| `unsupported_card` | not a MIFARE Classic / Type 2 badge (e.g. a bank card, a phone) |
| `ndef_type2_only` | NDEF writes need an NTAG / Ultralight |
| `not_ndef_formatted` | Type 2 badge without NDEF capability container |
| `too_long` | message larger than the badge capacity |
| `out_of_range` | block / page outside the writable area |
| `protected_block` | block 0 or a sector trailer: refused on purpose |
| `auth_failed` | wrong Classic key A |
| `write_failed` | the badge did not acknowledge the write |
| `bad_data` | too many bytes for one block / page |
| `timeout` | no badge within 20 s |
| `cancelled` | cancelled through `DELETE /jobs/{id}` |

### Read results

Type 2 (NTAG / Ultralight):

```json
{"kind": "type2",
 "pages": ["53D85556", "42140001", "57480000", "E1101200", "0103A00C", "..."],
 "ndef": [{"type": "url", "value": "https://example.com"},
          {"type": "text", "lang": "en", "value": "Hello"}]}
```

MIFARE Classic:

```json
{"kind": "mifare_classic",
 "sectors": [{"sector": 0, "blocks": ["EB1125548B08...", "...", "...", "..."]},
             {"sector": 5, "locked": true}]}
```

`locked` means the key A was refused for that sector. Key A always reads back as zeros in
trailer blocks.

## 5. Other routes

| Route | Use |
|-------|-----|
| `GET /card` | badge on the reader now (`404 no_card` otherwise) |
| `GET /history` | last 20 badges `{uid, type, seconds_ago, at?}` |
| `POST /display` | `{text, seconds}`: message on the display (0-3600 s) |
| `GET/PUT /config` | `{name, hostname, lang, contrast}` |
| `PUT /wifi` | `{ssid, password}`, then reboot |
| `GET /wifi/scan` | `202` while scanning, then `{networks}` |
| `GET /tokens`, `DELETE /tokens/{id}` | paired devices |
| `POST /update` | multipart `firmware=@firmware-c3.bin`, then reboot |
| `POST /reboot`, `POST /factory-reset` | |

Errors: `{"error": {"code": "...", "message": "..."}}` with codes such as `unauthorized`,
`busy`, `reader_offline`, `bad_json`, `bad_content_type`, `bad_records`, `not_found`.

## Example: JavaScript client

```js
const base = 'http://rfid.local/api/v1';
const headers = { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' };

const ws = new WebSocket(`ws://rfid.local/api/v1/events?token=${token}`);
ws.onmessage = async (m) => {
  const { event, data } = JSON.parse(m.data);
  if (event === 'card.present') console.log('badge', data.uid, data.type);
  if (event === 'job.updated' && data.state === 'done' && data.kind === 'read') {
    const job = await (await fetch(`${base}/jobs/${data.id}`, { headers })).json();
    console.log(job.result.ndef);
  }
};

// Write a link on the next badge
await fetch(`${base}/write/ndef`, {
  method: 'POST', headers,
  body: JSON.stringify({ records: [{ type: 'url', value: 'https://example.com' }] }),
});
```

## Example: Python client

```python
import requests, time

BASE = "http://rfid.local/api/v1"
H = {"Authorization": f"Bearer {TOKEN}"}

job = requests.post(f"{BASE}/read", json={}, headers=H).json()
while job["state"] in ("waiting", "running"):
    time.sleep(0.5)
    job = requests.get(f"{BASE}/jobs/{job['id']}", headers=H).json()
print(job["code"], job.get("result", {}).get("ndef"))
```

## Notes for integrators

- CORS is open (`Access-Control-Allow-Origin: *`), so browser apps on other origins work.
- The API is plain HTTP on the local network; the token protects against other LAN
  users, not against someone sniffing the traffic. Keep terminals on a trusted network.
- Bodies are limited to 4 KB (the NDEF message itself to 888 bytes, the NTAG216 size).
