/*
  ─────────────────────────────────────────────────────────────────────────────
  ESP32 UWB / UWB Pro（Makerfabs）— Tag for Plotting via UDP
  ─────────────────────────────────────────────────────────────────────────────
  功能概述：
    • 以「Tag（標籤）」角色啟動 DW1000 測距協定。
    • 每次量測到距離時：
        1) 在序列監視視窗（115200 bps）輸出：
             from:<短址HEX>   Range:<距離(m)>   RX power:<dBm>
        2) 透過 UDP 對主機（電腦）送出字串：
             "Range: <距離m>\n"
       → 方便 Python/`nc` 以最簡單字串解析首個浮點數當距離。

  使用說明（快速步驟）：
    1) Arduino IDE 安裝 ESP32 核心與 Makerfabs 的 DW1000 函式庫（DW1000Ranging / DW1000）。
    2) 以 USB 連接「Tag 板」，在「工具 > 開發板」選 ESP32 Dev Module，連接正確的序列埠。
    3) 檢查下方 TAG_EUI、Wi-Fi SSID/密碼與 HOST_IP，改成實際環境。
    4) 燒錄後開啟序列監視視窗（115200）。若 Anchor 已上線，會開始量測並透過 UDP 回傳。

  備註與小技巧：
    • HOST_IP 請用 macOS 的 `ipconfig getifaddr en0` 查到電腦 IP。
    • UDP 目的埠預設 9000；若同時讓 Anchor 也送距離，建議兩端用不同埠避免混線。
    • 若想讓輸出更平滑，可在 setup() 裡把 useRangeFilter(true) 打開。
    • 有些函式庫版本 getRXPower() 可能不可用；若編譯錯誤，將其列印與取值的兩行註解掉即可。
*/

#include <SPI.h>
#include "DW1000Ranging.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>  // strlen/snprintf（多半已內建，為保險仍引入）

// ──────────────── 1) Tag EUI（8 組 16 進位字串，以冒號分隔） ────────────────
//   ※ 建議改成板子實際的 EUI；維持固定值可做示範。
#define TAG_EUI "7D:00:22:EA:82:60:3B:9C"

// ──────────────── 2) 硬體接腳（Makerfabs 預設腳位） ────────────────
// SPI 腳位（連接到 DW1000）
#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23
#define DW_CS      4

// DW1000 的 RST / IRQ / SS（片選）腳位
const uint8_t PIN_RST = 27;  // Reset
const uint8_t PIN_IRQ = 34;  // 中斷
const uint8_t PIN_SS  = 4;   // SPI 片選（與 DW_CS 相同）

// ──────────────── 3) Wi-Fi / UDP 參數（請改成實際環境） ────────────────
const char* WIFI_SSID = "Yi Ping";       // ←  Wi-Fi SSID
const char* WIFI_PW   = "ypypypyp";      // ←  Wi-Fi 密碼

// 電腦（接收端）IP：請用 Mac `ipconfig getifaddr en0` 查到後填入
IPAddress   HOST_IP(172, 20, 10, 3);     // ← 務必改成實際 IP
const uint16_t HOST_PORT = 9000;         // 距離資料的 UDP 埠

WiFiUDP UWB_UDP;  // UDP socket（用於送距離字串）

// ──────────────── 4) 函式原型宣告（避免 Arduino 自動原型失敗） ────────────────
void newRange();
void newDevice(DW1000Device *device);
void inactiveDevice(DW1000Device *device);

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Arduino 標準初始化函式
 *  - 初始化序列埠
 *  - 初始化 SPI 與 DW1000Ranging
 *  - 設定 Tag 角色與 MODE
 *  - 連上 Wi-Fi，建立 UDP 送端
 */
