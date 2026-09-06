#include "tablet_hid.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>

namespace {

// アドバタイズ名。タブレット側のBluetooth設定画面にはこの名前で表示される。
const char *BLE_DEVICE_NAME = "AutoToilet-Tablet-HID";

const uint8_t REPORT_ID = 1;

// System ControlとKeyboardを別々のReportキャラクタリスティック(0x2a4d)として
// 複数作る構成(inputReport()を複数回呼ぶ)は、arduino-esp32のBluedroidバックエンドの
// BLEHIDDeviceで notify() 時にクラッシュ(LoadProhibited、
// BLECharacteristic::notify() → BLEService::getServer())する既知の不具合に
// 当たったため、1つのReport(1キャラクタリスティック)にすべてのフィールドを
// まとめる構成にしている。
// レポート形式(2バイト固定):
//   byte0: System Control選択値(0=なし/1=Power Down/2=Sleep/3=Wake Up)
//   byte1: キーボードキーコード(USB HID Keyboard/Keypad Usage。0=キー無し)
// ロック画面がマウスのドラッグ(スワイプ)には反応しなかったが、キーボードの
// スペースキー押下では解除できることを実機で確認したため、スワイプではなく
// スペースキー送信でロック解除する。
const uint8_t kReportMap[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Controls)
    0x09, 0x80,       // Usage (System Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, REPORT_ID,  //   Report ID

    //-- byte0: System Control(Power Down/Sleep/Wake Up) --
    0x19, 0x81,  //   Usage Minimum (System Power Down)
    0x29, 0x83,  //   Usage Maximum (System Wake Up)
    0x15, 0x01,  //   Logical Minimum (1)
    0x25, 0x03,  //   Logical Maximum (3)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x00,  //   Input (Data, Array, Absolute)

    //-- byte1: キーボードキーコード(1キーのみ、修飾キー無し) --
    0x05, 0x07,  //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,  //   Usage Minimum (Reserved, no event)
    0x29, 0x65,  //   Usage Maximum (Keyboard Application)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (0x65)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x00,  //   Input (Data, Array, Absolute)

    0xC0,  // End Collection
};

const uint8_t USAGE_SYSTEM_SLEEP = 2;
const uint8_t USAGE_NONE = 0;

const uint8_t KEY_NONE = 0x00;
const uint8_t KEY_SPACE = 0x2C;  // USB HID Keyboard Usage: Spacebar
const uint8_t KEY_F = 0x09;      // USB HID Keyboard Usage: F(WebUI側の全画面復帰ショートカット)

// HIDレポート送信後、ホスト側に「押下」を確実に認識させてから解放するまでの猶予。
const uint32_t HID_RELEASE_DELAY_MS = 30;

// Wake送信後、タブレットの画面/タッチ入力が反応可能になるまでの待ち時間。
// 短すぎるとキー送信がロック画面表示前に届いてしまい無視される。
const uint32_t WAKE_TO_UNLOCK_DELAY_MS = 1000;

// ロック解除で全画面表示が解除されてしまうため、解除後に全画面復帰用の
// Fキーを送るまでの待ち時間。
const uint32_t UNLOCK_TO_FULLSCREEN_DELAY_MS = 1000;

BLEHIDDevice *hidDevice = nullptr;
BLECharacteristic *reportChar = nullptr;
BLEAdvertising *advertising = nullptr;
bool tabletConnected = false;

class TabletServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override {
        tabletConnected = true;
        Serial.println("[BLE-HID] タブレットが接続しました");
    }
    void onDisconnect(BLEServer *server) override {
        tabletConnected = false;
        Serial.println("[BLE-HID] タブレットが切断されました。再アドバタイズを開始します");
        // 再接続を即座に受け付けられるよう、切断のたびに広告を再開する。
        if (advertising != nullptr) advertising->start();
    }
};

// 接続が保たれている間だけ1レポート(2バイト)を送信する。
bool sendReport(uint8_t systemControl, uint8_t keyCode) {
    if (!tabletConnected) return false;
    uint8_t report[2] = {systemControl, keyCode};
    reportChar->setValue(report, 2);
    reportChar->notify();
    return true;
}

void sendSystemControlUsage(uint8_t usage) {
    if (!sendReport(usage, KEY_NONE)) return;
    delay(HID_RELEASE_DELAY_MS);
    sendReport(USAGE_NONE, KEY_NONE);
}

// 1キーだけ押して離す(修飾キー無し)。
void performKeyPress(uint8_t key) {
    if (!sendReport(USAGE_NONE, key)) return;
    delay(HID_RELEASE_DELAY_MS);
    sendReport(USAGE_NONE, KEY_NONE);
}

}  // namespace

void tabletHidBegin() {
    BLEDevice::init(BLE_DEVICE_NAME);

    // タブレット側にパスワード入力を求めない(IO capability無し)。
    // ペアリング可否の確認自体はタブレット側のBluetooth設定操作で行う。
    BLESecurity *security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new TabletServerCallbacks());

    hidDevice = new BLEHIDDevice(server);
    reportChar = hidDevice->inputReport(REPORT_ID);

    hidDevice->manufacturer()->setValue("DIY auto-toilet");
    hidDevice->pnp(0x02, 0xCAFE, 0x0001, 0x0100);
    hidDevice->hidInfo(0x00, 0x01);
    hidDevice->reportMap((uint8_t *)kReportMap, sizeof(kReportMap));
    hidDevice->startServices();

    advertising = server->getAdvertising();
    advertising->setAppearance(HID_KEYBOARD);
    advertising->addServiceUUID(hidDevice->hidService()->getUUID());
    advertising->start();

    Serial.println("[BLE-HID] アドバタイズ開始。タブレット側のBluetooth設定から\"" + String(BLE_DEVICE_NAME) + "\"とペアリングしてください");
}

void tabletHidWake() {
    if (!tabletConnected) {
        Serial.println("[BLE-HID][WARN] タブレット未接続のためWake/解除をスキップしました");
        return;
    }
    // System Wake Up(System Control)ではなく、動作確認済みのスペースキーで
    // 画面点灯も行う。HIDレポートが実際にタブレットへ届いているかを、
    // このWake時点の反応(画面が点くかどうか)で切り分けられるようにするため。
    Serial.println("[BLE-HID] 画面点灯(スペースキー)を送信");
    performKeyPress(KEY_SPACE);

    delay(WAKE_TO_UNLOCK_DELAY_MS);

    Serial.println("[BLE-HID] ロック画面解除(スペースキー)を送信");
    performKeyPress(KEY_SPACE);

    // ロック解除でWebUIの全画面表示が解除されてしまうため、Fキーで復帰させる
    // (WebUI側はFキー押下で全画面表示に戻すショートカットを実装している)。
    delay(UNLOCK_TO_FULLSCREEN_DELAY_MS);
    Serial.println("[BLE-HID] 全画面復帰(Fキー)を送信");
    performKeyPress(KEY_F);
}

void tabletHidSleep() {
    if (!tabletConnected) {
        Serial.println("[BLE-HID][WARN] タブレット未接続のためSleepをスキップしました");
        return;
    }
    Serial.println("[BLE-HID] System Sleepを送信(画面OFF+ロック)");
    sendSystemControlUsage(USAGE_SYSTEM_SLEEP);
}
