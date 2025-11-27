// ===============================================================
// 加密版 ESP32-UWB Tag 範例：AES-GCM 封包 (Serial / UDP)
// ---------------------------------------------------------------
// 功能說明：
// 1. 使用 Makerfabs ESP32-UWB (DW1000) + DW1000Ranging 做 TWR 量距。
// 2. 每次取得距離後，組出明文字串，例如： "7D00,1.234\n"
// 3. 使用 AES-GCM (mbedTLS) 進行加密：ciphertext + 16-byte Tag。
// 4. 封包格式： [1-byte version][12-byte IV][16-byte Tag][ciphertext...]
// 5. 將整個封包透過 Serial.write() 送出（可選：同時透過 UDP 送出）。
//
// 注意：
// - 這裡只加密「Tag → PC」的輸出資料，不改動 DW1000 的底層 UWB 幀格式。
// - Anchor 只要能正常量距即可，不需要改 Library。
// ===============================================================

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"

#include <WiFi.h>     // 若只用 Serial，可先刪掉 WiFi 相關部分
#include <WiFiUdp.h>

extern "C" {
  #include "mbedtls/aes.h"
  #include "mbedtls/gcm.h"
}

// =========================
// UWB / 硬體腳位設定（依板子保持不變）
// =========================
#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23
#define DW_CS     4       // 有些範例叫 SS / CS，這裡跟之前貼的一樣用 4

const uint8_t PIN_RST = 27;   // RST 腳位
const uint8_t PIN_IRQ = 34;   // IRQ 腳位
const uint8_t PIN_SS  = 4;    // SPI CS 腳位（同 DW_CS）

// Tag 的 UWB 短位址（原本的設定）
char tag_addr[] = "7D:00:22:EA:82:60:3B:9C";

// =========================
// （可選）WiFi + UDP 設定
// 如果只用 Serial，在下面 #define USE_UDP 改成 0 即可
// =========================
#define USE_UDP 0  // 1 = 也用 UDP 送封包到電腦, 0 = 僅使用 Serial

#if USE_UDP
const char *WIFI_SSID     = "Yi Ping";     // WiFi SSID
const char *WIFI_PASSWORD = "your_pass";   // WiFi 密碼

WiFiUDP Udp;
const char *UDP_HOST = "192.168.1.100";    // 電腦 IP（請改成電腦在同一個 LAN 的 IP）
const uint16_t UDP_PORT = 9100;            // Python 端監聽的 Port
#endif

// =========================
// AES-GCM 金鑰與封包參數
// =========================

// 16 bytes = 128-bit PSK（實驗用；正式系統請改成金鑰）
static const uint8_t AES_KEY[16] = {
  0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88,
  0x99, 0xAA, 0xBB, 0xCC,
  0xDD, 0xEE, 0xFF, 0x00
};

static const size_t AES_KEY_LEN = 16;   // 128-bit
static const size_t AES_IV_LEN  = 12;   // GCM 推薦 96-bit IV
static const size_t AES_TAG_LEN = 16;   // 128-bit 認證 Tag

// 協定版本（目前固定 0x01）
static const uint8_t PACKET_VERSION = 0x01;

// 最大明文字串長度（足夠放 "FFFF,1234.567890\n" 這種）
static const size_t MAX_PLAINTEXT_LEN = 128;
// 封包最大長度 = 1 (version) + 12 (IV) + 16 (Tag) + 明文上限
static const size_t MAX_PACKET_LEN = 1 + AES_IV_LEN + AES_TAG_LEN + MAX_PLAINTEXT_LEN;

// =========================
// AES-GCM 小工具函式
// =========================

/**
 * 產生隨機 IV
 * 使用 ESP32 內建硬體亂數 esp_random()
 */
void generateRandomIV(uint8_t *iv, size_t iv_len) {
  for (size_t i = 0; i < iv_len; i += 4) {
    uint32_t r = esp_random();  // ESP32 硬體亂數
    size_t remain = iv_len - i;
    size_t copy_len = (remain >= 4) ? 4 : remain;
    memcpy(iv + i, &r, copy_len);
  }
}

/**
 * 使用 AES-GCM 進行加密
 *
 * 參數：
 *   key        : 16-byte AES 金鑰
 *   iv         : IV 緩衝區（長度 AES_IV_LEN），呼叫前需已填好
 *   plaintext  : 明文字節陣列
 *   plen       : 明文長度
 *   ciphertext : 輸出：密文字節陣列（長度 >= plen）
 *   tag        : 輸出：GCM 認證 Tag（長度 AES_TAG_LEN）
 *
 * 回傳：
 *   0 表示成功；非 0 表示失敗
 */
int aesGcmEncrypt(
  const uint8_t *key,
  const uint8_t *iv,
  const uint8_t *plaintext, size_t plen,
  uint8_t *ciphertext,
  uint8_t *tag
) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);  // 初始化 context

  // 設定金鑰
  int ret = mbedtls_gcm_setkey(
    &ctx,
    MBEDTLS_CIPHER_ID_AES,   // cipher 類型：AES
    key,
    AES_KEY_LEN * 8          // bit 數（128）
  );
  if (ret != 0) {
    mbedtls_gcm_free(&ctx);
    return ret;
  }

  // 加密 + 產生 Tag
  ret = mbedtls_gcm_crypt_and_tag(
    &ctx,
    MBEDTLS_GCM_ENCRYPT,     // 加密模式
    plen,                    // 明文長度
    iv, AES_IV_LEN,          // IV
    NULL, 0,                 // AAD（這個範例不使用附加認證資料）
    plaintext,               // 明文輸入
    ciphertext,              // 密文輸出
    AES_TAG_LEN,             // Tag 長度
    tag                      // Tag 輸出
  );

  mbedtls_gcm_free(&ctx);
  return ret;
}

