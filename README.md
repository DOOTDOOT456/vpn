# ESP8266 MQTT VPN — Community Hotspot

Turn a $3 ESP8266 (WeMos D1 Mini / NodeMCU) into a tiny VPN hotspot. Users
join its WiFi and all their traffic is tunneled through an MQTT broker to a
Raspberry Pi exit node on an uncensored network. Based on
[martin-ger/MQTT_VPN](https://github.com/martin-ger/MQTT_VPN).

```
+-----------+   WiFi    +-----------+  MQTT (TLS)  +--------------+   Internet
| Chrome-   |<--------->|  WeMos    |<------------>| MQTT broker  |
| book      |  hotspot  |  ESP8266  |              +------+-------+
+-----------+           | (NAT/tun) |                     |
                        +-----------+               tunnels packets
                                                            |
                                                            v
                                                   +------------------+
                                                   | Raspberry Pi     |
                                                   | (mqtt_vpn + NAT) |
                                                   +------------------+
```

---

# Devices you need

| Device | Role | ~Cost |
|---|---|---|
| **WeMos D1 Mini** (or any ESP8266: NodeMCU v3 etc.) + micro-USB data cable | The hotspot: broadcasts its own WiFi, tunnels everyone through the broker | $3–5 |
| **Raspberry Pi** (any model, ideally 3/4/Zero 2 W) + SD card | The exit node **and** the MQTT broker — all traffic exits here | $0–35 (use one you have) |
| **Chromebook** (or any laptop/phone) | The client — just joins the hotspot WiFi | — |

> One computer plays two roles: the Raspberry Pi runs **both** the MQTT broker
> (Mosquitto) and the tunnel client (`mqtt_vpn`). That keeps the whole setup
> on two devices + one charger.

Optional: a micro-USB **power** adapter for the WeMos (it can't run off the Pi
reliably), and the PC you'll use to flash the WeMos (any laptop works, even
the Chromebook itself via Linux mode).

---

# Step 1 — Set up the Raspberry Pi (broker + exit node)

You'll need the Pi running Raspberry Pi OS (Lite is fine), reachable via
SSH or a keyboard/screen. Everything below happens **on the Pi**.

### 1.1 Install the MQTT broker (Mosquitto)

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
sudo mosquitto_passwd -c /etc/mosquitto/passwd vpnuser
# → type a strong password twice. Remember it: it's BROKER_PASS everywhere.
```

Create `/etc/mosquitto/conf.d/vpn.conf`:

```
listener 8883
allow_anonymous false
password_file /etc/mosquitto/passwd
certfile /etc/mosquitto/certs/ca.crt
keyfile  /etc/mosquitto/certs/ca.key
```

Generate a free self-signed TLS certificate:

```bash
sudo mkdir -p /etc/mosquitto/certs
sudo openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
  -keyout /etc/mosquitto/certs/ca.key -out /etc/mosquitto/certs/ca.crt \
  -subj "/CN=vpn-broker"
```

Start it and open the firewall:

```bash
sudo systemctl restart mosquitto
sudo ufw allow 8883/tcp    # skip if ufw is not enabled
```

### 1.2 Give the Pi a reachable address

- **Pi at home:** enable SSH, then in your **router** forward TCP 8883 to the
  Pi. Note your home public IP (`curl ifconfig.me`) — or set up a free
  dynamic-DNS name at duckdns.org if your IP changes.
- **Pi at a site with an open network:** the network's IP may be all you need.

This address is your **BROKER_HOST** used in Step 2 and Step 3.

### 1.3 Build the VPN client

```bash
sudo apt install -y git build-essential cmake
git clone https://github.com/martin-ger/MQTT_VPN.git
cd MQTT_VPN/linux
sudo ./mqttVPNdependencyInstaller.sh      # builds ./mqtt_vpn
```

### 1.4 Enable IP forwarding + NAT

```bash
echo 'net.ipv4.ip_forward=1' | sudo tee /etc/sysctl.d/99-vpn.conf
sudo sysctl -p /etc/sysctl.d/99-vpn.conf
ip route get 1.1.1.1                       # note the outbound iface, e.g. wlan0/eth0
sudo iptables -t nat -A POSTROUTING -o wlan0 -j MASQUERADE   # use your iface
sudo apt install -y iptables-persistent && sudo netfilter-persistent save
```

### 1.5 Run the tunnel client

Pick a **VPN preshared key** — any long random string, e.g.
`openssl rand -hex 32`. It must match the ESP's `vpn_password` (Step 2).

```bash
sudo ./mqtt_vpn \
  -i mq0 \
  -a 10.0.1.1 \
  -b tls://BROKER_HOST:8883 \
  -u vpnuser -p BROKER_PASSWORD \
  -k "your-preshared-key" -d
