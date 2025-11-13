# uwb_plot_udp.py 
# 用法：
#   python3 uwb_plot_udp.py --port 9000 --save run_udp.csv
#   （可加 --debug 看每個收到的封包內容；--print-every 10 代表每 10 筆資料印 1 行）

import argparse
import re
import socket
import time
import matplotlib.pyplot as plt
import pandas as pd
from collections import deque
from typing import Optional
from pathlib import Path 

def parse_line(msg: str) -> Optional[float]:
    """解析 UDP 訊息中的距離值，回傳公尺數；無法解析則回傳 None

    Args:
        msg (str): UDP 收到的字串訊息
    
    Returns:
        Optional[float]: 公尺數距離值，或 None 如果無法解析
    """
    m = re.search(r'Range:\s*([\-]?\d+(?:\.\d+)?)\s*(cm|m)?', msg, re.IGNORECASE)
    if m:
        val = float(m.group(1))
        unit = (m.group(2) or 'm').lower()
        if unit == 'cm':
            val /= 100.0
        return val
    m2 = re.search(r'(?:distance[:=]\s*|d\s*=\s*|range\s+)(\-?\d+(?:\.\d+)?)\s*(cm|m)?', msg, re.IGNORECASE)
    if m2:
        val = float(m2.group(1))
        unit = (m2.group(2) or 'm').lower()
        if unit == 'cm':
            val /= 100.0
        return val
    return None

def main() -> None:
    """
    主程式：從 UDP 讀取 UWB 距離資料並即時繪圖
    1. 解析命令列參數
    2. 開啟 UDP socket
    3. 進入讀取迴圈，解析每行資料並更新圖表
    4. 停止後儲存 CSV 檔（如果指定）
    5. 清理資源並結束
    """
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=9000)
    ap.add_argument('--save', default=None)
    ap.add_argument('--window', type=int, default=300)
    ap.add_argument('--print-every', dest='print_every', type=int, default=0)
    ap.add_argument('--debug', action='store_true', help='列印每個收到的封包內容')
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', args.port))
    sock.settimeout(0.5)
    print(f"[INFO] UDP listen on 0.0.0.0:{args.port}  (Ctrl+C 結束)", flush=True)

    xs, ys = deque(maxlen=args.window), deque(maxlen=args.window)
    all_t, all_d = [], []

    plt.ion()
    fig = plt.figure()
    ax = fig.add_subplot(111)
    line, = ax.plot([], [])
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Distance (m)')
    ax.set_title('UWB Range (UDP)')
    ax.grid(True)

    t0 = time.time()
    n_ok = 0
    try:
        while True:
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                plt.pause(0.001)
                continue

            msg = data.decode('utf-8', errors='ignore').strip()
            if args.debug:
                print(f"[RAW] from {addr}: {msg}", flush=True)

            d = parse_line(msg)
            if d is None:
                continue

            t = time.time() - t0
            xs.append(t); ys.append(d)
            all_t.append(t); all_d.append(d)
            n_ok += 1

            if args.print_every and (n_ok % args.print_every == 0):
                print(f"[OK] t={t:.2f}s  range={d:.3f} m  msg='{msg[:60]}'", flush=True)

            line.set_xdata(xs); line.set_ydata(ys)
            ax.relim(); ax.autoscale_view()
            plt.draw(); plt.pause(0.001)
    except KeyboardInterrupt:
        print("\n[INFO] 停止。", flush=True)
    finally:
        sock.close()

    if args.save:
        # 統一集中到 results/，自動建立資料夾
        outdir = Path("results")
        outdir.mkdir(parents=True, exist_ok=True)

        # 不吃使用者給的路徑，只取檔名部分統一丟進 results/
        out_path = outdir / Path(args.save).name

        pd.DataFrame({'time_s': all_t, 'distance_m': all_d}).to_csv(out_path, index=False)
        print(f'[INFO] 已儲存 CSV：{out_path.resolve()}', flush=True)

if __name__ == '__main__':
    main()
