/*

For ESP32 UWB or ESP32 UWB Pro

*/

#include <SPI.h>
#include "DW1000Ranging.h"

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4

// connection pins
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

void setup()
{
    Serial.begin(115200);
    delay(1000);
    //init the configuration
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); //Reset, CS, IRQ pin
    //define the sketch as anchor. It will be great to dynamically change the type of module
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachNewDevice(newDevice);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);
    //Enable the filter to smooth the distance
    //DW1000Ranging.useRangeFilter(true);

    //we start the module as a tag
    DW1000Ranging.startAsTag("BB:BB:BB:BB:BB:BB:BB:01", DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
}

void loop()
{
    DW1000Ranging.loop();
}

unsigned long lastTime = 0; // 用來計算兩次接收的時間差 (Latency)

void newRange()
{
    unsigned long now = millis();
    unsigned long timeDiff = 0;

    // 計算距離上一次收到數據經過了多久 (Latency / 負載指標)
    if (lastTime != 0) {
        timeDiff = now - lastTime;
    }
    lastTime = now;

    // 取得距離數據
    float dist = DW1000Ranging.getDistantDevice()->getRange();

    // 格式化輸出: "DATA,毫秒,公尺"
    // 例如: DATA,105,3.01
    Serial.print("DATA,");
    Serial.print(timeDiff);
    Serial.print(",");
    Serial.println(dist);

    // 註解掉原本的輸出，保持 Serial 乾淨給 Python 讀
    /*
    Serial.print("from: ");
    Serial.print(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
    Serial.print("\t Range: ");
    Serial.print(dist);
    Serial.print(" m");
    Serial.println(" dBm");
    */
}

void newDevice(DW1000Device *device)
{
    Serial.print("ranging init; 1 device added ! -> ");
    Serial.print(" short:");
    Serial.println(device->getShortAddress(), HEX);
}

void inactiveDevice(DW1000Device *device)
{
    Serial.print("delete inactive device: ");
    Serial.println(device->getShortAddress(), HEX);
}
