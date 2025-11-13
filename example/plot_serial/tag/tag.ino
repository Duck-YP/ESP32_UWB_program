/*
  ─────────────────────────────────────────────────────────────────────────────
  ESP32 UWB / UWB Pro（Makerfabs）— Tag for Plotting via UDP
  ─────────────────────────────────────────────────────────────────────────────
  功能摘要
    • 以「Tag（標籤）」角色啟動 DW1000 測距協定。
    • 每次完成距離量測時，序列監視視窗（115200 bps）輸出：
         from:<短址HEX>   Range:<距離(m)>   RX power:<dBm>
    • 程式維持官方最小可行結構，補齊區塊化註解、腳位與模式說明。

  注意事項
    • startAsTag(...) 內之 EUI 為長位址（64-bit）；須與裝置設定一致方能正常配對。
    • 腳位配置依 Makerfabs 板卡預設；硬體異動需同步調整 SPI 與 RST/IRQ/SS。
    • 量測數值如有明顯抖動，可依需求啟用內建濾波（useRangeFilter(true)）。
*/

#include <SPI.h>
#include "DW1000Ranging.h"

// ──────────────── SPI 與 DW1000 相關腳位（Makerfabs 預設） ────────────────
// SPI 訊號（接至 DW1000 晶片）
#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23
#define DW_CS      4  // 片選腳位（下方 PIN_SS 亦指向此腳位）

// DW1000 的重置、外部中斷與片選腳位
const uint8_t PIN_RST = 27; // Reset pin
const uint8_t PIN_IRQ = 34; // IRQ pin（中斷）
const uint8_t PIN_SS  = 4;  // SPI 片選（與 DW_CS 同腳位）

// ──────────────── 函式原型（避免自動原型推斷失敗） ────────────────
void newRange();
void newDevice(DW1000Device *device);
void inactiveDevice(DW1000Device *device);

void setup()
{
    // 1) 啟動序列埠（供觀察量測輸出與除錯）
    Serial.begin(115200);
    delay(1000);

    // 2) 初始化 SPI 與 DW1000Ranging 通訊層
    //    SPI.begin(SCK, MISO, MOSI) 後，DW1000Ranging 以 Reset/CS/IRQ 與晶片連線
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // Reset, CS, IRQ pin

    // 3) 註冊回呼
    //    newRange(): 每次完成距離量測時觸發
    //    newDevice(): 掃描到新裝置（Blink）時觸發
    //    inactiveDevice(): 對端長時間未互動轉為 inactive 時觸發
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);

    // 4) （可選）啟用內建距離濾波以平滑量測（預設關閉）
    // DW1000Ranging.useRangeFilter(true);

    // 5) 以「Tag」角色啟動
    //    參數說明：
    //      • 第 1 參數：Tag 的 EUI（長位址字串）
    //      • 第 2 參數：無線參數模式（資料長度/速率/功耗/精度）
    //
    //    常見模式（對照需求擇一）：
    //      MODE_SHORTDATA_FAST_LOWPOWER  ：短包、快速、低功耗
    //      MODE_LONGDATA_FAST_LOWPOWER   ：長包、快速、低功耗
    //      MODE_SHORTDATA_FAST_ACCURACY  ：短包、快速、較高精度
    //      MODE_LONGDATA_FAST_ACCURACY   ：長包、快速、較高精度
    //      MODE_LONGDATA_RANGE_LOWPOWER  ：長包、量測距離穩定、低功耗（本例）
    //      MODE_LONGDATA_RANGE_ACCURACY  ：長包、量測距離精度較佳
    DW1000Ranging.startAsTag("7D:00:22:EA:82:60:3B:9C",
                             DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
}

void loop()
{
    // 主迴圈需持續呼叫，驅動 DW1000Ranging 的狀態機與收發流程
    DW1000Ranging.loop();
}

// ─────────────────────────────────────────────────────────────────────────────
// 回呼區：量測與裝置狀態通知
// ─────────────────────────────────────────────────────────────────────────────

/*
  newRange()
  說明：
    每次成功完成一次「Tag ↔ Anchor」的距離量測後觸發。
    透過 DW1000Ranging.getDistantDevice() 取得對端裝置，再讀取：
      • getShortAddress()：對端短址（16 位元），此處以 HEX 輸出便於識別
      • getRange()       ：單次量測距離（公尺）
      • getRXPower()     ：接收訊號強度（dBm；視函式庫版本而定）
  輸出格式（序列監視）：
      from:<短址HEX>   Range:<距離(m)>   RX power:<dBm>
*/
void newRange()
{
    Serial.print("from: ");
    Serial.print(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
    Serial.print("\t Range: ");
    Serial.print(DW1000Ranging.getDistantDevice()->getRange());
    Serial.print(" m");
    Serial.print("\t RX power: ");
    Serial.print(DW1000Ranging.getDistantDevice()->getRXPower());
    Serial.println(" dBm");
}

/*
  newDevice(DW1000Device* device)
  說明：
    當偵測到新裝置的「Blink」封包時觸發，代表已發現一個可互動的對端。
*/
void newDevice(DW1000Device *device)
{
    Serial.print("ranging init; 1 device added ! -> ");
    Serial.print(" short:");
    Serial.println(device->getShortAddress(), HEX);
}

/*
  inactiveDevice(DW1000Device* device)
  說明：
    當某對端裝置長時間未通訊而轉為「inactive」時觸發。
    通常表示裝置離線、關機或暫時不在通訊範圍內。
*/
void inactiveDevice(DW1000Device *device)
{
    Serial.print("delete inactive device: ");
    Serial.println(device->getShortAddress(), HEX);
}
