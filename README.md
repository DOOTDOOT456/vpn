# ESP8266 MQTT VPN — Community Hotspot

Turn a $3 ESP8266 into a tiny VPN hotspot that tunnels everyone's traffic
through an MQTT broker to an exit node on an uncensored network. Based on
[martin-ger/MQTT_VPN](https://github.com/martin-ger/MQTT_VPN).

```
+-----------+   WiFi    +-----------+  MQTT (TLS)  +--------------+   Internet
| Laptops,  |<--------->|  ESP8266  |<------------>| MQTT broker  |
| phones    |  hotspot  | (NAT/tun) |              +------+-------+
+-----------+           +-----------+                     |
                                                   tunnels packets
                                                         |
                                                         v
                                                +------------------+
                                                | Linux exit node  |
                                                | (mqtt_vpn + NAT) |
                                                +------------------+
```

Users just join the ESP's WiFi hotspot — no per-device config. The ESP NATs
all clients behind a single encrypted IP-over-MQTT tunnel.

---

## Repo layout

```
firmware/mqtt_vpn_hotspot.ino   ESP8266 hotspot firmware (flash this)
api/webhooks/dodo/webhook_server.py   Paid-tier provisioning webhook
api/webhooks/dodo/README.md     Webhook receiver docs
```

Everything else you need to know is in **this file only**.

---

## 1. What you need

| Part | Example | ~Cost |
|---|---|---|
| ESP8266 dev board | NodeMCU v3 / Wemos D1 Mini | $3–5 |
| Micro-USB data cable + PC | for flashing | $2 |
| MQTT broker | HiveMQ Cloud free (TLS) or Mosquitto | $0 |
| Linux exit node | Raspberry Pi / spare PC / ~$5 VPS | $0–5/mo |

> Tip: run Mosquitto on the same Linux exit node — one box, one address.

## 2. Set up the MQTT broker

### Option A — Mosquitto on the exit node (recommended)

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
sudo mosquitto_passwd -c /etc/mosquitto/passwd vpnuser
```

`/etc/mosquitto/conf.d/vpn.conf`:

```
listener 8883
allow_anonymous false
password_file /etc/mosquitto/passwd
certfile /etc/mosquitto/certs/ca.crt
keyfile  /etc/mosquitto/certs/ca.key
```

```bash
sudo systemctl restart mosquitto
sudo ufw allow 8883/tcp
```

If behind home NAT, port-forward **TCP 8883** to the node. Note its public IP
(`curl ifconfig.me`).

### Option B — HiveMQ Cloud (managed, free tier)

Sign up, create a cluster, note the URL (`xxx.s1.eu.hivemq.cloud:8883`) and
create username/password credentials under Access Management.

## 3. Prepare the exit node

```bash
sudo apt install -y git build-essential cmake
git clone https://github.com/martin-ger/MQTT_VPN.git
cd MQTT_VPN/linux
sudo ./mqttVPNdependencyInstaller.sh     # builds ./mqtt_vpn
```

Enable forwarding + NAT:

```bash
echo 'net.ipv4.ip_forward=1' | sudo tee /etc/sysctl.d/99-vpn.conf
sudo sysctl -p /etc/sysctl.d/99-vpn.conf
ip route get 1.1.1.1                      # find outbound interface, e.g. eth0
sudo iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
sudo apt install -y iptables-persistent && sudo netfilter-persistent save
```

Run the client (one instance per tier — see §6):

```bash
sudo ./mqtt_vpn -i mq0 -a 10.0.1.1 \
  -b tls://YOUR_BROKER_HOST:8883 \
  -u vpnuser -p BROKER_PASSWORD \
  -k "standard-key-here" -d
```

Make it persistent with a systemd unit:

```ini
# /etc/systemd/system/mqtt-vpn.service
[Unit]
Description=MQTT VPN tunnel client
After=network-online.target

[Service]
ExecStart=/home/pi/MQTT_VPN/linux/mqtt_vpn -i mq0 -a 10.0.1.1 -b tls://HOST:8883 -u vpnuser -p PASS -k KEY
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now mqtt-vpn
```

## 4. Flash the ESP8266

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. **File → Preferences → Additional Board Manager URLs**:
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. **Boards Manager** → install **esp8266 by ESP8266 Community**; select your board.
4. Copy the `mqtt_vpn_arduino` library folder from the MQTT_VPN repo ZIP into
   `~/Arduino/libraries/`, restart the IDE.
5. Open `firmware/mqtt_vpn_hotspot.ino` (in a folder of the same name) and edit
   the CONFIG block at the top.

### Uplink WiFi — username & password

The uplink is the network the ESP itself uses to reach the broker. The
firmware supports two modes, controlled by `UPLINK_USES_ENTERPRISE`:

| Mode | Set this | When to use |
|---|---|---|
| **Regular WPA2** (default) | `UPLINK_SSID` + `UPLINK_PASSWORD` | Home/office WiFi with a normal passphrase |
| **WPA2-Enterprise (PEAP/MSCHAPv2)** | `UPLINK_SSID` + `UPLINK_USERNAME` + `UPLINK_PASSWORD`, `UPLINK_USES_ENTERPRISE = true` | School/campus WiFi that asks for a **username and password** (e.g. "eduroam"-style networks) |

This is what "per your wifi rules" means in practice: if your network policy
requires individual username/password login (802.1X), set
`UPLINK_USES_ENTERPRISE` to `true` and fill in the account the network rules
assign to you. Hosted networks that only need a shared key stay on the default
mode.

Also set:

- **Hotspot SSID / password** — what the community joins (≥ 8 chars; never run
  an open hotspot).
- **Broker** address, port, credentials.
- **`vpn_password`** (preshared tunnel key) — must match the `-k` on the exit
  node. Free and paid tiers use different keys (see §6).
- `mqtt_vpn_addr` — leave at `10.0.1.2` unless you changed the exit node.

Flash, then watch **Serial Monitor at 115200** — the heartbeat line shows
uptime, free heap, and connected clients.

### Test

From the exit node: `ping 10.0.1.2`. From a joined device: `curl ifconfig.me`
should return the **exit node's** IP.

## 5. Connect users

With the hotspot firmware, every device just joins the ESP's WiFi — DHCP and
routing are automatic. Verify exit with `curl ifconfig.me` (crosh on
Chromebook).

## 6. Tiers — free vs. paid

Both tiers use the same firmware and get identical encryption; the tier only
changes **which preshared key** devices use, which maps to QoS on the exit
node.

| | **Free** | **Supporter ($5/mo)** |
|---|---|---|
| Tunnel access | ✅ Standard key | ✅ Priority key |
| Bandwidth | Best-effort, deprioritized at peak | QoS priority on the exit node |
| Sessions | 2 devices | 5 devices |
| Support | Community | Priority |

Payments run through **Dodo Payments** (Merchant of Record — handles sales
tax/VAT). The webhook receiver in `api/webhooks/dodo/` provisions a unique
`vip-<random>` key on `subscription.created` and deactivates it on expiry or
cancellation. See `api/webhooks/dodo/README.md`.

### Exit-node QoS

```bash
# Priority queue: paid (mq1) traffic preferred under load
sudo tc qdisc add dev eth0 root handle 1: prio bands 3
sudo tc filter add dev eth0 parent 1: protocol ip prio 1 u32 match ip iif mq1 0 0 flowid 1:1
sudo tc filter add dev eth0 parent 1: protocol ip prio 3 u32 match ip iif mq0 0 0 flowid 1:3

# Cap free tier at 4 Mbit so heavy free users can't starve supporters
sudo tc qdisc add dev mq0 root tbf rate 4mbit burst 32kbit latency 50ms
```

Run **two instances** of the client and firmware (one per tier), with separate
tunnel IPs (`10.0.1.x` / `10.0.2.x`) and broker topic prefixes (`mqttip` /
`mqttipvip`). If you use one shared ESP hotspot, you need **two ESPs** — one
flashed per tier, serving two SSIDs (e.g. `CommunityVPN` and
`CommunityVPN-VIP`) — the simplest honest way to sell "priority".

`keys.json` (written by the webhook receiver) maps member IDs to keys and
active/inactive status; cron a small script every 5 min to reload it and drop
inactive keys.

## 7. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| ESP won't join WiFi | Wrong SSID/credentials; 5 GHz network (ESP8266 is 2.4 GHz only); enterprise WiFi without `UPLINK_USES_ENTERPRISE = true` |
| Enterprise auth fails | Wrong username/password; wrong anonymous identity; network requires a specific CA — check Serial output |
| No broker connection | Firewall blocks 8883; wrong scheme (`tls://` vs `tcp://`) |
| `ping 10.0.1.2` fails | Preshared key mismatch (`-k` vs sketch); tunnel IPs not on the same /24 |
| Tunnel up, no internet | Exit node missing `ip_forward=1` or MASQUERADE rule |
| Stalls after minutes | ESP8266 heap exhaustion — reduce NAPT / users / power-cycle |
| Many users, very slow | Normal: 0.3–1 Mbps **shared**; plan 3–5 comfortable users |

Debug: `mosquitto_sub -h BROKER -p 8883 -u vpnuser -P PASS -t 'mqttip/#' -d`,
Serial Monitor on the ESP, or `mqtt_vpn -d` on the exit node.

## 8. Honest limitations & responsibility

- **~0.3–1 Mbps throughput** — fine for browsing, painful for video.
- All tunnel traffic is encrypted with libnacl; the broker sees only traffic
  patterns, not contents. Packet replay at the MQTT layer is possible.
- One exit node = everyone shares its bandwidth and public IP. Publish a
  privacy policy and acceptable-use policy; you're responsible for what leaves
  your exit node.
- If you charge money you're a service provider — Dodo acts as Merchant of
  Record for tax, but check local regulations for operating proxy services.
- Bypassing a school/organization's filtering usually violates its
  acceptable-use policy. Know the rules you're working under.

## 9. Key rotation & uptime

- **Rotate keys:** change `vpn_password` on the exit node (`-k`) and in every
  ESP sketch, then reflash — revokes everyone at once.
- **Fairness:** the ESP has no per-user limits; run several ESPs as you grow.
- **Uptime:** the ESP can hang under load; a smart plug rebooting it nightly is
  a cheap fix.

## Credits

Tunnel firmware and Linux client by
[martin-ger/MQTT_VPN](https://github.com/martin-ger/MQTT_VPN). Hotspot
firmware, tiering and billing hooks in this repo.
