/*
 * mqtt_vpn_hotspot — Community VPN hotspot for ESP8266
 * Based on martin-ger/MQTT_VPN (mqtt_vpn_nat / RangeExtender-NAPT).
 *
 * The ESP8266:
 *   - connects as a station (STA) to an existing WiFi network (the uplink),
 *   - opens its own WiFi hotspot (SoftAP) that anyone with the passphrase
 *     can join,
 *   - NATs ALL hotspot clients behind the ESP's single MQTT-VPN tunnel IP,
 *   - tunnels the traffic through an MQTT broker to a Linux exit node with
 *     internet access.
 *
 * Because every hotspot client is masqueraded behind the one tunnel address
 * (10.0.1.2), the broker only ever sees one topic pair — the number of
 * hotspot users is NOT limited by the library's 8-topic subscription limit.
 *
 * Edit every "..." line in the CONFIG section before flashing.
 */

#include <ESP8266WiFi.h>
#include <mqttif.h>
#include <lwip/napt.h>
#include <lwip/dns.h>

/* ------------------------- CONFIG — edit these ------------------------- */

// Uplink network: the WiFi the ESP uses to reach the broker.
// NOTE: this network must NOT block the broker port (e.g. 8883).
const char* uplink_ssid     = "...";
const char* uplink_password = "...";

// Hotspot opened by the ESP for everyone.
// ap_password must be >= 8 chars, or use "" for an OPEN hotspot (anyone in
// range can use your tunnel — not recommended).
const char* ap_ssid     = "CommunityVPN";
const char* ap_password = "changeme8";

// MQTT broker
char* broker            = "...";        // hostname or IP
int   broker_port       = 1883;         // use 8883 for TLS if your broker has it
char* broker_username   = "...";
char* broker_password   = "...";
char* broker_topic_prefix = "mqttip";

// VPN preshared key — MUST match the -k value on the Linux exit node.
char* vpn_password = "secret";

// The ESP's address inside the VPN tunnel. Must be in the same /24 as the
// exit node's -a address (10.0.1.1 by default).
IPAddress mqtt_vpn_addr(10, 0, 1, 2);

/* ---------------------------- end CONFIG ------------------------------- */

/* NAT translation table size. Bigger = more concurrent connections
 * (users × tabs) but more RAM. 1000 is a good balance on an ESP8266. */
#define NAPT 1000
#define NAPT_PORT 10

struct mqtt_if_data *my_if;

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.printf("Heap at boot: %d\r\n", ESP.getFreeHeap());

  /* 1. Bring up the hotspot for the community FIRST */
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("Hotspot '");
  Serial.print(ap_ssid);
  Serial.print("' up, AP IP: ");
  Serial.println(WiFi.softAPIP().toString());

  /* 2. Connect the uplink (path to the broker) */
  Serial.print("Connecting uplink to ");
  Serial.println(uplink_ssid);
  WiFi.begin(uplink_ssid, uplink_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Uplink connected, IP: ");
  Serial.println(WiFi.localIP().toString());

  /* Use a public DNS resolver (reached through the tunnel) so clients'
   * DNS queries are not answered or hijacked by the local network. */
  ip_addr_t dns_ip;
  IP_ADDR(&dns_ip, 1, 1, 1, 1);
  dns_setserver(0, &dns_ip);

  /* 3. Bring up the IP-over-MQTT tunnel interface, then enable NAPT so every
   *    hotspot client is masqueraded behind mqtt_vpn_addr. */

  /* gw = 10.0.1.1 makes the tunnel the default route: ALL client traffic
   * (not just 10.0.1.x) is pushed into the VPN. */
  my_if = mqtt_if_init(broker, broker_username, broker_password, broker_port,
                       broker_topic_prefix, vpn_password, mqtt_vpn_addr,
                       IPAddress(255, 255, 255, 0), IPAddress(10, 0, 1, 1));

  Serial.printf("Heap after tunnel init: %d\r\n", ESP.getFreeHeap());

  err_t ret = ip_napt_init(NAPT, NAPT_PORT);
  Serial.printf("ip_napt_init(%d,%d): ret=%d\r\n", NAPT, NAPT_PORT, (int)ret);
  if (ret == ERR_OK) {
    ret = ip_napt_enable(mqtt_vpn_addr, 1);
    Serial.printf("ip_napt_enable: ret=%d\r\n", (int)ret);
    if (ret == ERR_OK) {
      Serial.printf("All '%s' clients are now NATed behind the tunnel\r\n",
                    ap_ssid);
    }
  }
  if (ret != ERR_OK) {
    Serial.println("NAPT initialization FAILED");
  }
}

void loop() {
  /* Heartbeat so you can watch it in the Serial Monitor */
  static uint32_t last = 0;
  if (millis() - last > 10000) {
    last = millis();
    Serial.printf("up=%lus heap=%u sta=%d clients=%d\r\n",
                  millis() / 1000, ESP.getFreeHeap(),
                  WiFi.status() == WL_CONNECTED,
                  WiFi.softAPgetStationNum());
  }
}
