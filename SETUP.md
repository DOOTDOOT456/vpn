# Full Setup Guide: ESP8266 MQTT VPN for a Chromebook

This guide takes you from parts on a desk to a Chromebook browsing through an
MQTT-tunneled VPN. Follow the parts in order — each depends on the previous
one.

## Architecture overview

```
+-----------+   WiFi    +-----------+  MQTT (TLS)  +--------------+   Internet
| Chromebook|<--------->|  ESP8266  |<------------>| MQTT broker  |
+-----------+           | (NAT/tun) |              +------+-------+
                        +-----------+                     |
                                                    tunnels packets
                                                          |
                                                          v
                                                 +------------------+
                                                 | Linux exit node  |
                                                 | (mqtt_vpn + NAT) |
                                                 +------------------+
```

- **ESP8266** (`mqtt_vpn_nat` sketch): creates a WiFi access point, NATs client
  traffic into an IP-over-MQTT tunnel.
- **MQTT broker**: relays packets between ESP and exit node. Use one with TLS +
  authentication for real use (a censored network may block plain MQTT on 1883;
  TLS on 8883 looks like ordinary encrypted traffic).
- **Exit node**: Linux box running `mqtt_vpn`, with IP forwarding so tunneled
  packets go out to the real internet.

---

## Part 0 — Bill of materials

| Item | Example | ~Cost |
|---|---|---|
| ESP8266 dev board | NodeMCU v3 / Wemos D1 Mini | $3–5 |
| Micro-USB data cable | — | $2 |
| Linux exit node | Raspberry Pi, old laptop, or small VPS (Hetzner, Oracle free tier) | $0–5/mo |
| MQTT broker | HiveMQ Cloud free tier (TLS) **or** Mosquitto on the exit node | $0 |

> **Tip:** Simplest setup = run Mosquitto on the same Linux exit node. One box,
> one static address, no cloud account needed. The HiveMQ Cloud option is best
> if the exit node sits behind home NAT with no port forwarding.

---

## Part 1 — Set up the MQTT broker

Pick **one** option.

### Option A: Mosquitto on the exit node (recommended)

SSH into the exit node and install Mosquitto:

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
```

Create a password file:

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd vpnuser
# enter a strong password when prompted
```

Create `/etc/mosquitto/conf.d/vpn.conf`:

```
listener 8883
allow_anonymous false
password_file /etc/mosquitto/passwd

# TLS — generate a self-signed cert if you don't have a real one:
# sudo openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
#   -keyout /etc/mosquitto/certs/ca.key -out /etc/mosquitto/certs/ca.crt
certfile /etc/mosquitto/certs/ca.crt
keyfile  /etc/mosquitto/certs/ca.key
```

Restart and open the port:

```bash
sudo systemctl restart mosquitto
sudo ufw allow 8883/tcp   # skip if ufw isn't enabled
```

If the exit node is behind a home router, port-forward **TCP 8883** to it.
Note the node's public IP or hostname (e.g. via `curl ifconfig.me`).

> With a self-signed cert, the clients need `ca.crt`. The ESP8266 Arduino
> client in MQTT_VPN uses plain TCP by default; TLS support depends on the
> broker URL scheme it's compiled with. If you hit TLS issues on the ESP, you
> can run a plain-TCP listener on a high port (e.g. 8899) as a fallback — still
> VPN-encrypted by the preshared key, but the broker handshake is visible.

### Option B: HiveMQ Cloud (free, managed)

1. Sign up at hivemq.com → HiveMQ Cloud (Serverless free tier).
2. Create a cluster; note the **URL** (e.g. `abc123.s1.eu.hivemq.cloud:8883`).
3. Add credentials (username + password) under Access Management.
4. Allow connections from any IP (or restrict to your exit node).

---

## Part 2 — Prepare the Linux exit node

Works on Raspberry Pi OS, Ubuntu, Debian. On a VPS you'll SSH in; on a
Raspberry Pi at home, SSH or use a keyboard.

### 2.1 Install MQTT_VPN dependencies

```bash
sudo apt install -y git build-essential cmake
git clone https://github.com/martin-ger/MQTT_VPN.git
cd MQTT_VPN/linux
sudo chmod +x ./mqttVPNdependencyInstaller.sh
sudo ./mqttVPNdependencyInstaller.sh
```

This installs the Paho MQTT C library and libnacl, then builds the
`mqtt_vpn` binary in the current directory.

### 2.2 Enable IP forwarding and NAT

```bash
sudo sysctl -w net.ipv4.ip_forward=1
# make it permanent:
echo 'net.ipv4.ip_forward=1' | sudo tee /etc/sysctl.d/99-vpn.conf

# find your outbound interface (e.g. eth0, wlan0):
ip route get 1.1.1.1

# NAT tunneled traffic out to the internet (replace eth0):
sudo iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
```