```

Leave it running (`-d` prints packets — drop that flag once it works).

**Make it permanent** — `/etc/systemd/system/mqtt-vpn.service`:

```ini
[Unit]
Description=MQTT VPN tunnel client
After=network-online.target

[Service]
ExecStart=/home/pi/MQTT_VPN/linux/mqtt_vpn -i mq0 -a 10.0.1.1 -b tls://BROKER_HOST:8883 -u vpnuser -p BROKER_PASSWORD -k your-preshared-key
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now mqtt-vpn
```

**Sanity check on the Pi:** `mosquitto_sub -h localhost -t 'mqttip/#' -d`
should show traffic once the ESP is connected (Step 2).

---

# Step 2 — Set up the WeMos D1 Mini (ESP8266 hotspot)

You need a **PC/laptop with the Arduino IDE** to flash it. It takes ~10
minutes; you only do this once (and again whenever you change the key or
WiFi).

### 2.1 Install the toolchain (on the flashing PC)

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. **File → Preferences → Additional Board Manager URLs**, add:
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. **Tools → Board → Boards Manager** → install **esp8266 by ESP8266
   Community** (needs version 3.x or newer for enterprise WiFi support).
4. **Tools → Board** → select **LOLIN(WEMOS) D1 R2 & mini** (or NodeMCU 1.0).

### 2.2 Install the MQTT_VPN Arduino library

Download the [MQTT_VPN repo ZIP](https://github.com/martin-ger/MQTT_VPN),
unzip it, and copy the `mqtt_vpn_arduino` folder into your Arduino libraries
folder (`~/Arduino/libraries/` on Linux/macOS,
`Documents\Arduino\libraries\` on Windows). Restart the IDE.

### 2.3 Configure and flash the sketch

1. Put [`code/firmware/mqtt_vpn_hotspot.ino`](code/firmware/mqtt_vpn_hotspot.ino)
   in a folder of the same name (`mqtt_vpn_hotspot/mqtt_vpn_hotspot.ino`) and
   open it in the IDE.
2. Edit the **CONFIG** block at the top:

   | Setting | What to put |
   |---|---|
   | `UPLINK_SSID` / `UPLINK_PASSWORD` | The WiFi the **WeMos itself** joins to reach the broker (e.g. your home WiFi, or a phone hotspot). Must not block port 8883. |
   | `UPLINK_USES_ENTERPRISE` | `1` only if that WiFi needs a **username + password** login (school/campus 802.1X) — then also set `UPLINK_USERNAME`. |
   | `ap_ssid` / `ap_password` | The hotspot **users** join. Password ≥ 8 chars; never leave it open. |
   | `broker` / `broker_port` | `BROKER_HOST` from Step 1.2, port `8883`. |
   | `broker_username` / `broker_password` | `vpnuser` + the password from Step 1.1. |
   | `vpn_password` | The **same preshared key** as `-k` on the Pi (Step 1.5). |
   | `mqtt_vpn_addr` | Leave at `10.0.1.2` unless you changed the Pi. |

3. Plug the WeMos into the PC with a **data** micro-USB cable, pick the port
   under **Tools → Port**, and click **Upload**.
4. Open **Tools → Serial Monitor** at **115200** baud. You should see it join
   the uplink WiFi, open the hotspot, and start the tunnel.

> Why everyone fits on one ESP: hotspot clients are NATed behind
> `10.0.1.2`, so the broker sees one topic pair no matter how many users.
> The real limits are the ESP8266's radio and ~0.3–1 Mbps **shared**
> bandwidth — plan for 3–5 comfortable simultaneous users.

### 2.4 Power it

After flashing, unplug from the PC and power the WeMos from any USB charger /
power bank (5V, ≥ 500 mA). Place it near where users will be.

---

# Step 3 — Set up the Chromebook (client)

Nothing to install. Each user:

1. **Settings → Network → Wi-Fi** → join the hotspot from Step 2
   (default `CommunityVPN`), enter the hotspot password.
2. That's it — DHCP hands out addresses and routing automatically.

**Verify it works** — open Chrome, or press `Ctrl+Alt+T` → type `shell` → run:

```
curl ifconfig.me
```

It should return the **Raspberry Pi's public IP** (Step 1.2), not the local
network's. If it does, all traffic is exiting through the Pi.

---

# Quick checklist

- [ ] Pi: Mosquitto installed, password created, TLS cert generated, port 8883 reachable from outside
- [ ] Pi: `mqtt_vpn` built, `ip_forward=1`, MASQUERADE rule, service running
- [ ] WeMos: flashed with matching broker address/credentials and the **same** preshared key as the Pi
- [ ] Chromebook: joins hotspot → `curl ifconfig.me` shows the Pi's IP

# Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| WeMos won't join uplink WiFi | Wrong SSID/password; 5 GHz network (ESP8266 is 2.4 GHz only); campus WiFi needing username/password → set `UPLINK_USES_ENTERPRISE 1` + `UPLINK_USERNAME` |
| Enterprise auth fails | Wrong username/password or anonymous identity; check Serial output at 115200 |
| No broker connection | Port 8883 not forwarded/open; wrong `tls://` vs `tcp://`; HiveMQ-style cloud broker required but not used |
| Self-signed cert rejected | Linux client must trust `ca.crt`; the ESP client in MQTT_VPN uses plain TCP — if TLS fails on the ESP, add a plain listener on port 8899 (tunnel contents still encrypted by the preshared key) |
| `ping 10.0.1.2` from Pi fails | Preshared key mismatch (`-k` vs `vpn_password`); tunnel IPs not in the same /24 |
| Chromebook online but `curl ifconfig.me` shows local IP | Pi missing `ip_forward=1` or the MASQUERADE rule |
| Works, then stalls after minutes | ESP8266 heap exhaustion — power-cycle, reduce users, or lower NAPT entries |
| Many users, very slow | Normal: 0.3–1 Mbps is **shared**; plan 3–5 comfortable users |

