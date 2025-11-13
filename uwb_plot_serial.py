# uwb_plot_serial.py 
# 用法：
#   python3 uwb_plot_serial.py --port /dev/cu.usbserial-02E2277A --baud 115200 --save run.csv
#   （可加 --debug 看每個收到的封包內容；--print-every 10 代表每 10 筆資料印 1 行）

import argparse
import re
import sys
import time
import serial
import matplotlib.pyplot as plt
import pandas as pd
from collections import deque
from pathlib import Path 
from typing import Optional


# 解析一行：優先抓 "Range: X (m|cm)"，其次抓 "distance:" / "d=" 等備援格式
def parse_line(line: str) -> Optional[dict]:
    """解析序列埠讀到的一行字串，回傳包含距離、公尺數、peer ID、RX power 的字典；無法解析則回傳 None

    Args:
        line (str): 序列埠讀到的一行字串
    
    Returns:
        Optional[dict]: 包含 "peer", "range_m", "rx_dbm" 的字典，或 None 如果無法解析
    """
    # 1) 標準：from: <peer>  Range: <val> <unit>  RX power: <dbm> dBm
    m = re.search(
        r'from:\s*(\d+).*?Range:\s*([\-]?\d+(?:\.\d+)?)\s*(cm|m)?(?:.*?RX\s*power:\s*([\-]?\d+(?:\.\d+)?)\s*dBm)?',
        line, re.IGNORECASE
    )
    if m:
        peer = m.group(1)
        val = float(m.group(2))
        unit = (m.group(3) or 'm').lower()
        if unit == 'cm':
            val /= 100.0
        rx = float(m.group(4)) if m.group(4) is not None else None
        return {"peer": peer, "range_m": val, "rx_dbm": rx}

    # 2) 備援：distance: X m / d=X / range X
    m2 = re.search(r'(?:distance[:=]\s*|d\s*=\s*|range\s+)(\-?\d+(?:\.\d+)?)\s*(cm|m)?', line, re.IGNORECASE)
    if m2:
        val = float(m2.group(1))
        unit = (m2.group(2) or 'm').lower()
        if unit == 'cm':
            val /= 100.0
        return {"peer": None, "range_m": val, "rx_dbm": None}

    return None

def main() -> None:
    """
    主程式：從序列埠讀取 UWB 距離資料並即時繪圖
    1. 解析命令列參數
    2. 開啟序列埠
    3. 進入讀取迴圈，解析每行資料並更新圖表
    4. 停止後儲存 CSV 檔（如果指定）
    5. 清理資源並結束
    """
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True, help='例如 /dev/cu.usbserial-02E2277A')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--save', default=None, help='結束後輸出 CSV 檔名（可省略）')
    ap.add_argument('--window', type=int, default=300, help='折線圖顯示最近點數')
    ap.add_argument('--debug', action='store_true', help='印出每一行 RAW，以利除錯')
    ap.add_argument('--print-every', type=int, default=0, help='每 N 筆成功資料印 1 行摘要（0=不印）')
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(f"[ERROR] 無法開啟埠 {args.port}: {e}")
        sys.exit(1)
    print(f'[INFO] 連線 {args.port} @ {args.baud}，開始讀取... (Ctrl+C 結束)')

    xs = deque(maxlen=args.window)
    ys = deque(maxlen=args.window)
    all_rows = []  # 存 time_s, range_m, peer, rx_dbm
    plt.ion()
    fig = plt.figure()
    ax = fig.add_subplot(111)
    line, = ax.plot([], [])
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Distance (m)')
    ax.set_title('UWB Range (Serial)')
    ax.grid(True)

    t0 = time.time()
    n_ok = 0
    try:
        while True:
            raw = ser.readline()
            if not raw:
                time.sleep(0.01)
                continue
            txt = raw.decode('utf-8', errors='ignore').strip()
            if args.debug:
                print('[RAW]', txt)

            rec = parse_line(txt)
            if not rec:
                continue

            t = time.time() - t0
            xs.append(t)
            ys.append(rec["range_m"])
            all_rows.append({
                "time_s": t,
                "range_m": rec["range_m"],
                "peer": rec["peer"],
                "rx_dbm": rec["rx_dbm"]
            })
            n_ok += 1
            if args.print_every and (n_ok % args.print_every == 0):
                print(f"[OK] t={t:.2f}s  range={rec['range_m']:.3f} m  peer={rec['peer']}  rx={rec['rx_dbm']} dBm")

            # 更新圖
            line.set_xdata(xs); line.set_ydata(ys)
            ax.relim(); ax.autoscale_view()
            plt.draw(); plt.pause(0.001)

    except KeyboardInterrupt:
        print('\n[INFO] 停止讀取。')
    finally:
        try: ser.close()
        except: pass

    if args.save:
        # 固定寫到 results/，如果不存在就建立
        outdir = Path("results").expanduser()
        outdir.mkdir(parents=True, exist_ok=True)

        # 只取檔名（去掉使用者可能給的路徑），統一存到 results/
        out_path = outdir / Path(args.save).name

        df = pd.DataFrame(all_rows)
        if df.empty:
            print(f"[WARN] 沒有任何資料列可寫入，仍建立空檔：{out_path}")
            df.to_csv(out_path, index=False)
        else:
            df.to_csv(out_path, index=False)
            print(f"[INFO] 已儲存 CSV：{out_path}（{len(df)} 筆）")

if __name__ == '__main__':
    main()
