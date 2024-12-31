#include "BLEDevice.h"
#include "HT_SSD1306Wire.h"
#include "HotButton.h"

enum STATE {
  DISCONNECTED,
  CONNECTING,
  CONNECTED
};

enum NAMES {
  BATTERY_1,
  BATTERY_2,
  WATER_LEVEL,
  COUNT
};

struct DEVICE {
  char address[18];
  char name[10];
  char value[128];
  uint8_t com[8];
  uint8_t state = STATE::DISCONNECTED;
  uint8_t i = 0;
  uint8_t data[35];
  BLERemoteCharacteristic *pRemoteNotification;
  BLERemoteCharacteristic *pRemoteCommand;
  BLEAdvertisedDevice *myDevice;
  BLEUUID service;
  BLEUUID command;
  BLEUUID notification;
  BLEClient *pClient = BLEDevice::createClient();
};
DEVICE devices[NAMES::COUNT];

const int BUTTON = GPIO_NUM_0;
unsigned long loop_Timer = 0;
const unsigned long LOOP_TIME = 500;
unsigned long display_Timer = millis();
const unsigned long DISPLAY_TIME = 120000;
bool scanning = false;

SSD1306Wire oled(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
HotButton button(BUTTON);
BLEScan *pBLEScan;

/*-------------------------------------------------------------------------------------*/

static void do_Notify_CALLBACK(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {

  // Battery 1
  if (pBLERemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString() == devices[NAMES::BATTERY_1].address) {
    float volts = 0;
    float amps = 0;
    int percentage = 0;
    for (int j = 0; j < length; j++) {
      if (pData[j] == 221) {  // Hex DD
        memset(devices[NAMES::BATTERY_1].data, 0, sizeof(devices[NAMES::BATTERY_1].data));
        devices[NAMES::BATTERY_1].i = 0;
      }
      if (devices[NAMES::BATTERY_1].data[1] == 3 && pData[j] == 119 && devices[NAMES::BATTERY_1].i == 33) {  // Hex 77
        volts = (float(int16_t(devices[NAMES::BATTERY_1].data[4] * 256 + devices[NAMES::BATTERY_1].data[5]))) / 100.00;
        amps = (float(int16_t(devices[NAMES::BATTERY_1].data[6] * 256 + devices[NAMES::BATTERY_1].data[7]))) / 100.00;
        percentage = devices[NAMES::BATTERY_1].data[23];
        strcpy(devices[NAMES::BATTERY_1].value, (((String)(uint8_t)percentage) + "%  " + String(amps, 2) + "A").c_str());
      }
      devices[NAMES::BATTERY_1].data[(devices[NAMES::BATTERY_1].i)++] = pData[j];
    }
  };

  // Battery 2
  if (pBLERemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString() == devices[NAMES::BATTERY_2].address) {
    float volts = 0;
    float amps = 0;
    int percentage = 0;
    for (int j = 0; j < length; j++) {
      if (pData[j] == 221) {  // Hex DD
        memset(devices[NAMES::BATTERY_2].data, 0, sizeof(devices[NAMES::BATTERY_2].data));
        devices[NAMES::BATTERY_2].i = 0;
      }
      if (devices[NAMES::BATTERY_2].data[1] == 3 && pData[j] == 119 && devices[NAMES::BATTERY_2].i == 33) {  // Hex 77
        volts = (float(int16_t(devices[NAMES::BATTERY_1].data[4] * 256 + devices[NAMES::BATTERY_1].data[5]))) / 100.00;
        amps = (float(int16_t(devices[NAMES::BATTERY_2].data[6] * 256 + devices[NAMES::BATTERY_2].data[7]))) / 100.00;
        percentage = devices[NAMES::BATTERY_2].data[23];
        strcpy(devices[NAMES::BATTERY_2].value, (((String)(uint8_t)percentage) + "%  " + String(amps, 2) + "A").c_str());
      }
      devices[NAMES::BATTERY_2].data[(devices[NAMES::BATTERY_2].i)++] = pData[j];
    }
  }

  // Water Level
  if (pBLERemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString() == devices[NAMES::WATER_LEVEL].address) {
    float roll = 0;
    if (pData[0] == 85 && pData[1] == 97) {
      roll = float((pData[15] << 8) | pData[14]) / 32768.00 * 180.00;
      strcpy(devices[NAMES::WATER_LEVEL].value, (String(roll, 2)).c_str());
    }
  }

}

class do_Client_CALLBACK : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {
    for (int i = 0; i < NAMES::COUNT; i++) {
      if (pclient->getPeerAddress().toString() == devices[i].address) {
        Serial.print("Device ");
        Serial.print(devices[i].name);
        Serial.println(" paired.");
      }
    }
  }
  void onDisconnect(BLEClient *pclient) {
    for (int i = 0; i < NAMES::COUNT; i++) {
      if (pclient->getPeerAddress().toString() == devices[i].address) {
        devices[i].state = STATE::DISCONNECTED;
        strcpy(devices[i].value, "Scanning...");
        Serial.print("Device ");
        Serial.print(devices[i].name);
        Serial.println(" disconnected.");
        pBLEScan->clearResults();
      }
    }
  }
};

void do_Connect() {
  for (int i = 0; i < NAMES::COUNT; i++) {
    if (devices[i].state == STATE::CONNECTING) {
      devices[i].pClient->setClientCallbacks(new do_Client_CALLBACK());
      devices[i].pClient->connect(devices[i].myDevice);
      devices[i].pClient->setMTU(517);
      BLERemoteService *pRemoteService = devices[i].pClient->getService(devices[i].service);
      if (pRemoteService != nullptr) {
        devices[i].pRemoteCommand = pRemoteService->getCharacteristic(devices[i].command);
        if (devices[i].pRemoteCommand != nullptr) {
          devices[i].pRemoteNotification = pRemoteService->getCharacteristic(devices[i].notification);
          if (devices[i].pRemoteNotification != nullptr) {
            if (devices[i].pRemoteNotification->canNotify()) {
              devices[i].pRemoteNotification->registerForNotify(do_Notify_CALLBACK);
              Serial.print("Device ");
              Serial.print(devices[i].name);
              Serial.println(" connected.");
              devices[i].state = STATE::CONNECTED;
            }
          } else {
            devices[i].pClient->disconnect();
          }
        } else {
          devices[i].pClient->disconnect();
        }
      } else {
        devices[i].pClient->disconnect();
      }
    }
  }
}

void do_Results(BLEScanResults d) {
  for (int h = 0; h < d.getCount(); h++) {
    for (int i = 0; i < NAMES::COUNT; i++) {
      if (devices[i].state == STATE::DISCONNECTED) {
        if (d.getDevice(h).getAddress().toString() == devices[i].address) {
          Serial.print("Device ");
          Serial.print(devices[i].name);
          Serial.println(" found.");
          if (d.getDevice(h).haveServiceUUID() && d.getDevice(h).isAdvertisingService(devices[i].service)) {
            devices[i].myDevice = new BLEAdvertisedDevice(d.getDevice(h));
            devices[i].state = STATE::CONNECTING;
          }
        }
      }
    }
  }
  scanning = false;
}

void do_Scan() {
  if(scanning) return;
  for (int i = 0; i < NAMES::COUNT; i++) {
    if (devices[i].state == STATE::DISCONNECTED) {
      Serial.println("Scan started.");
      pBLEScan->start(5, do_Results, true);
      scanning = true;
      break;
    }
  }
}

void do_Talk() {
  for (int i = 0; i < NAMES::COUNT; i++) {
    if (devices[i].state == STATE::CONNECTED) {
      if (sizeof(devices[i].com) > 0) {
        devices[i].pRemoteCommand->writeValue(devices[i].com, 8);
      }
    }
  }
};

void do_Display() {
  if (display_Timer != 0) {
    if (millis() - display_Timer > DISPLAY_TIME) {
      display_Timer = 0;
      oled.displayOff();
    } else {
      int l = 0;
      oled.clear();
      for (int i = 0; i < NAMES::COUNT; i++) {
        oled.drawString(2, 0 + (i * 18), devices[i].name);
        l = oled.getStringWidth(devices[i].value);
        oled.drawString(128 - l, 0 + (i * 18), devices[i].value);
      }
      oled.display();
    }
  }
}

void do_Button() {
  button.update();
  if (button.isSingleClick()) {
    Serial.println("Single click.");
    display_Timer = millis();
    oled.displayOn();
  }
}

/*-------------------------------------------------------------------------------------*/

void setup() {

  // Start SERIAL
  Serial.begin(115200);
  Serial.println("Starting Camper BLE Monitor ...");

  // Start OLED
  oled.init();
  oled.setFont(ArialMT_Plain_16);

  // Setup BATTERY 1
  strcpy(devices[NAMES::BATTERY_1].address, "a4:c1:38:2f:a3:5a");
  strcpy(devices[NAMES::BATTERY_1].name, "B1");
  strcpy(devices[NAMES::BATTERY_1].value, "Scanning...");
  memcpy(devices[NAMES::BATTERY_1].com, new uint8_t[8]{ 0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77, 0x00 }, 8);
  devices[NAMES::BATTERY_1].service = BLEUUID("0000ff00-0000-1000-8000-00805f9b34fb");
  devices[NAMES::BATTERY_1].command = BLEUUID("0000ff02-0000-1000-8000-00805f9b34fb");
  devices[NAMES::BATTERY_1].notification = BLEUUID("0000ff01-0000-1000-8000-00805f9b34fb");

  // Setup BATTERY 2
  strcpy(devices[NAMES::BATTERY_2].address, "a4:c1:38:21:6d:d5");
  strcpy(devices[NAMES::BATTERY_2].name, "B2");
  strcpy(devices[NAMES::BATTERY_2].value, "Scanning...");
  memcpy(devices[NAMES::BATTERY_2].com, new uint8_t[8]{ 0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77, 0x00 }, 8);
  devices[NAMES::BATTERY_2].service = BLEUUID("0000ff00-0000-1000-8000-00805f9b34fb");
  devices[NAMES::BATTERY_2].command = BLEUUID("0000ff02-0000-1000-8000-00805f9b34fb");
  devices[NAMES::BATTERY_2].notification = BLEUUID("0000ff01-0000-1000-8000-00805f9b34fb");

  // Setup WATER LEVEL
  strcpy(devices[NAMES::WATER_LEVEL].address, "ee:2f:3a:cb:38:50");
  strcpy(devices[NAMES::WATER_LEVEL].name, "WL");
  strcpy(devices[NAMES::WATER_LEVEL].value, "Scanning...");
  memcpy(devices[NAMES::WATER_LEVEL].com, new uint8_t[0]{}, 0); 
  devices[NAMES::WATER_LEVEL].service = BLEUUID("0000ffe5-0000-1000-8000-00805f9a34fb");
  devices[NAMES::WATER_LEVEL].command = BLEUUID("0000ffe9-0000-1000-8000-00805f9a34fb");
  devices[NAMES::WATER_LEVEL].notification = BLEUUID("0000ffe4-0000-1000-8000-00805f9a34fb");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  Serial.println("Startup finished.");
  
}

void loop() {
  do_Button();
  if (millis() - loop_Timer > LOOP_TIME) {
    do_Scan();
    do_Connect();
    do_Talk();
    do_Display();
    loop_Timer = millis();
  }
}
