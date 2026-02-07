#include "wled.h"
#ifdef ARDUINO_ARCH_ESP32
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#else
#error "BLE JSON usermod supported only on ESP32"
#endif
#include <ArduinoJson.h>

class BLEJsonUsermod : public Usermod {
private:
  BLEServer* pServer = nullptr;
  BLECharacteristic* pChr = nullptr;
  const char* svcUuid = "12345678-1234-1234-1234-1234567890ab";
  const char* chrUuid = "abcdefab-1234-5678-1234-abcdefabcdef";

  class CharCallback : public BLECharacteristicCallbacks {
    BLEJsonUsermod* parent;
  public:
    CharCallback(BLEJsonUsermod* p) : parent(p) {}

    void onWrite(BLECharacteristic* chr) override {
      std::string v = chr->getValue();
      if (v.empty()) return;

      // parse incoming JSON and hand to WLED's deserializeState()
      PSRAMDynamicJsonDocument doc(JSON_BUFFER_SIZE);
      DeserializationError err = deserializeJson(doc, v.c_str());
      if (err) return;

      JsonObject root = doc.as<JsonObject>();

      // support "get" queries for modes/palettes
      if (root.containsKey("get") && root["get"].is<const char*>()) {
        const char* what = root["get"].as<const char*>();
        PSRAMDynamicJsonDocument outDoc(JSON_BUFFER_SIZE);
        if (strcmp(what, "modes") == 0) {
          JsonArray arr = outDoc.to<JsonArray>();
          serializeModeNames(arr);
        } else if (strcmp(what, "palettes") == 0) {
          JsonObject r = outDoc.to<JsonObject();
          serializePalettes(r, 0);
        } else {
          // unknown; ignore
        }
        std::string out;
        serializeJson(outDoc, out);
        if (pChr) {
          pChr->setValue(out);
          pChr->notify();
        }
        return;
      }

      // default: pass JSON to WLED state deserializer
      deserializeState(root, CALL_MODE_DIRECT_CHANGE);
    }

    void onRead(BLECharacteristic* chr) override {
      // serialize current WLED state and set as characteristic value
      PSRAMDynamicJsonDocument doc(JSON_BUFFER_SIZE);
      JsonObject root = doc.to<JsonObject>();
      serializeState(root, false, true, true, false);
      std::string out;
      serializeJson(doc, out);
      chr->setValue(out);
    }
  };

public:
  void setup() override {
    // disable WiFi to save power
    WiFi.disconnect(true);
    delay(5);
    WiFi.mode(WIFI_OFF);
    noWifiSleep = true;

    // init BLE peripheral
    BLEDevice::init("WLED-BLE");
    pServer = BLEDevice::createServer();
    BLEService* svc = pServer->createService(svcUuid);
    pChr = svc->createCharacteristic(chrUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    static CharCallback cb(this);
    pChr->setCallbacks(&cb);
    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(svcUuid);
    adv->setScanResponse(true);
    adv->start();
  }

  void loop() override {
    static unsigned long lastNotify = 0;
    if (pServer && pServer->getConnectedCount() > 0 && stateChanged) {
      unsigned long now = millis();
      if (now - lastNotify > 200) {
        PSRAMDynamicJsonDocument doc(JSON_BUFFER_SIZE);
        JsonObject root = doc.to<JsonObject>();
        serializeState(root, false, true, true, false);
        std::string out;
        serializeJson(doc, out);
        if (pChr) {
          pChr->setValue(out);
          pChr->notify();
        }
        lastNotify = now;
      }
    }
  }
};

static BLEJsonUsermod ble_json_usermod;
REGISTER_USERMOD(ble_json_usermod);
