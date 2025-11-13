# uwb_packet_ladder.py 
# 用法：
#   python3 uwb_packet_ladder.py \
#   --port 9100 --window 60 \
#   --pair-horizon 0.30 --pair-horizon-full 0.40 \
#   --reorder-guard 0.02 --strict-order \
#   --annotate-dt --ann-last 12 --use-range-fallback \
#   --save-csv cycles.csv --csv-flush-every 1 --debug
import argparse
import re
import socket
import time
import csv
from collections import deque, namedtuple
from pathlib import Path
from typing import Optional

def lazy_matplotlib():
    """
    延遲載入 matplotlib 並設定中文字型，避免非必要時增加啟動時間
    Returns:
        module: 已設定中文字型的 matplotlib.pyplot 模組
    """
    import matplotlib.pyplot as plt
    plt.rcParams['font.sans-serif'] = [
        'PingFang TC','Heiti TC','Arial Unicode MS',
        'Microsoft JhengHei','Noto Sans CJK TC','DejaVu Sans'
    ]
    plt.rcParams['axes.unicode_minus'] = False
    return plt

# ---- 事件格式（容忍 RV/RCV/RECV、TXOK/TX_DONE、HB）----
PAT_TXRX  = re.compile(
    r'role=(?P<role>TAG|ANCHOR)\s+evt=(?P<evt>TX(?:OK)?|TX_DONE|RX|RV|RCV|RECV)\s+'
    r'(?:dev_ts|host_ts)=(?P<ts>\d+)', re.I
)
PAT_RANGE = re.compile(
    r'role=(?P<role>TAG|ANCHOR)\s+evt=RANGE\s+(?:dev_ts|host_ts)=(?P<ts>\d+)', re.I
)
PAT_HB    = re.compile(
    r'role=(?P<role>TAG|ANCHOR)\s+evt=(?:HEARTBEAT|HB)(?:\s+(?:dev_ts|host_ts)=(?P<ts>\d+))?',
    re.I
)

def parse_line(msg: str) -> Optional[dict]:
    """解析 UDP 訊息中的事件，回傳包含角色、種類、事件、裝置時間戳的字典；無法解析則回傳 None

    Args:
        msg (str): UDP 收到的字串訊息

    Returns:
        Optional[dict]: 包含 "role", "kind", "evt", "ts" 的字典
    """
    m = PAT_TXRX.search(msg)
    if m:
        evt_raw = m.group('evt').upper()
        if evt_raw.startswith('TX'):
            evt = 'TX'
        elif evt_raw in ('RX','RV','RCV','RECV'):
            evt = 'RX'
        else:
            evt = evt_raw
        return {'role': m.group('role').upper(), 'kind': 'TXRX', 'evt': evt, 'ts': int(m.group('ts'))}
    m = PAT_RANGE.search(msg)
    if m:
        return {'role': m.group('role').upper(), 'kind': 'RANGE', 'evt': 'RANGE', 'ts': int(m.group('ts'))}
    m = PAT_HB.search(msg)
    if m:
        ts = m.group('ts')
        return {'role': m.group('role').upper(), 'kind': 'HEARTBEAT', 'evt': 'HEARTBEAT', 'ts': int(ts) if ts else 0}
    return None

# 結構：主機時間（host now）為時間軸
Half = namedtuple('Half','id t_tx t_rx')               # TAG:TX -> ANCHOR:RX
Full = namedtuple('Full','id t_tx1 t_rx1 t_tx2 t_rx2') # + ANCHOR:TX -> TAG:RX
RPair= namedtuple('RPair','id t_tag t_anc')            # RANGE 後備（TAG↔ANCHOR）