Make the iptables rule survive reboots:

```bash
sudo apt install -y iptables-persistent
sudo netfilter-persistent save
```

### 2.3 Run the VPN client

Assuming the ESP will use `10.0.1.2` and the exit node `10.0.1.1` on the
tunnel (matching the defaults in the sketch):

```bash
sudo ./mqtt_vpn \
  -i mq0 \
  -a 10.0.1.1 \
  -b tls://YOUR_BROKER_HOST:8883 \
  -u vpnuser -p BROKER_PASSWORD \
  -k "your-vpn-preshared-key" \
  -d
```

Flags:
- `-i mq0` — name of the TUN interface it creates
- `-a 10.0.1.1` — this box's IP inside the tunnel
- `-b` — broker URL (`tls://` for TLS, `tcp://` for plain)
- `-u` / `-p` — broker credentials
- `-k` — the VPN preshared key (**must match the ESP's**)
- `-d` — debug output (drop it once it works)
- `-t <ip>` — optional: NAT a specific target behind the ESP (used with the
  `mqtt_vpn_nat` sketch to reach a host on the ESP's WiFi LAN)

Leave it running. For a persistent setup, create a systemd unit:

`/etc/systemd/system/mqtt-vpn.service`:

```ini
[Unit]
Description=MQTT VPN tunnel client
After=network-online.target

[Service]
ExecStart=/home/pi/MQTT_VPN/linux/mqtt_vpn -i mq0 -a 10.0.1.1 -b tls://YOUR_BROKER_HOST:8883 -u vpnuser -p BROKER_PASSWORD -k your-vpn-preshared-key
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now mqtt-vpn
```

---

## Part 3 — Flash the ESP8266

### 3.1 Install the toolchain

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. **File → Preferences → Additional Board Manager URLs**, add:
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. **Tools → Board → Boards Manager**, search "esp8266", install
   **esp8266 by ESP8266 Community**.
4. Select your board under **Tools → Board** (e.g. "NodeMCU 1.0").

### 3.2 Install the MQTT_VPN library

Download the repo ZIP from GitHub, extract it, and copy the
`mqtt_vpn_arduino` folder into your Arduino libraries folder
(`~/Arduino/libraries/` on Linux/macOS, `Documents\Arduino\libraries\` on
Windows). Restart the IDE.

### 3.3 Option A (recommended): Community hotspot firmware

Use the ready-made sketch in this repo:
[`firmware/mqtt_vpn_hotspot.ino`](firmware/mqtt_vpn_hotspot.ino). It turns the
ESP8266 into a **multi-user hotspot**: it opens its own WiFi AP that anyone
with the passphrase can join and NATs every client behind the ESP's single
tunnel IP, so the whole community shares one tunnel without any per-device
setup.

1. Copy the file into a sketch folder of the same name
   (`mqtt_vpn_hotspot/mqtt_vpn_hotspot.ino`) and open it in the IDE.
2. Edit the CONFIG section:
   - **Uplink SSID/password** — the WiFi network the ESP uses to reach the
     broker (this network must not block your broker port).
   - **Hotspot SSID/password** — what the community connects to. Passphrase
     must be ≥ 8 chars. An open hotspot (`""`) works but lets anyone in range
     use your tunnel — don't.
   - **Broker** — address, port, credentials.
   - **`vpn_password`** — must match the `-k` on the exit node.
   - Leave `mqtt_vpn_addr` at `10.0.1.2` unless you changed the exit node.
3. Flash and watch the Serial Monitor (115200). The heartbeat line shows
   uptime, free heap, and how many clients are connected.

Why every user fits: hotspot clients are NATed behind `10.0.1.2`, so the
broker sees one topic pair regardless of user count — the library's
8-topic limit does not cap users. Real limits are the ESP8266's radio and
~0.3–1 Mbps shared bandwidth: plan for **3–5 comfortable simultaneous users**.

The stock `mqtt_vpn_nat` example (Option B below) is a single-client demo and
is only useful for reaching one specific host behind the ESP — skip it unless
you need that special case.

### 3.4 Option B (stock demo): mqtt_vpn_nat example

Open **File → Examples → mqtt_vpn_arduino → mqtt_vpn_nat** and edit the
constants at the top:

- WiFi STA credentials — SSID/password the ESP connects *to* (your home WiFi,
  or a phone hotspot)
- `mqtt_vpn_addr` — the ESP's tunnel IP, e.g. `10.0.1.2`
- Broker address, username, password
- VPN preshared key — **must match the `-k` value on the exit node**
- Target host IP (`172.16.0.100` by default) — the device on the ESP's WiFi
  side that the exit node should be able to reach

> If you'd rather have the Chromebook connect *to* the ESP as its own hotspot,
> the sketch's SoftAP mode provides that; the WiFi AP credentials are also in
> the sketch.

Click **Upload** with the board plugged in. Open **Tools → Serial Monitor** at
115200 baud to watch it connect to WiFi and the broker.

### 3.5 Test the tunnel

From the exit node:

```bash
ping 10.0.1.2        # ping the ESP through the tunnel
telnet 10.0.1.2 23   # if using the telnet sample
```

If pings work, the broker path, keys, and IPs all line up.

---

## Part 4 — Connect the Chromebook (and everyone else)

### Option A: Join the ESP's community hotspot (no per-device config)

With the `mqtt_vpn_hotspot` firmware, every device just joins the ESP's WiFi
like any other network — DHCP hands out addresses, gateway, and routing
automatically. Users never touch IP settings.

1. On the Chromebook: **Settings → Network → Wi-Fi**, join the ESP's AP
   (default `CommunityVPN`), enter the passphrase.
2. That's it. Verify below.

To confirm exit:

```
curl ifconfig.me     # from the Chromebook (crosh shell) — should return
                     # the EXIT NODE's public IP, not the local network's
```

### Option B: Manual static gateway (single device, no hotspot firmware)

1. On the Chromebook: **Settings → Network → Wi-Fi**, join the ESP's AP.
2. Click the network → configure IP manually:
   - IP: an address in the ESP's AP subnet (e.g. `172.16.0.101`)
   - Netmask: `255.255.255.0`
   - Gateway: the ESP's AP address (e.g. `172.16.0.100` — adapt to your sketch)
3. DNS: set to a public resolver like `1.1.1.1` or `8.8.8.8`.

### Option C: ESP joins your existing network (STA mode)

If the ESP is in STA mode on your normal WiFi, it will route only traffic
explicitly addressed to the tunnel (`10.0.1.x`). To push all Chromebook
traffic through it you'd need the ESP to also run DHCP/default-route service,
which the stock sketch doesn't fully do. Option A is the reliable path.

### Verify

From the Chromebook's browser or the crosh shell (`Ctrl+Alt+T`, type `shell`,
then `tracepath -n 1.1.1.1`): the first hops should show the ESP and tunnel
addresses, and sites should load via the exit node.

To confirm exit:

```
curl ifconfig.me     # from the Chromebook (crosh shell) — should return
                     # the EXIT NODE's public IP, not the local network's
```

---

## Part 5 — Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| ESP won't join WiFi | Wrong SSID/password; 5 GHz network (ESP8266 is 2.4 GHz only) |
| No broker connection | Broker unreachable from the local network (firewall blocks 8883); wrong URL scheme (`tls://` vs `tcp://`) |
| Ping 10.0.1.2 fails | Preshared key mismatch between `-k` and sketch; tunnel IPs not on the same /24 |
| Tunnel up but no internet from Chromebook | Exit node missing `ip_forward=1` or the MASQUERADE iptables rule |
| Works for a few minutes then stalls | ESP8266 heap exhaustion — lower the `NAPT` value in the firmware, reduce concurrent users, or restart the ESP |
| Many users, very slow | Normal: 0.3–1 Mbps is **shared** by everyone; plan for 3–5 comfortable users |
| School network blocks everything | Filter may block TLS-MQTT too; try the broker on port 443 or a different host/port |

### Debug checklist

1. `mosquitto_sub -h YOUR_BROKER -p 8883 -u vpnuser -P PASS -t 'mqttip/#' -d`
   on any machine — you should see packets flowing when the ESP is active.
2. Serial Monitor on the ESP (115200) shows MQTT connect state.
3. `sudo ./mqtt_vpn ... -d` on the exit node prints each tunneled packet.

---

## Running it as a community VPN

- **Share the hotspot passphrase** (and WiFi SSID) with members; nothing on
  their devices needs configuring.
- **One exit node, everyone shares its bandwidth and public IP.** Anything a
  member does online looks like it came from the exit node — set ground rules
  with your community, and consider an acceptable-use agreement.
- **Key rotation:** change `vpn_password` on the exit node (`-k`) and in every
  ESP sketch, then reflash, to revoke access for everyone at once.
- **Fairness:** the ESP has no per-user bandwidth limits; heavy users slow
  everyone. Consider running several ESPs with separate tunnels if you grow.
- **Uptime:** the ESP can hang under heavy load. A smart plug that reboots it
  nightly is a cheap fix, or add a watchdog to the firmware.

---

## Security notes

- All tunnel traffic is encrypted/authenticated with libnacl using the
  preshared key — the broker operator cannot read it, only see traffic
  patterns.
- Change the default password `secret` to something long and random.
- The broker sees *who talks to whom and how much*, not the contents.
- Replay of individual packets is possible at the MQTT layer; don't use this
  for anything requiring strong guarantees.
- Bypassing a school/employer's network filtering usually violates their
  acceptable-use policy — understand the consequences before deploying.
