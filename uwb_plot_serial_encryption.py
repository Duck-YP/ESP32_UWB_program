# 檔名：uwb_decrypt_serial.py
# 功能：從 Serial 讀取 Tag 送來的 AES-GCM 封包，解密出原本的 "ADDR,distance" 字串

import serial
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# ========= AES-GCM 參數（要跟 ESP32 上完全一致） =========
AES_KEY = bytes([
    0x11, 0x22, 0x33, 0x44,
    0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC,
    0xDD, 0xEE, 0xFF, 0x00,
])

AES_IV_LEN  = 12
AES_TAG_LEN = 16
PACKET_VERSION = 0x01

# ========= Serial 連線設定 =========
ser = serial.Serial(
    port="/dev/cu.usbserial-02E2277A",  # 請改成你的實際埠名
    baudrate=115200,
    timeout=1.0,
)

print("Serial opened:", ser.port)

def read_one_packet():
    """
    簡易版讀封包：
    - 先讀 1 byte 當 version
    - 再讀 12-byte IV + 16-byte Tag
    - 再一次把剩下 buffer 當作 ciphertext（示範用）
    實務上建議你在封包前面加上「長度欄位」會更穩。
    """
    # 1. 讀 1 byte version
    v = ser.read(1)
    if len(v) != 1:
        return None
    version = v[0]
    if version != PACKET_VERSION:
        print("Unknown version:", version)
        return None

    # 2. 讀 IV + Tag
    header = ser.read(AES_IV_LEN + AES_TAG_LEN)
    if len(header) != AES_IV_LEN + AES_TAG_LEN:
        print("Header not complete")
        return None

    iv  = header[:AES_IV_LEN]
    tag = header[AES_IV_LEN:]

    # 3. 讀 ciphertext（這裡暫時「把目前 buffer 全部讀出來」）
    #    若你發現有時候會黏包 / 斷包，就要改封包格式，多加 length。
    ciphertext = ser.read(ser.in_waiting or 1)
    if len(ciphertext) == 0:
        print("No ciphertext data")
        return None

    # 4. 解密
    aesgcm = AESGCM(AES_KEY)

    try:
        # cryptography 的 AESGCM 期待密文格式為：ciphertext + tag
        ct_plus_tag = ciphertext + tag
        plaintext = aesgcm.decrypt(iv, ct_plus_tag, None)
        return plaintext.decode("utf-8", errors="ignore")
    except Exception as e:
        print("Decrypt failed:", e)
        return None

while True:
    msg = read_one_packet()
    if not msg:
        continue
    msg = msg.strip()
    print("Decrypted:", msg)

    # 解析 "ADDR,distance"
    try:
        addr_str, dist_str = msg.split(",")
        print("Address:", addr_str, "Distance:", float(dist_str), "m")
    except Exception as e:
        print("Parse error:", e)
