#!/usr/bin/env python3
"""Dodo Payments webhook receiver — provisions paid-tier tunnel keys.

Listens on POST /webhooks/dodo, verifies the x-dodo-signature HMAC header,
and writes keys.json with active/inactive supporter keys.

Run:  DODO_WEBHOOK_SECRET=whsec_... PORT=8080 python3 webhook_server.py
"""

import hashlib
import hmac
import json
import os
import secrets
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

WEBHOOK_SECRET = os.environ.get("DODO_WEBHOOK_SECRET", "")
KEYS_FILE = os.environ.get("KEYS_FILE", "keys.json")
PORT = int(os.environ.get("PORT", "8080"))

SIG_TOLERANCE = 300  # seconds — reject timestamps older than this


# ---------------------------------------------------------------------------
# keys.json helpers
# ---------------------------------------------------------------------------

def load_keys():
    if os.path.exists(KEYS_FILE):
        with open(KEYS_FILE) as f:
            return json.load(f)
    return {"free_key": "changeme-standard-key", "members": {}}


def save_keys(data):
    tmp = KEYS_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2)
    os.replace(tmp, KEYS_FILE)  # atomic: exit node never reads a half-file


def set_member_status(subscription_id, status):
    data = load_keys()
    member = data["members"].get(subscription_id)
    if member:
        member["status"] = status
        save_keys(data)
        print(f"[keys] {subscription_id} -> {status}")
    else:
        print(f"[keys] unknown subscription {subscription_id} ({status})")


# ---------------------------------------------------------------------------
# Webhook signature verification (Dodo: t=<unix>,v1=<hmac-sha256 hex>)
# ---------------------------------------------------------------------------

def verify_signature(payload: bytes, header: str) -> bool:
    if not WEBHOOK_SECRET:
        print("[warn] DODO_WEBHOOK_SECRET unset — refusing all webhooks")
        return False
    try:
        parts = dict(p.split("=", 1) for p in header.split(","))
        ts, sig = parts["t"], parts["v1"]
    except (ValueError, KeyError):
        return False
    if abs(time.time() - int(ts)) > SIG_TOLERANCE:
        return False
    signed = f"{ts}.{payload.decode('utf-8', errors='replace')}"
    expected = hmac.new(
        WEBHOOK_SECRET.encode(), signed.encode(), hashlib.sha256
    ).hexdigest()
    return hmac.compare_digest(expected, sig)


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):

    def do_POST(self):
        if self.path != "/webhooks/dodo":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", 0))
        payload = self.rfile.read(length)
        sig = self.headers.get("x-dodo-signature", "")

        if not verify_signature(payload, sig):
            self.send_error(401, "invalid signature")
            return

        event = json.loads(payload)
        self._handle(event["type"], event.get("data", {}))
        self._ok({"received": True})

    def _handle(self, event_type, data):
        obj = data.get("object", data)  # tolerate both payload shapes
        sub_id = obj.get("subscription_id") or obj.get("id", "")
        customer = obj.get("customer", {})
        email = customer.get("email", "") if isinstance(customer, dict) else ""

        if event_type in ("subscription.created", "subscription.active",
                          "subscription.renewed"):
            data_keys = load_keys()
            member = data_keys["members"].get(sub_id) or {
                "email": email,
                "key": "vip-" + secrets.token_hex(16),
                "since": datetime.now(timezone.utc).isoformat(),
            }
            member["tier"] = "supporter"
            member["status"] = "active"
            data_keys["members"][sub_id] = member
            save_keys(data_keys)
            print(f"[provision] {sub_id} key={member['key'][:12]}... email={email}")

        elif event_type in ("subscription.expired", "subscription.cancelled",
                            "subscription.payment_failed"):
            set_member_status(sub_id, "inactive")

        else:
            print(f"[skip] unhandled event {event_type}")

    def _ok(self, body):
        body_bytes = json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body_bytes)))
        self.end_headers()
        self.wfile.write(body_bytes)

    def log_message(self, fmt, *args):
        print("[http]", fmt % args)


if __name__ == "__main__":
    print(f"listening on :{PORT} at /webhooks/dodo (keys: {KEYS_FILE})")
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
