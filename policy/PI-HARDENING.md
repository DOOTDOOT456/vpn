# Pi & Home Network Hardening Guide

A VPN exit node forwards **strangers' traffic through your home network**.
Harden the Raspberry Pi before going live. Every step below is on the Pi,
unless noted.

## 1. Lock down SSH

```bash
# On your PC: copy your key first, then disable password login
ssh-copy-id pi@<pi-ip>

sudo nano /etc/ssh/sshd_config
#   PasswordAuthentication no
#   PermitRootLogin no
sudo systemctl restart ssh
```

Verify key login works from your PC **before** closing the current session.

## 2. Firewall: only what's needed

```bash
sudo apt install -y ufw
sudo ufw default deny incoming
sudo ufw allow 8883/tcp    # MQTT broker (TLS)
sudo ufw allow ssh         # from your LAN ideally — see below
sudo ufw enable
```

If your router supports it, forward TCP 8883 **only** (not SSH, not the web
UIs of any other device). Better: many routers can't forward to arbitrary
ports safely — if you can, put the broker behind a VPN or use a cheap VPS
with a WireGuard tunnel back home instead of exposing the home network.

## 3. Protect the home LAN from tunnel users

The Pi routes **internet-bound** traffic out, but you must make sure tunnel
clients cannot reach your home devices (NAS, cameras, other PCs).

```bash
# Block tunnel clients from reaching private ranges except the tunnel itself.
# Replace eth0 with your real outbound interface and mq0 with the tunnel iface.
sudo iptables -A FORWARD -i mq0 -d 192.168.0.0/16 -j DROP
sudo iptables -A FORWARD -i mq0 -d 10.0.0.0/8 ! -d 10.0.1.0/24 -j DROP
sudo iptables -A FORWARD -i mq0 -d 172.16.0.0/12 -j DROP
sudo netfilter-persistent save
```

If your home LAN uses 192.168.x.x, this guarantees hotspot users can only
talk to the internet — never to your other devices.

## 4. Isolate the hotspot WiFi from your home WiFi

Never let the WeMos bridge to the same network your personal devices trust.
Options, best first:

1. **Dedicated "guest" SSID** on your router (client isolation enabled) and
   put the WeMos uplink + broker access there.
2. A **second cheap router** or a VLAN for the hotspot gear.
3. At minimum, rely on the iptables rules in §3.

## 5. Keep the system patched and supervised

```bash
sudo apt install -y unattended-upgrades
sudo dpkg-reconfigure -plow unattended-upgrades   # choose "yes"
```

- Update regularly: `sudo apt update && sudo apt upgrade`.
- Reboot after kernel updates: `sudo reboot`.
- Watch disk/logs occasionally: `df -h`, `journalctl -p err -b`.

## 6. Fail2ban (optional but cheap)

```bash
sudo apt install -y fail2ban
sudo systemctl enable --now fail2ban
```

Bans IPs that hammer SSH or other exposed services.

## 7. Change all default secrets

- [ ] Broker password (`mosquitto_passwd`) — long and random
- [ ] VPN preshared key (`-k` / `vpn_password`) — `openssl rand -hex 32`
- [ ] Hotspot SSID/password (`ap_password`) — ≥ 12 chars
- [ ] Pi user password (even with key-only SSH) — `passwd`
- [ ] Default `pi` user: rename it (`sudo adduser <you> && sudo deluser -remove-home pi`)

## 8. Physical & power safety

- Put the Pi and WeMos somewhere cool and out of reach of hotspot users.
- A smart plug that power-cycles the WeMos nightly prevents lockups.
- Back up `/etc/mosquitto/passwd`, the TLS certs, and your systemd unit —
  they're the only irreplaceable bits.

## 9. Rate-limit and watch for abuse

Cap hotspot abuse by capping free-tier bandwidth (see README §Tiers) and
periodically check what's going out:

```bash
sudo iftop -i eth0        # live traffic — who's using the exit
sudo ss -tupn             # active connections
```

If your ISP sends an abuse complaint, you'll see it — take the exit node
down, rotate the key, and re-admit only people you trust.