void setup() {
  // 1) 串列通訊供除錯/觀察輸出
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("### TAG ###"));

  // 2) 初始化 SPI 與 DW1000 通訊
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // Reset, CS, IRQ

  // 3) 註冊回呼：量測成功 / 發現新裝置（Blink）/ 裝置轉為不活動（inactive）
  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  // 4) 可選：啟用內建距離濾波（平滑輸出）。預設關閉，若需要請取消註解。
  // DW1000Ranging.useRangeFilter(true);

  // 5) 以 Tag 身分啟動測距
  //    - 第 1 參數：Tag 的 EUI（長位址字串）
  //    - 第 2 參數：MODE（功耗/速率/精度配置）
  //      這裡使用 LONGDATA_RANGE_LOWPOWER：省電、量測穩定性不錯
  DW1000Ranging.startAsTag(TAG_EUI, DW1000.MODE_LONGDATA_RANGE_LOWPOWER);

  // 6) 連上 Wi-Fi（Station 模式）
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PW);
  Serial.print(F("WiFi connecting"));
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("."));
    delay(500);
  }
  Serial.print(F("\nWiFi OK, IP = "));
  Serial.println(WiFi.localIP());

  // 7) 啟動 UDP（對端是電腦；這裡當送端，不需固定本地埠，begin(0) 讓系統自選）
  UWB_UDP.begin(0);
}

/**
 * @brief Arduino 主迴圈
 *  - 務必呼叫 DW1000Ranging.loop() 以推進內部狀態機（收/發/測距）
 *  - 不需額外 delay，函式庫會自行節奏調度
 */
void loop() {
  DW1000Ranging.loop();
}

/**
 * @brief 成功取得一次距離量測時觸發
 *  輸出：
 *    1) 序列監視：from:<短址HEX>   Range:<距離(m)>   RX power:<dBm>
 *    2) UDP：    "Range: <距離> m\n"
 *
 *  注意：
 *    - getShortAddress()：顯示對端的 16-bit 短址（這裡以 16 進位印出）
 *    - getRange()：單次量測距離（公尺）
 *    - getRXPower()：接收端量到的訊號強度（dBm，部分庫版本可能略有不同）
 */
void newRange() {
  DW1000Device* dev = DW1000Ranging.getDistantDevice();
  if (!dev) return;

  // 取得距離與 RSSI（若 DW1000 函式庫無 getRXPower()，請把兩行 rx_dbm 相關註解）
  float range_m = dev->getRange();
  float rx_dbm  = dev->getRXPower();

  // 1) 序列輸出（便於人工檢視）
  Serial.print(F("from: "));
  Serial.print(dev->getShortAddress(), HEX);  // 以 16 進位輸出短址
  Serial.print(F("\t Range: "));
  Serial.print(range_m, 2);
  Serial.print(F(" m\t RX power: "));
  Serial.print(rx_dbm, 2);
  Serial.println(F(" dBm"));

  // 2) UDP：傳「最乾淨」的一行距離字串，方便 Python/`nc` 解析
  //    建議含換行 '\n'，讓接收端逐行處理更穩定。
  char msg[64];
  int n = snprintf(msg, sizeof(msg), "Range: %.2f m\n", range_m);
  UWB_UDP.beginPacket(HOST_IP, HOST_PORT);
  UWB_UDP.write((const uint8_t*)msg, (size_t)n);
  UWB_UDP.endPacket();

  // 若想同時帶 short addr 一起傳（Python 也能解析），可改用下列格式：
  // char msg2[96];
  // int n2 = snprintf(msg2, sizeof(msg2),
  //                   "from:%u  Range: %.2f m  RX power: %.2f dBm\n",
  //                   dev->getShortAddress(), range_m, rx_dbm);
  // UWB_UDP.beginPacket(HOST_IP, HOST_PORT);
  // UWB_UDP.write((const uint8_t*)msg2, (size_t)n2);
  // UWB_UDP.endPacket();
}

/**
 * @brief 偵測到新裝置（Blink）時觸發
 *  - 一般表示 Anchor/Tag 對端已被發現
 */
void newDevice(DW1000Device *device) {
  Serial.print(F("ranging init; 1 device added ! ->  short:"));
  Serial.println(device->getShortAddress(), HEX);
}

/**
 * @brief 某裝置轉為「inactive」時觸發
 *  - 代表長時間未通訊，從清單移除
 */
void inactiveDevice(DW1000Device *device) {
  Serial.print(F("delete inactive device: "));
  Serial.println(device->getShortAddress(), HEX);
}