// ---------------------------------------------------------------
// 如果之後要 ESP32 ↔ ESP32 互相解密，也可以用這個 Decrypt 函式
// 目前 Tag → PC 解密在 Python 端做，所以這裡可以只留 Encrypt 即可。
// ---------------------------------------------------------------

int aesGcmDecrypt(
  const uint8_t *key,
  const uint8_t *iv,
  const uint8_t *tag,
  const uint8_t *ciphertext, size_t clen,
  uint8_t *plaintext
) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);

  int ret = mbedtls_gcm_setkey(
    &ctx,
    MBEDTLS_CIPHER_ID_AES,
    key,
    AES_KEY_LEN * 8
  );
  if (ret != 0) {
    mbedtls_gcm_free(&ctx);
    return ret;
  }

  ret = mbedtls_gcm_auth_decrypt(
    &ctx,
    clen,
    iv, AES_IV_LEN,
    NULL, 0,          // 無 AAD
    tag, AES_TAG_LEN, // 驗證用 Tag
    ciphertext,
    plaintext
  );

  mbedtls_gcm_free(&ctx);
  return ret;  // 0 = OK，非 0 = 驗證失敗 / 解密錯誤
}

// =========================
// DW1000Ranging callback 函式宣告
// =========================
void newRange();
void newDevice(DW1000Device *device);
void inactiveDevice(DW1000Device *device);

// =========================
// setup / loop
// =========================

void setup() {
  // ---- Serial 初始化：用來送「加密後封包」給 PC ----
  Serial.begin(115200);
  delay(1000);

  // ---- SPI 初始化（照 Makerfabs 範例）----
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); // 初始化 DW1000 通訊

  // ---- 綁定 callback ----
  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  // ---- 啟動為 Tag ----
  DW1000Ranging.startAsTag(
    tag_addr,
    DW1000.MODE_LONGDATA_RANGE_LOWPOWER,
    false
  );

#if USE_UDP
  // ---- 若需要同時透過 UDP 送到 PC，啟動 WiFi + UDP ----
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("連線到 WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi 連線成功，IP = ");
  Serial.println(WiFi.localIP());

  Udp.begin(UDP_PORT);
  Serial.print("UDP 已啟動，Port = ");
  Serial.println(UDP_PORT);
#endif
}

void loop() {
  // DW1000Ranging 的主迴圈（一定要持續呼叫）
  DW1000Ranging.loop();
}

// =========================
// callback 實作
// =========================

// ---- 新量測距離時會被呼叫 ----
void newRange() {
  // 1. 從 DW1000Ranging 取得目標裝置短位址與距離
  DW1000Device *dev = DW1000Ranging.getDistantDevice();
  if (dev == nullptr) {
    Serial.println("ERR: no distant device");
    return;
  }

  uint16_t shortAddr = dev->getShortAddress();
  float distance = dev->getRange();  // 單位通常是公尺（m）

  // 2. 組成「明文字串」
  //    格式： "7D00,1.234\n"
  char plaintext[MAX_PLAINTEXT_LEN];
  int plen = snprintf(
    plaintext,
    sizeof(plaintext),
    "%04X,%.3f\n",
    shortAddr,
    distance
  );

  if (plen <= 0 || plen > (int)MAX_PLAINTEXT_LEN) {
    Serial.println("ERR: plaintext too long or format error");
    return;
  }

  // 3. 準備加密所需的 buffer：IV / tag / ciphertext
  uint8_t iv[AES_IV_LEN];
  uint8_t tag[AES_TAG_LEN];
  uint8_t ciphertext[MAX_PLAINTEXT_LEN];

  // 3-1. 產生隨機 IV
  generateRandomIV(iv, AES_IV_LEN);

  // 3-2. AES-GCM 加密
  int ret = aesGcmEncrypt(
    AES_KEY,
    iv,
    (const uint8_t *)plaintext,
    (size_t)plen,
    ciphertext,
    tag
  );

  if (ret != 0) {
    Serial.print("ERR: aesGcmEncrypt failed, ret = ");
    Serial.println(ret);
    return;
  }

  // 4. 組成「完整封包」
  uint8_t packet[MAX_PACKET_LEN];
  size_t offset = 0;

  // 4-1. version
  packet[offset++] = PACKET_VERSION;

  // 4-2. IV
  memcpy(packet + offset, iv, AES_IV_LEN);
  offset += AES_IV_LEN;

  // 4-3. Tag
  memcpy(packet + offset, tag, AES_TAG_LEN);
  offset += AES_TAG_LEN;

  // 4-4. Ciphertext
  memcpy(packet + offset, ciphertext, (size_t)plen);
  offset += (size_t)plen;

  // 5. 透過 Serial 送整包（PC 端看到會是「亂碼 bytes」）
  Serial.write(packet, offset);
  Serial.flush();  // 確保資料真的送出去

#if USE_UDP
  // 6. （可選）透過 UDP 送到電腦
  Udp.beginPacket(UDP_HOST, UDP_PORT);
  Udp.write(packet, offset);
  Udp.endPacket();
#endif
}

// ---- 新裝置加入時（可視需要印 Log）----
void newDevice(DW1000Device *device) {
  Serial.print("新裝置加入，短位址 = ");
  Serial.println(device->getShortAddress(), HEX);
}

// ---- 裝置失聯時（可視需要印 Log）----
void inactiveDevice(DW1000Device *device) {
  Serial.print("裝置失聯，短位址 = ");
  Serial.println(device->getShortAddress(), HEX);
}
