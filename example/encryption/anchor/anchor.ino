/*
  ─────────────────────────────────────────────────────────────────────────────
  ESP32 UWB / UWB Pro（Makerfabs）— Anchor for Plotting via UDP
  ─────────────────────────────────────────────────────────────────────────────
  功能概述：
    • 以「Anchor（錨點）」角色啟動 DW1000 測距協定。
    • 串列監視視窗（115200 bps）持續輸出：
        from:<短址>   Range:<距離(m)>   RX power:<dBm>

  使用說明（快速步驟）：
    1) Arduino IDE 安裝 ESP32 核心與 Makerfabs 的 DW1000 函式庫（DW1000Ranging / DW1000）。
    2) 以 USB 連接「Anchor 板」，在「工具 > 開發板」選 ESP32 Dev Module，連接正確的序列埠。
    3) 檢查下方 ANCHOR_ADD 是否與 Anchor EUI 一致（或沿用此固定值做配對示範）。
    4) 燒錄後開啟序列監視視窗（115200），待 Tag 上線後即可看到距離變化。

  重要名詞：
    • EUI（Extended Unique Identifier）：裝置長位址（字串 8 組 16 進位）。
    • Short Address（短址）：協定交換中使用的 16-bit 短位址（序列印出時以 16 進位顯示）。
    • MODE_*：DW1000 的資料率／前導碼等配置，影響測距耗電與精度。
      - LOWPOWER：較省電、速率較慢、量測較穩。
      - FAST / ACCURACY：速度／精度權衡，依環境選擇。

  常見調整：
    • 想要更平滑的距離輸出，可開啟下方的 useRangeFilter(true)。
    • 若要切換不同 MODE，請參考 setup() 內的幾種預設註解選項。
*/

#include <SPI.h>
#include "DW1000Ranging.h"

// ──────────────── 1) Anchor EUI（8 組 16 進位字串，以冒號分隔） ────────────────
//   ※ 若有實體標籤上的 EUI，請改成對應的字串；否則可先維持此固定值做測試。
#define ANCHOR_ADD "86:17:5B:D5:A9:9A:E2:9C"

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

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Arduino 標準初始化函式
 *  - 初始化序列埠
 *  - 初始化 SPI 與 DW1000Ranging
 *  - 設定 Anchor 角色與 MODE
 *  - （可選）開啟距離濾波
 */
void setup() {
  // 1) 串列通訊供除錯/觀察輸出
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("### ANCHOR ###"));

  // 2) 初始化 SPI 與 DW1000 通訊
  //    - SPI.begin() 依 Makerfabs 板子接腳設定
  //    - initCommunication() 需要 Reset / CS / IRQ 腳位
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // Reset, CS, IRQ

  // 3) 註冊回呼：量測成功 / 發現新裝置（Blink）/ 裝置轉為不活動（inactive）
  //    - newRange()：成功拿到一次距離時觸發
  //    - newBlink()：偵測到 Tag 的初次 Blink 封包（表示發現新裝置）
  //    - inactiveDevice()：對端長時間不通訊時觸發
  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachBlinkDevice(newBlink);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  // 4) 可選：啟用內建距離濾波（平滑輸出）。預設關閉，若需要請取消註解。
  // DW1000Ranging.useRangeFilter(true);

  // 5) 以 Anchor 身分啟動測距
  //    - 第 1 參數：Anchor 的 EUI（長位址字串）
  //    - 第 2 參數：MODE（功耗/速率/精度配置）
  //    - 第 3 參數：是否隨機分配短址（false=固定短址，較容易追蹤）
  //
  //    這裡預設使用「LONGDATA_RANGE_LOWPOWER」：省電、距離穩定性不錯
  DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);

  //    其他常見 MODE（依需求擇一；請保持僅啟用一個）：
  // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_SHORTDATA_FAST_LOWPOWER);
  // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_FAST_LOWPOWER);
  // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_SHORTDATA_FAST_ACCURACY);
  // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_FAST_ACCURACY);
  // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_RANGE_ACCURACY);
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
 *  輸出格式：
 *    from:<短址HEX>   Range:<距離(m)>   RX power:<dBm>
 *
 *  注意：
 *    - getShortAddress()：顯示對端的 16-bit 短址（以 16 進位印出）
 *    - getRange()：單次量測距離（公尺）
 *    - getRXPower()：接收端量到的訊號強度（dBm，部分庫版本可能略有不同）
 */
void newRange() {
  Serial.print(F("from: "));
  Serial.print(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);

  Serial.print(F("\t Range: "));
  Serial.print(DW1000Ranging.getDistantDevice()->getRange());
  Serial.print(F(" m"));

  Serial.print(F("\t RX power: "));
  Serial.print(DW1000Ranging.getDistantDevice()->getRXPower());
  Serial.println(F(" dBm"));
}

/**
 * @brief 偵測到新裝置（Blink）時觸發
 *  - 一般表示有 Tag 上線並被 Anchor 發現
 *  - 這個回呼只印出「短址」
 */
void newBlink(DW1000Device* device) {
  Serial.print(F("blink; 1 device added ! ->  short:"));
  Serial.println(device->getShortAddress(), HEX);
}

/**
 * @brief 某裝置轉為「inactive」時觸發
 *  - 代表長時間未通訊，從清單移除
 */
void inactiveDevice(DW1000Device* device) {
  Serial.print(F("delete inactive device: "));
  Serial.println(device->getShortAddress(), HEX);
}
