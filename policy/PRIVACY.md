# Privacy Policy — Community VPN Hotspot

_Last updated: October 2, 2026_

This hotspot is operated by the operator of the Raspberry Pi exit node
("the operator"). If you use it, this is what happens with your data.

## What we can see

All traffic leaving the hotspot is tunneled to the operator's Raspberry Pi.
The tunnel contents are encrypted between the ESP8266 hotspot and the Pi, so
plaintext cannot be read at the broker. However:

- **Web traffic you send over plain HTTP is readable at the exit node** — the
  tunnel ends at the Pi, which forwards packets to their destinations. Always
  prefer HTTPS sites (the browser padlock) for anything personal.
- **Metadata is visible to the operator:** which sites/domains you connect to
  (via DNS and TLS handshakes), how much data, and when. This is true of any
  VPN.
- **Device identities:** the hotspot sees your device's MAC address and
  hostname while connected.

## What we log

By default, **nothing is persisted**. The Pi's `mqtt_vpn` client and
Mosquitto broker log connection events only (for debugging), and system logs
rotate automatically. We do not record browsing history, DNS queries, or
traffic contents.

If the operator enables additional logging (e.g. for abuse investigation),
this policy will be updated first and users will be notified at the hotspot.

## What we never do

- No injection of content into your traffic (no ad replacement, no
  javascript injection, no SSL stripping).
- No selling or sharing of connection metadata with third parties.
- No correlation of your identity with your browsing beyond what is
  technically unavoidable (see above).

## Legal requests

If the operator receives a lawful request (subpoena, police request) for
records, the honest answer is: there are no stored traffic records to hand
over, only the operator's system logs and this policy. The exit IP of all
tunnel traffic is the Pi's own public IP, so abuse complaints will go to the
operator, not to individual users.

## Your responsibilities

- Use HTTPS wherever possible; the tunnel is not end-to-end encryption to
  the websites you visit.
- Don't use the hotspot for anything you wouldn't do on your own connection —
  see [ACCEPTABLE_USE.md](ACCEPTABLE_USE.md).
