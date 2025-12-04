import serial
import time
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

# ================= 設定區 (請修改這裡) =================
COM_PORT = '/dev/cu.usbserial-02E2277A'    # Windows 改成 'COMx', Mac 改成 '/dev/tty...'
BAUD_RATE = 115200     # 必須跟 Arduino setup 設定的一樣
MAX_SAMPLES = 200      # 每一輪實驗要抓幾筆數據 (建議 100~200)
# ======================================================

data_latency = [] # 儲存時間差 (ms) - 評估負載
data_dist = []    # 儲存距離 (m)   - 評估準確度

print(f"--- UWB Experiment Tool ---")
print(f"Connecting to {COM_PORT}...")

try:
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=2)
    print("連線成功！開始收集數據...")
    print(f"目標收集: {MAX_SAMPLES} 筆")

    while len(data_dist) < MAX_SAMPLES:
        try:
            # 讀取一行並解碼
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            # 判斷是不是我們定義的格式 "DATA,105,3.02"
            if line.startswith("DATA,"):
                parts = line.split(',')
                if len(parts) == 3:
                    try:
                        latency = int(parts[1])
                        dist = float(parts[2])
                        
                        # 過濾掉第一筆 (通常時間差會是 0)
                        if latency > 0 and latency < 5000: 
                            data_latency.append(latency)
                            data_dist.append(dist)
                            
                            # 即時顯示進度
                            print(f"[{len(data_dist)}/{MAX_SAMPLES}] 延遲: {latency}ms | 距離: {dist:.4f}m")
                    except ValueError:
                        pass
            
        except KeyboardInterrupt:
            print("\n使用者手動停止收集。")
            break
        except Exception as e:
            print("讀取錯誤:", e)
            continue

    ser.close()
    print("\n收集完成！正在生成報表...")

    # ====== 數據分析 ======
    # 轉換成 numpy array 方便計算
    np_dist = np.array(data_dist)
    np_lat = np.array(data_latency)

    avg_dist = np.mean(np_dist)
    std_dist = np.std(np_dist) # 標準差 (越低越穩)
    
    avg_lat = np.mean(np_lat)
    std_lat = np.std(np_lat)

    print("-" * 30)
    print(f"【距離分析】")
    print(f"  平均距離: {avg_dist:.4f} m")
    print(f"  標準差 (誤差波動): {std_dist:.4f} m")
    print(f"【負載分析 (Latency)】")
    print(f"  平均更新間隔: {avg_lat:.2f} ms")
    print(f"  標準差 (延遲抖動): {std_lat:.2f} ms")
    print("-" * 30)

    # ====== 畫圖 ======
    plt.figure(figsize=(12, 6))

    # 圖 1: 距離穩定度 (Distance Stability)
    plt.subplot(1, 2, 1)
    plt.plot(data_dist, marker='o', linestyle='-', color='blue', alpha=0.6, markersize=4)
    plt.axhline(y=avg_dist, color='red', linestyle='--', label=f'Avg: {avg_dist:.4f}m')
    plt.title(f"Distance Stability (Std: {std_dist:.4f}m)")
    plt.xlabel("Sample Index")
    plt.ylabel("Measured Distance (m)")
    plt.legend()
    plt.grid(True, alpha=0.3)

    # 圖 2: 系統負載 (System Load / Latency)
    plt.subplot(1, 2, 2)
    plt.plot(data_latency, marker='x', linestyle='-', color='orange', alpha=0.6, markersize=4)
    plt.axhline(y=avg_lat, color='red', linestyle='--', label=f'Avg: {avg_lat:.1f}ms')
    plt.title(f"System Load / Latency (Avg: {avg_lat:.1f}ms)")
    plt.xlabel("Sample Index")
    plt.ylabel("Time Diff (ms)")
    plt.legend()
    plt.grid(True, alpha=0.3)

    plt.tight_layout()
    
    # 儲存結果
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    filename = f"UWB_Result_{timestamp}"
    # plt.savefig(f"{filename}.png")
    
    # 儲存 CSV 供後續分析
    df = pd.DataFrame({'Latency_ms': data_latency, 'Distance_m': data_dist})
    # df.to_csv(f"{filename}.csv", index=False)
    
    print(f"圖表已儲存為: {filename}.png")
    print(f"原始數據已儲存為: {filename}.csv")
    
    plt.show() # 顯示視窗

except Exception as e:
    print(f"無法打開 {COM_PORT}，請檢查連線或 Port 設定。")
    print("錯誤訊息:", e)