# Dodo Payments Webhook Receiver

Receives subscription webhooks from Dodo Payments and writes `keys.json`,
which the exit node reads to map supporters to their priority tunnel key.

**Code:** [`code/webhook/webhook_server.py`](../code/webhook/webhook_server.py)

## Run

```bash
DODO_WEBHOOK_SECRET=whsec_... PORT=8080 python3 webhook_server.py
```

In the Dodo dashboard, set the webhook URL to
`https://your-host:8080/webhooks/dodo` and subscribe to:

- `subscription.created` / `subscription.active` / `subscription.renewed`
- `subscription.expired` / `subscription.cancelled` / `subscription.payment_failed`

## Environment variables

| Name | Purpose |
|---|---|
| `DODO_WEBHOOK_SECRET` | HMAC secret from the Dodo dashboard; verifies webhook signatures |
| `KEYS_FILE` | Path to `keys.json` (default `./keys.json`) |
| `PORT` | Listen port (default 8080) |

## keys.json format

```json
{
  "free_key": "changeme-standard-key",
  "members": {
    "sub_abc123": {
      "email": "user@example.com",
      "key": "vip-0f1e2d3c...",
      "status": "active",
      "tier": "supporter",
      "since": "2026-10-02T12:00:00+00:00"
    }
  }
}
```

Only members with `"status": "active"` are allowed through the paid tunnel —
see the tier/QoS section in the root [README](../README.md).
