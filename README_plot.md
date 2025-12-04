# ESP32‑UWB (DW1000) 測距與即時視覺化套件

> 兩塊 Makerfabs ESP32‑UWB（DW1000）板：**快速燒錄、量測距離、即時視覺化（Serial/UDP）、封包事件 MSC 梯形圖**。
>
> 作業系統：macOS（以 M1/12.1 實測）。IDE：Arduino IDE 2.x / VS Code（選用）。
>
> 內含三個 Python 腳本：`plot_serial.py`、`plot_udp.py`、`packet_ladder.py`。

---

## 目錄

* [硬體與軟體需求](#硬體與軟體需求)
* [安裝 Arduino IDE 與 ESP32 核心](#安裝-arduino-ide-與-esp32-核心)
* [安裝 Makerfabs DW1000 函式庫](#安裝-makerfabs-dw1000-函式庫)
* [抓範例專案與本倉庫腳本](#抓範例專案與本倉庫腳本)
* [辨識序列埠（macOS）](#辨識序列埠macos)
* [燒錄 Anchor 範例](#燒錄-anchor-範例)
* [燒錄 Tag 範例（Serial 版本）](#燒錄-tag-範例serial-版本)
* [（選用）Tag/Anchor 送 UDP 距離與事件](#選用taganchor-送-udp-距離與事件)
* [Python 即時視覺化](#python-即時視覺化)

  * [1) Serial 版：`plot_serial.py`](#1-serial-版plot_serialpy)
  * [2) UDP 版：`plot_udp.py`](#2-udp-版plot_udppy)
  * [3) 封包 MSC 梯形圖：`packet_ladder.py`](#3-封包-msc-梯形圖packet_ladderpy)
* [教授要看的「兩個封包視覺化」是什麼](#教授要看的兩個封包視覺化是什麼)
* [VS Code 操作（選用）](#vs-code-操作選用)
* [疑難排解（FAQ）](#疑難排解faq)
* [資料夾建議結構](#資料夾建議結構)
* [授權](#授權)

---

## 硬體與軟體需求

* 兩塊 **Makerfabs ESP32‑UWB (DW1000)** 板（例如一塊當 **Anchor**、一塊當 **Tag**）。
* USB‑C 傳輸線各一。
* macOS（實測：MacBook Air (M1, 2020), macOS 12.1）。
* Arduino IDE 2.x（或 Arduino CLI / VS Code + Arduino 擴充）。
* Python 3.9+（實測 3.11），`pip` 可用；建議使用 `venv`。

---

## 安裝 Arduino IDE 與 ESP32 核心

1. 安裝 Arduino IDE（建議 2.x）。
2. 開啟 IDE → `Arduino IDE > Preferences > Additional Boards Manager URLs` 加入 **Espressif 官方 JSON**：

   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. `Tools > Board > Boards Manager` 搜尋 **esp32** 並安裝 **Espressif ESP32** 平台。
4. 安裝後可在 `Tools > Board` 選用 **ESP32 Dev Module** 等板型。

> 備註：本 README 依官方文件流程，實測可用。

---

## 安裝 Makerfabs DW1000 函式庫

Makerfabs 官方提供相容的 **DW1000** 函式庫（檔名通常為 `mf_DW1000.zip`）。因程式 `#include` 名稱是 `DW1000`，請：

1. 下載官方 zip（針對 Makerfabs 版本）。
2. 將檔案**改名**為 `DW1000.zip`。
3. Arduino IDE → `Sketch > Include Library > Add .ZIP Library…` 選擇此 `DW1000.zip` 安裝。

> 目的是確保 `#include "DW1000.h"`、`#include "DW1000Ranging.h"` 能正確解析到 Makerfabs 版本。

---

## 抓範例專案與本倉庫腳本

* 官方 repo：`Makerfabs-ESP32-UWB` → 使用 `example/anchor/` 與 `example/tag/`。
* 本倉庫提供三個 Python 視覺化腳本，放在倉庫根目錄：

  * `plot_serial.py`
  * `plot_udp.py`
  * `packet_ladder.py`

> 若你要在 MCU 端送 UDP 事件/距離，請參考本倉庫 `example/tag/` 與 `example/anchor/` 內附的 **已加註解版本**（可直接燒錄）。

---

## 辨識序列埠（macOS）

接上板子後，在終端機：

```bash
ls /dev/cu.*
python3 -m serial.tools.list_ports -v
```

常見會看到：`/dev/cu.usbserial-xxxx`（CP210x）或 `/dev/cu.wchusbserial-xxxx`（CH34x）。

範例（實測）：

```
/dev/cu.usbserial-02E22762   # Anchor
/dev/cu.usbserial-02E2277A   # Tag
```

---

## 燒錄 Anchor 範例

1. 插上 **第 1 塊**（預計當 Anchor）。
2. Arduino IDE → `File > Open…` → 開啟官方 `example/anchor/anchor.ino`（或本倉庫增強版）。
3. `Tools > Board` 選 **ESP32 Dev Module**；`Tools > Port` 選上一步查到的埠。
4. **Upload**。
5. `Tools > Serial Monitor`（115200 bps）可看到 Anchor 訊息。

輸出示例：

```
from: 4A3C   Range: 0.56 m   RX power: -56.02 dBm
from: 4A3C   Range: 0.54 m   RX power: -56.73 dBm
...
```

---

## 燒錄 Tag 範例（Serial 版本）

1. 插上 **第 2 塊**（預計當 Tag）。
2. 開啟 `example/tag/tag/tag.ino`（或本倉庫增強版）。
3. 同樣設定板型與 Port → **Upload**。
4. `Serial Monitor`（115200 bps）應持續列印 **距離** 與 **RSSI**。

到此：你已能在 **序列埠** 看到距離資料。

---

## （選用）Tag/Anchor 送 UDP 距離與事件

為了在電腦端做更進階的視覺化（UDP），韌體端需設定：

* **Wi‑Fi**（Station）：

  ```cpp
  const char* WIFI_SSID = "<你的SSID>";
  const char* WIFI_PW   = "<你的密碼>";
  ```
* **接收端（你的電腦）IP**：在 Mac 查詢

  ```bash
  ipconfig getifaddr en0   # 常見為 192.168.x.x 或 172.20.10.1（iPhone 熱點）
  ```

  然後在兩份 sketch 內設：

  ```cpp
  IPAddress HOST_IP(172, 20, 10, 1);   // ← 改成你的實際 IP
  ```
* **UDP 埠**：

  * 距離資料：`HOST_PORT = 9000`
  * 事件回報（MSC 梯形圖用）：`EVT_PORT = 9100`
* **開關巨集**：

  ```cpp
  #define ENABLE_EVT_UDP 1      // 9100 送 TX/RX/RANGE（兩端都要一致）
  #define ENABLE_UDP_RANGE 0    // Anchor 是否也送距離到 9000（預設關）
  ```

> 上電後，兩端會各送一包 `HEARTBEAT` 到 9100，方便你用 `nc -ul 9100` 先驗證路通。

---

## Python 即時視覺化

> 建議以 `venv` 隔離環境：

```bash
python3 -m venv venv 
source venv/bin/activate

python3 -m pip install --upgrade pip

python3 -m pip install pyserial matplotlib pandas
```

### 1) Serial 版：`plot_serial.py`

**功能**：從 Tag 串列輸出解析距離（公尺），即時畫「距離 vs. 時間」，可存 CSV。

**用法**：

```bash
python3 plot_serial.py --port /dev/cu.usbserial-02E2277A --baud 115200 --save run_serial.csv --window 300
```

**參數**：

* `--port`：你的 Tag 序列埠。
* `--baud`：波特率（預設 115200）。
* `--save`：結束後輸出 CSV（選用）。
* `--window`：圖上顯示最近 N 點（預設 300）。

**輸出**：互動折線圖 + （選用）CSV：`time_s, distance_m`。

---

### 2) UDP 版：`plot_udp.py`

**功能**：監聽 9000 埠，解析 MCU 送來的 `"Range: <m>"` 訊息，畫距離折線圖、可存 CSV。

**用法**：

```bash
# 先用 netcat 確認收到資料
nc -ul 9000

# 看到連續的 "Range: 0.85 m" 後，改跑 Python 視覺化
python3 plot_udp.py --port 9000 --save run_udp.csv --print-every 10
```

**參數**：

* `--port`：UDP 監聽埠（預設 9000）。
* `--save`：存 CSV（選用）。
* `--window`：顯示最近 N 點（預設 300）。
* `--print-every`：每 N 筆列印一次解析確認（選用）。

---

### 3) 封包 MSC 梯形圖：`packet_ladder.py`

**功能**：監聽 9100 埠的**封包事件**，把 `TAG` 與 `ANCHOR` 的 **TX/RX/RANGE/HEARTBEAT** 畫成**訊息序列圖（Message Sequence Chart）**，並在下方顯示 **每秒事件數（pps）**。

**事件字串格式（MCU 韌體已內建）**：

```
role=TAG|ANCHOR  evt=TX|RX         dev_ts=<數字>
role=TAG|ANCHOR  evt=RANGE        host_ts=<數字>
role=TAG|ANCHOR  evt=HEARTBEAT    host_ts=0
```

**用法**：

```bash
# 先確認有事件
nc -ul 9100
# 看見 HEARTBEAT 與 TX/RX 後，改跑視覺化：
python3 packet_ladder.py --port 9100 --window 60 --pair-horizon 0.3 --debug
```

**參數**：

* `--port`：UDP 監聽埠（預設 9100）。
* `--window`：圖上顯示最近幾秒（預設 60）。
* `--pair-horizon`：**TX→對端 RX** 的配對時間窗（秒），建議 0.2–0.5，避免錯配。
* `--debug`：終端同步打印 RAW 事件，方便稽核。

**讀圖重點**：

* 上半部左右泳道分別是 TAG 與 ANCHOR；彩點是事件時刻，黑箭頭是 **最近配對的 TX→RX**。
* 下半部是每秒事件數（不含 HEARTBEAT）。
* `dev_ts` 來自 DW1000 晶片，解析度約 **15.65 ps**，為 **40‑bit 計數器**，**約 17.2 秒回捲**，屬正常現象；配對主要依**主機接收時間**。

---

## 教授要看的「兩個封包視覺化」是什麼

1. **距離 vs. 時間**：以 `plot_serial.py`（或 `plot_udp.py`）畫出**測距曲線**，可附 CSV 作為原始證據。
2. **封包訊息序列（MSC）**：以 `packet_ladder.py` 畫出每一輪**封包從 TAG 到 ANCHOR 的 TX→RX**（以及回程），呈現鏈路健康度與事件率。

> 簡報話術建議：
>
> * 架構：Tag/Anchor 透過 DW1000 完成 ranging；MCU 以 UDP 報送距離與封包事件。
> * 證據：圖 + RAW 事件（加 `--debug`）；可量化 `RX/TX` 比、TX→RX 主機延遲分佈。
> * 現況：實測事件率約 1–3 pps；偶發單邊事件多半是距離/遮蔽物/天線朝向干擾。

---

## VS Code 操作（選用）

* 安裝 **Python** 與 **Arduino** 擴充。
* Python：打開本倉庫資料夾 → `Python: Select Interpreter` 選 `venv` → 直接在內建終端執行腳本。
* Arduino：安裝 Arduino 擴充後，選對板型與埠即可上傳；或使用 Arduino IDE 上傳、VS Code 只用來閱讀程式碼。

---

## 疑難排解（FAQ）

* **Serial 連線錯誤**：`could not open port /dev/cu.SLAB_USBtoUART`

  * 用 `python3 -m serial.tools.list_ports -v` 查正確埠名（如 `/dev/cu.usbserial-02E2277A`）。
  * 確認線材可傳輸；更換 USB 埠。
* **9100/9000 沒有任何輸出**：

  * 先看 `Serial Monitor` 是否顯示 **Wi‑Fi OK, IP = ...**。
  * 檢查 `HOST_IP(...)` 是否就是你 Mac 的 IP；iPhone 熱點常見 `172.20.10.1`。
  * 先用 `nc -ul 9100` / `nc -ul 9000` 驗證 HEARTBEAT/Range 是否到達。
  * 兩端 `#define ENABLE_EVT_UDP 1` 是否一致；Anchor 若也要送距離，開 `ENABLE_UDP_RANGE 1` 或改用不同埠避免混線。
* **MSC 圖只有單邊箭頭**：

  * 放寬 `--pair-horizon` 至 0.5–1.0 秒試配對；或改善擺位/天線朝向。
  * 在兩端 `newRange()` 內也送 `evt=RANGE`（本倉庫版本已內建），確保每次量測都有成對訊號可視化。
* **Matplotlib 中文缺字警告**：不影響功能；可安裝 Noto CJK 或改腳本中 `rcParams` 字型設定。
* **zsh 出現 `^[ [200~python3`**：是貼上殘留的 bracketed‑paste 控制碼；請手動輸入指令。
* **`dev_ts` 為何變小？**：DW1000 40‑bit 計數器回捲，約每 17.2 秒一次，屬正常；配對視覺化以主機時間為準。

---

## 資料夾建議結構

```
ESP32_program/
├── example/
│   ├── anchor/              # Anchor 增強版（含 UDP 事件/距離，繁中註解）
│   └── tag/                 # Tag 增強版（含 UDP 事件/距離，繁中註解）
├── plot_serial.py       # 串列即時距離圖 + CSV
├── plot_udp.py          # UDP 距離圖 + CSV
├── packet_ladder.py     # UDP 事件 MSC + PPS
└── README.md                # 本文件
```

---

## 授權

* 本倉庫新增的 Python 腳本與 README：建議 **MIT License**（可自行在根目錄新增 `LICENSE`）。
* Arduino 端 DW1000 函式庫與官方範例：依原專案授權條款。

---

### 附：常用指令速查

```bash
# 查序列埠
python3 -m serial.tools.list_ports -v

# 查 Mac Wi‑Fi IP
ipconfig getifaddr en0

# UDP 監聽測試
nc -ul 9000
nc -ul 9100

# 視覺化（常用）
python3 plot_serial.py --port /dev/cu.usbserial-XXXX --baud 115200 --save run.csv

python3 plot_udp.py    --port 9000 --save run_udp.csv --print-every 10

python3 packet_ladder.py \
  --port 9100 --window 60 \
  --pair-horizon 0.30 --pair-horizon-full 0.40 \
  --reorder-guard 0.02 --strict-order \
  --annotate-dt --ann-last 12 --use-range-fallback \
  --save-csv cycles.csv --csv-flush-every 1 --debug
```