def main() -> None:
    """
    主程式：從 UDP 讀取 UWB 封包事件並即時繪圖
    1. 解析命令列參數
    2. 開啟 UDP socket
    3. 進入讀取迴圈，解析每行資料並更新圖表
    4. 停止後儲存 CSV 檔（如果指定）
    5. 清理資源並結束
    """
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=9100, help='UDP 監聽埠')
    ap.add_argument('--window', type=float, default=.0, help='圖上顯示最近幾秒')
    ap.add_argument('--pair-horizon', type=float, default=0.3, help='半循環 (TAG:TX→ANCHOR:RX) 配對門檻（秒）')
    ap.add_argument('--pair-horizon-full', type=float, default=0.4, help='完整循環回程 (ANCHOR:TX→TAG:RX) 門檻（秒）')
    ap.add_argument('--reorder-guard', type=float, default=0.02, help='配對前等待以吸收亂序（秒）')
    ap.add_argument('--strict-order', action='store_true', help='回程配對要求 t3>=t_rx 且 t4>=t3，避免負的 host Δt')
    ap.add_argument('--annotate-dt', action='store_true', help='在箭頭上標註 Δt（毫秒）')
    ap.add_argument('--ann-last', type=int, default=12, help='只標註最近 N 組 Δt/箭頭')
    ap.add_argument('--full-only', action='store_true', help='只畫「完整循環」箭頭')
    ap.add_argument('--use-range-fallback', action='store_true', help='啟用 RANGE 後備循環配對')
    ap.add_argument('--save-csv', default=None, help='輸出 CSV 檔名')
    ap.add_argument('--csv-flush-every', type=int, default=1, help='每寫入幾筆完整循環就 flush 一次（1=每筆）')
    ap.add_argument('--no_gui', action='store_true', help='不開圖形（純收錄/CSV）')
    ap.add_argument('--debug', action='store_true', help='印 RAW 訊息')
    args = ap.parse_args()

    # UDP
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', args.port))
    sock.settimeout(0.2)
    print(f"[INFO] listening on UDP 0.0.0.0:{args.port}  (Ctrl+C 停止)")

    # GUI
    if not args.no_gui:
        plt = lazy_matplotlib()
        plt.ion()
        fig = plt.figure(figsize=(9, 7.5))
        ax_seq = fig.add_subplot(211)
        ax_rt  = fig.add_subplot(212)

    # 緩衝與統計
    buf = deque()    # (t_host, role, kind, evt, ts_dev)
    pps = deque()    # events/s（不含 HB）
    cps = deque()    # cycles/s

    tag_tx, anc_rx, anc_tx, tag_rx = deque(), deque(), deque(), deque()
    tag_rng, anc_rng = deque(), deque()
    halves, fulls, rpairs = deque(), deque(), deque()

    next_id = 1
    t0 = time.time()
    last_bin_evt, cnt_evt = None, 0
    last_bin_cyc, cnt_cyc = None, 0

    # CSV
    csv_writer = None
    csv_fh = None
    full_count_since_flush = 0
    if args.save_csv:
        # 目標資料夾固定為 results；若不存在就建立
        outdir = Path("results").expanduser()
        outdir.mkdir(parents=True, exist_ok=True)

        # 只取檔名（去掉使用者可能給的路徑），一律存到 results/
        out_path = outdir / Path(args.save_csv).name

        csv_fh = open(out_path, 'w', newline='')
        csv_writer = csv.writer(csv_fh)
        csv_writer.writerow([
            'type','cycle_id','t_tx1','t_rx1','t_tx2','t_rx2',
            'host_dt1_ms','host_dt2_ms','t_tag_range','t_anc_range','dt_range_ms'
        ])
        csv_fh.flush()
        print(f"[INFO] CSV 會寫到：{out_path}")

    def push_cyc(now) -> None:
        """
        更新循環速率統計

        Args:
            now (float): 主機時間戳
        """
        nonlocal last_bin_cyc, cnt_cyc
        sec_bin = int(now - t0)
        if last_bin_cyc is None: last_bin_cyc = sec_bin
        if sec_bin != last_bin_cyc:
            cps.append((last_bin_cyc, cnt_cyc))
            while cps and (now - (t0 + cps[0][0])) > args.window: cps.popleft()
            cnt_cyc = 0; last_bin_cyc = sec_bin
        cnt_cyc += 1

    def add_cycle_full(h, t3, t4, now) -> None:
        """
        新增完整循環並更新統計
        Args:
            h (Half): 對應的半循環事件
            t3 (float): ANCHOR:TX 主機時間戳
            t4 (float): TAG:RX 主機時間戳
            now (float): 主機時間戳
        """
        nonlocal full_count_since_flush
        fulls.append(Full(h.id, h.t_tx, h.t_rx, t3, t4))
        push_cyc(max(t3, t4))
        dt1 = (h.t_rx - h.t_tx) * 1000.0
        dt2 = (t4 - t3) * 1000.0
        print(f"[CYCLE #{h.id} FULL] TAG→ANCHOR Δ={dt1:.1f} ms, ANCHOR→TAG Δ={dt2:.1f} ms")
        if csv_writer:
            csv_writer.writerow(['FULL', h.id, h.t_tx, h.t_rx, t3, t4, dt1, dt2, '', '', ''])
            full_count_since_flush += 1
            if full_count_since_flush >= args.csv_flush_every:
                csv_fh.flush(); full_count_since_flush = 0

    def add_cycle_half(cid, t1, t2, now) -> None:
        """
        新增半循環並更新統計

        Args:
            cid (int): 循環 ID
            t1 (float): TAG:TX 主機時間戳
            t2 (float): ANCHOR:RX 主機時間戳
            now (float): 主機時間戳
        """
        push_cyc(t2)
        if csv_writer:
            dt1 = (t2 - t1) * 1000.0
            csv_writer.writerow(['HALF', cid, t1, t2, '', '', dt1, '', '', '', ''])
            csv_fh.flush()

    def add_cycle_range(t_tag, t_anc, cid, now) -> None:
        """
        新增範圍循環並更新統計

        Args:
            t_tag (float): TAG 主機時間戳
            t_anc (float): ANCHOR 主機時間戳
            cid (int): 循環 ID
            now (float): 主機時間戳
        """
        rpairs.append(RPair(cid, t_tag, t_anc))
        push_cyc(max(t_tag, t_anc))
        dt = abs(t_anc - t_tag) * 1000.0
        print(f"[CYCLE #{cid} RANGE] TAG↔ANCHOR Δ≈{dt:.1f} ms")
        if csv_writer:
            csv_writer.writerow(['RANGE', cid, '', '', '', '', '', '', t_tag, t_anc, dt])
            csv_fh.flush()

    def try_match(now) -> None:
        """
        嘗試配對半循環與完整循環事件
        Args:
            now (float): 主機時間戳
        """
        nonlocal next_id
        half_h = args.pair_horizon
        full_h = args.pair_horizon_full if args.pair_horizon_full is not None else args.pair_horizon

        # half：TAG:TX -> ANCHOR:RX（先等一點時間吸收亂序）
        while tag_tx and anc_rx:
            if (now - tag_tx[0]) < args.reorder_guard or (now - anc_rx[0]) < args.reorder_guard:
                break
            t1 = tag_tx[0]
            cands = [t for t in anc_rx if abs(t - t1) <= half_h]
            if not cands:
                # 丟掉過舊的一端
                if anc_rx and (anc_rx[0] < t1 - half_h): anc_rx.popleft()
                else: tag_tx.popleft()
            else:
                t2 = min(cands, key=lambda x: abs(x - t1))
                # tag_tx.popLeft = False  # 只為了避免編輯器誤自動完成，高度可見；下一行才是真正操作
                tag_tx.popleft(); anc_rx.remove(t2)
                cid = next_id
                halves.append(Half(cid, t1, t2))
                if csv_writer:
                    add_cycle_half(cid, t1, t2, now)
                next_id += 1

        # full：補回程 ANCHOR:TX -> TAG:RX
        i = 0
        while i < len(halves) and anc_tx and tag_rx:
            h = halves[i]
            if args.strict_order:
                cand_tx = [t for t in anc_tx if (t >= h.t_rx) and (t - h.t_rx) <= full_h]
                if not cand_tx: i += 1; continue
                t3 = min(cand_tx)
                cand_rx = [t for t in tag_rx if (t >= t3) and (t - t3) <= full_h]
                if not cand_rx: i += 1; continue
                t4 = min(cand_rx)
            else:
                cand_tx = [t for t in anc_tx if abs(t - h.t_rx) <= full_h]
                if not cand_tx: i += 1; continue
                t3 = min(cand_tx, key=lambda x: abs(x - h.t_rx))
                cand_rx = [t for t in tag_rx if abs(t - t3) <= full_h]
                if not cand_rx: i += 1; continue
                t4 = min(cand_rx, key=lambda x: abs(x - t3))

            anc_tx.remove(t3); tag_rx.remove(t4)
            add_cycle_full(h, t3, t4, now)
            halves.remove(h)

        # 丟掉超窗的 half
        while halves and (now - halves[0].t_tx) > args.window:
            halves.popleft()

        # RANGE 後備
        if args.use_range_fallback:
            while tag_rng and anc_rng:
                if (now - tag_rng[0]) < args.reorder_guard or (now - anc_rng[0]) < args.reorder_guard:
                    break
                t_tag = tag_rng[0]
                cand = [t for t in anc_rng if abs(t - t_tag) <= half_h]
                if not cand:
                    if anc_rng and (anc_rng[0] < t_tag - half_h): anc_rng.popleft()
                    else: tag_rng.popleft()
                    continue
                t_anc = min(cand, key=lambda x: abs(x - t_tag))
                tag_rng.popleft(); anc_rng.remove(t_anc)
                add_cycle_range(t_tag, t_anc, next_id, now)
                next_id += 1

    def redraw(now) -> None:
        """
        重畫圖表

        Args:
            now (float): 主機時間戳
        """
        if args.no_gui: return
        ax_seq.clear(); ax_rt.clear()
        lanes = {'TAG':0.1,'ANCHOR':0.9}
        ax_seq.set_xlim(0,1); ax_seq.set_xticks([lanes['TAG'],lanes['ANCHOR']])
        ax_seq.set_xticklabels(['TAG','ANCHOR'])
        ax_seq.set_ylabel('time (s, host ref)')
        ax_seq.set_title('UWB Packet Message Sequence (live)')

        # 點
        for (t,r,k,e,_) in buf:
            if k=='TXRX': ax_seq.plot([lanes[r]],[t - t0], marker='o', ms=3, label='_nolegend_')
            if k=='RANGE': ax_seq.plot([lanes[r]],[t - t0], marker='s', ms=4, label='_nolegend_')

        # half 箭頭（只標註最近 N）
        if not args.full_only:
            for h in list(halves)[-args.ann_last:]:
                ax_seq.annotate('', xy=(0.9, h.t_rx - t0), xytext=(0.1, h.t_tx - t0),
                                arrowprops=dict(arrowstyle='->', lw=1.2))
                if args.annotate_dt:
                    dt=(h.t_rx-h.t_tx)*1000.0
                    ax_seq.text(0.5,(h.t_tx+h.t_rx)/2 - t0,f'Δ{dt:.1f} ms',ha='center',fontsize=8)

        # 完整循環箭頭（只標註最近 N）
        for f in list(fulls)[-args.ann_last:]:
            ax_seq.annotate('', xy=(0.9, f.t_rx1 - t0), xytext=(0.1, f.t_tx1 - t0),
                            arrowprops=dict(arrowstyle='->', lw=1.8))
            ax_seq.annotate('', xy=(0.1, f.t_rx2 - t0), xytext=(0.9, f.t_tx2 - t0),
                            arrowprops=dict(arrowstyle='->', lw=1.8))
            if args.annotate_dt:
                ax_seq.text(0.5,(f.t_tx1+f.t_rx1)/2 - t0,f'Δ{(f.t_rx1-f.t_tx1)*1000:.1f} ms',
                            ha='center',fontsize=8)
                ax_seq.text(0.5,(f.t_tx2+f.t_rx2)/2 - t0,f'Δ{(f.t_rx2-f.t_tx2)*1000:.1f} ms',
                            ha='center',fontsize=8)

        # RANGE 後備（也限最近 N）
        for rp in list(rpairs)[-args.ann_last:]:
            ax_seq.annotate('', xy=(0.9, rp.t_anc - t0), xytext=(0.1, rp.t_tag - t0),
                            arrowprops=dict(arrowstyle='<->', lw=1.4))

        ax_seq.set_ylim(max(0,(now-t0)-args.window),(now-t0))

        # 事件/循環速率
        labels_drawn = False
        if pps:
            xs=[t0+b for (b,c) in pps]; ys=[c for (b,c) in pps]
            ax_rt.plot([x-t0 for x in xs], ys, marker='o', label='events/s'); labels_drawn = True
        if cps:
            xs2=[t0+b for (b,c) in cps]; ys2=[c for (b,c) in cps]
            ax_rt.plot([x-t0 for x in xs2], ys2, marker='x', label='cycles/s'); labels_drawn = True
        ax_rt.set_xlim(max(0,(now-t0)-args.window),(now-t0))
        ax_rt.set_xlabel('time (s, host ref)'); ax_rt.set_ylabel('rate'); ax_rt.set_title('Events & Cycles')
        if labels_drawn: ax_rt.legend(loc='upper right')
        if not buf:
            ax_seq.text(0.5,(now-t0)-0.1,f"等待事件中…（UDP {args.port}）",ha='center')

        # 避免過度刷新
        import matplotlib.pyplot as plt
        plt.pause(0.05)

    try:
        while True:
            try:
                data,_ = sock.recvfrom(2048)
                msg = data.decode('utf-8','ignore').strip()
            except socket.timeout:
                msg = None
            now = time.time()

            if msg:
                if args.debug: print('[RAW]', msg)
                rec = parse_line(msg)
                if rec:
                    role, kind, evt, ts = rec['role'], rec['kind'], rec['evt'], rec['ts']
                    buf.append((now, role, kind, evt, ts))
                    # 分流（配對一律用 host now）
                    if kind=='TXRX':
                        if role=='TAG' and evt=='TX': tag_tx.append(now)
                        if role=='ANCHOR' and evt=='RX': anc_rx.append(now)
                        if role=='ANCHOR' and evt=='TX': anc_tx.append(now)
                        if role=='TAG' and evt=='RX': tag_rx.append(now)
                    elif kind=='RANGE':
                        if role=='TAG': tag_rng.append(now)
                        else: anc_rng.append(now)
                    # 事件速率（不含 HB）
                    if kind!='HEARTBEAT':
                        cur = int(now - t0)
                        if last_bin_evt is None: last_bin_evt = cur
                        if cur != last_bin_evt:
                            pps.append((last_bin_evt, cnt_evt))
                            while pps and (now - (t0 + pps[0][0])) > args.window: pps.popleft()
                            cnt_evt = 0; last_bin_evt = cur
                        cnt_evt += 1

            # 丟舊資料
            while buf and (now - buf[0][0]) > args.window:
                buf.popleft()

            # 配對與重畫
            try_match(now)
            redraw(now)
            time.sleep(0.02)

    except KeyboardInterrupt:
        print("\n[INFO] 停止。")
    finally:
        if csv_fh: csv_fh and csv_fh.flush(); 
        if csv_fh: csv_fh.close()
        sock.close()
        print("[INFO] 結束。CSV（若有）已關檔。")

if __name__ == '__main__':
    main()