Debug: `mosquitto_sub` on the Pi shows packets flowing; Serial Monitor (115200)
shows the ESP's WiFi/broker state; `mqtt_vpn -d` prints each tunneled packet.

---

# Tiers — free vs. paid (optional)

Both tiers use the same devices; the tier only changes **which preshared key**
devices use, which maps to quality of service on the Pi.

| | **Free** | **Supporter ($5/mo)** |
|---|---|---|
| Tunnel access | ✅ Standard key | ✅ Priority key |
| Bandwidth | Best-effort, deprioritized | QoS priority on the Pi |
| Sessions | 2 devices | 5 devices |
| Support | Community | Priority |

Payments run through **Dodo Payments** (Merchant of Record — handles sales
tax/VAT). The webhook receiver in
[`code/webhook/webhook_server.py`](code/webhook/webhook_server.py) provisions
a unique `vip-<random>` key on subscription and deactivates it on expiry — see
[`docs/dodo-webhook-receiver.md`](docs/dodo-webhook-receiver.md).

To run both tiers you need a **second WeMos** (an ESP hotspot can only hold
one key/SSID): flash one with the standard key (SSID `CommunityVPN`, topic
prefix `mqttip`), one with the priority key (SSID `CommunityVPN-VIP`, prefix
`mqttipvip`), and run **two** `mqtt_vpn` instances on the Pi
(`-i mq0 -a 10.0.1.1` and `-i mq1 -a 10.0.2.1`). Prioritize paid traffic:

```bash
sudo tc qdisc add dev eth0 root handle 1: prio bands 3
sudo tc filter add dev eth0 parent 1: protocol ip prio 1 u32 match ip iif mq1 0 0 flowid 1:1
sudo tc filter add dev eth0 parent 1: protocol ip prio 3 u32 match ip iif mq0 0 0 flowid 1:3
sudo tc qdisc add dev mq0 root tbf rate 4mbit burst 32kbit latency 50ms  # cap free tier
```

(Use your real outbound interface instead of `eth0`.)

---

# Honest limitations & responsibility

- **~0.3–1 Mbps shared throughput** — fine for browsing, painful for video.
- All tunnel traffic is encrypted with libnacl; the broker sees only traffic
  patterns, not contents. MQTT-layer packet replay is possible.
- **Everything exits from the Pi** — its IP, its bandwidth, its
  responsibility. Publish a privacy policy and acceptable-use policy.
- If you charge money you're a service provider — check local regulations
  (Dodo handles sales tax as Merchant of Record).
- Bypassing a school/organization's filtering usually violates its
  acceptable-use policy. Know the rules you're working under.
- **Key rotation:** change `vpn_password` in the sketch + `-k` on the Pi and
  reflash to revoke everyone at once.
- **Uptime:** the ESP can hang under load; a smart plug that power-cycles it
  nightly is a cheap fix.

# Policies — protecting your home and your users

Publish the first two to your users; follow the third yourself before going
live:

- [policy/PRIVACY.md](policy/PRIVACY.md) — what the operator can and cannot
  see, logging practices, and legal-request handling.
- [policy/ACCEPTABLE_USE.md](policy/ACCEPTABLE_USE.md) — rules users must
  agree to; everything that exits the Pi is the operator's responsibility.
- [policy/PI-HARDENING.md](policy/PI-HARDENING.md) — step-by-step hardening:
  SSH lockdown, firewalls, **blocking tunnel users from reaching your home
  LAN**, WiFi isolation, auto-updates, and abuse monitoring.

## Credits

Tunnel firmware and Linux client by
[martin-ger/MQTT_VPN](https://github.com/martin-ger/MQTT_VPN). Hotspot
firmware, tiering and billing hooks in this repo.
