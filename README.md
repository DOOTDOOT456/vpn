# ESP8266 MQTT VPN — Unblock Sites for a Chromebook

Turn a $3 ESP8266 into a tiny VPN gateway that tunnels your traffic through an
MQTT broker to an exit node on an uncensored network. Based on
[martin-ger/MQTT_VPN](https://github.com/martin-ger/MQTT_VPN).

**Read the full guide: [SETUP.md](SETUP.md)**

## How it works

```
Chromebook ──WiFi──> ESP8266 (NAT) ──MQTT broker──> Linux exit node ──> Open Internet
```

- The ESP8266 runs the `mqtt_vpn_nat` sketch from MQTT_VPN. It acts as a NAT
  router: WiFi clients on one side, an IP-over-MQTT tunnel on the other.
- An MQTT broker relays the tunneled packets. Traffic between ESP and exit
  node is encrypted with libnacl using a preshared key — an observer sees only
  encrypted blobs and can't read or inject packets.
- A Linux box (Raspberry Pi, home PC, or VPS) on an unrestricted network runs
  the `mqtt_vpn` client and forwards tunneled traffic out to the internet.

## What you need

| Part | Notes |
|---|---|
| ESP8266 board | NodeMCU or Wemos D1 Mini |
| USB micro cable + PC | For flashing |
| MQTT broker | HiveMQ Cloud (free TLS tier) or self-hosted Mosquitto |
| Linux exit node | Raspberry Pi / spare PC / ~$5 VPS on an open network |
| Chromebook | Any model that supports manual network config |

## Honest limitations

- **~0.3–1 Mbps throughput** — fine for browsing, painful for video.
- Bypassing a school/organization's network filtering usually violates its
  acceptable-use policy. Know the rules you're working under.

## License / Credits

Tunnel firmware and Linux client by [martin-ger/MQTT_VPN](https://github.com/martin-ger/MQTT_VPN).
