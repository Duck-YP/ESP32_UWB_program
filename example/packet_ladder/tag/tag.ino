/*
  TAG（ESP32 UWB / UWB Pro）
  — 設計原則同 ANCHOR：ISR 入列、loop() 做 UDP；完成量測送出 RANGE。
*/

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>

// ========= 網路 / 主機 =========
#define HOST_IP     IPAddress(172,20,10,3)
#define EVT_PORT    9100
#define HOST_PORT   9000

// ========= 你的環境（EUI / Wi-Fi / 腳位）=========
#define TAG_EUI "7D:00:22:EA:82:60:3B:9C"
const char* WIFI_SSID = "Yi Ping";
const char* WIFI_PW   = "ypypypyp";

#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23
#define DW_CS      4
const uint8_t PIN_RST = 27;
const uint8_t PIN_IRQ = 34;
const uint8_t PIN_SS  = 4;

// ========= 功能開關 =========
#define ENABLE_UDP_RANGE 1
#define HEARTBEAT_MS     2000

WiFiUDP UWB_UDP;   // 距離
WiFiUDP UWB_EVT;   // 事件

// ========= ISR → 主迴圈：無鎖環形佇列 =========
struct EventRec {
  char kind;                      // 'T' = TX, 'R' = RX
  unsigned long long dev_ts;      // 裝置層時間戳
};
static volatile EventRec q[64];
static volatile uint8_t q_head = 0, q_tail = 0;

static inline void isr_push(char k, unsigned long long ts) {
  uint8_t nx = (uint8_t)((q_head + 1) & 63);
  if (nx == q_tail) return;       // 滿了就丟
  q[q_head].kind = k;
  q[q_head].dev_ts = ts;
  q_head = nx;
}
static bool pop(EventRec &out) {
  if (q_tail == q_head) return false;
  out.kind = q[q_tail].kind;
  out.dev_ts = q[q_tail].dev_ts;
  q_tail = (uint8_t)((q_tail + 1) & 63);
  return true;
}

// 小工具：抓裝置層時戳
static inline unsigned long long get_tx_ts(){
  DW1000Time t; DW1000.getTransmitTimestamp(t);
  return (unsigned long long)t.getTimestamp();
}
static inline unsigned long long get_rx_ts(){
  DW1000Time t; DW1000.getReceiveTimestamp(t);
  return (unsigned long long)t.getTimestamp();
}

// 回呼宣告
void IRAM_ATTR on_tx_done();
void IRAM_ATTR on_rx_done();
void newRange();
void newDevice(DW1000Device *dev);
void inactiveDevice(DW1000Device *dev);

// 心跳
unsigned long last_hb = 0;

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("### TAG ###");

  // DW1000
  // ===== DW1000 初始化 =====
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ);
  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  // !!! 先進入 Tag 模式
  DW1000Ranging.startAsTag(TAG_EUI, DW1000.MODE_LONGDATA_RANGE_LOWPOWER);

  // !!! 再次綁定 & 開中斷（避免被覆蓋）
  DW1000.attachSentHandler(on_tx_done);
  DW1000.attachReceivedHandler(on_rx_done);
  DW1000.interruptOnSent(true);
  DW1000.interruptOnReceived(true);

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PW);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(400); }
  Serial.println();
  Serial.print("WiFi OK, IP="); Serial.println(WiFi.localIP());

  // UDP
  UWB_UDP.begin(0);
  UWB_EVT.begin(0);
}

void loop() {
  DW1000Ranging.loop();

  // 將 ISR 佇列中的事件搬到 UDP（loop 做 I/O）
  EventRec r;
  while (pop(r)) {
    char buf[128];
    // 關鍵修正：用 %s 直接輸出 "TX"/"RX"，不再使用 '%c%c' 小技巧
    int n = snprintf(buf, sizeof(buf),
                     "role=TAG evt=%s dev_ts=%llu\n",
                     (r.kind=='T' ? "TX" : "RX"),
                     r.dev_ts);
    UWB_EVT.beginPacket(HOST_IP, EVT_PORT);
    UWB_EVT.write((const uint8_t*)buf, n);
    UWB_EVT.endPacket();
  }

  // 心跳
  unsigned long now = millis();
  if (now - last_hb >= HEARTBEAT_MS) {
    last_hb = now;
    char hb[64];
    snprintf(hb, sizeof(hb), "role=TAG evt=HEARTBEAT host_ts=%lu\n", now/1000);
    UWB_EVT.beginPacket(HOST_IP, EVT_PORT);
    UWB_EVT.write((const uint8_t*)hb, strlen(hb));
    UWB_EVT.endPacket();
  }
}

// 完成一次量測：送出 RANGE（RANGE 後備配對）
void newRange() {
  DW1000Device* dev = DW1000Ranging.getDistantDevice();
  if (!dev) return;

  float m = dev->getRange();
  float rssi = dev->getRXPower();

  Serial.print("from: "); Serial.print(dev->getShortAddress(), HEX);
  Serial.print("\t Range: "); Serial.print(m, 2);
  Serial.print(" m\t RX power: "); Serial.print(rssi, 2); Serial.println(" dBm");

  // RANGE 事件（給 Python 後備配對）
  char e[96];
  unsigned long sec = millis() / 1000;
  snprintf(e, sizeof(e), "role=TAG evt=RANGE host_ts=%lu\n", sec);
  UWB_EVT.beginPacket(HOST_IP, EVT_PORT);
  UWB_EVT.write((const uint8_t*)e, strlen(e));
  UWB_EVT.endPacket();

#if ENABLE_UDP_RANGE
  char msg[64];
  snprintf(msg, sizeof(msg), "Range: %.2f m\n", m);
  UWB_UDP.beginPacket(HOST_IP, HOST_PORT);
  UWB_UDP.write((const uint8_t*)msg, strlen(msg));
  UWB_UDP.endPacket();
#endif
}

void newDevice(DW1000Device *dev) {
  Serial.print("ranging init; 1 device added ! -> short:");
  Serial.println(dev->getShortAddress(), HEX);
}
void inactiveDevice(DW1000Device *dev) {
  Serial.print("delete inactive device: ");
  Serial.println(dev->getShortAddress(), HEX);
}

// ====== ISR（只入列，不做 I/O）======
// void IRAM_ATTR on_tx_done() { isr_push('T', get_tx_ts()); }
// void IRAM_ATTR on_rx_done() { isr_push('R', get_rx_ts()); }
void IRAM_ATTR on_tx_done(){
  DW1000Ranging.appHandleSent();
  isr_push('T', get_tx_ts());
}
void IRAM_ATTR on_rx_done(){
  DW1000Ranging.appHandleReceived();
  isr_push('R', get_rx_ts());
}

