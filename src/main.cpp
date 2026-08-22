#include <Arduino.h>
#include <M5Unified.h>
#include <ld2410.h>
#include <unit_audioplayer.hpp>
#include <Preferences.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "state.h"
#include "toilet_ir.h"
#include "bgm_control.h"
#include "led_control.h"
#include "scenes.h"
#include "web_api.h"

// ============================================================
// ハードウェア接続 (M5StampS3 PIN2.54版)
//   ミリ波センサー LD2410   : UART   TX=G9  RX=G7
//   Audio Playerモジュール  : UART   TX=G3  RX=G5
//   赤外線LED送信回路(自作) : GPIO   G1 (送信専用。ベース抵抗経由でNPNトランジスタを駆動しIR LEDを点灯)
//   状態表示                : 内蔵RGB LED(G21固定、M5Unifiedが自動初期化しM5.Ledで制御)
//   本体ボタン(G0)           : 押下でトイレ流し(赤外線送信)を手動実行
//   本体スピーカー・LCDは非搭載のため使用しない
//
//   ※G43/G44(U0Tx/U0Rx)は使用しない。ESP-IDFのシステムコンソールUART(UART0)が
//   デフォルトでこの2ピンに固定されており(sdkconfig: CONFIG_ESP_CONSOLE_UART_NUM=0)、
//   Arduino側のSerialをUSB CDCへ切り替えても内部的な競合が残り、UART2として
//   再割り当てしてもジャンパ線での直結ループバックすら成立しなかったため。
// ============================================================
static const int RADAR_RX_PIN = 7; // StampS3 RX ← LD2410 TX
static const int RADAR_TX_PIN = 9; // StampS3 TX → LD2410 RX
static const uint32_t RADAR_BAUD = 256000;
static const int AUDIO_TX_PIN = 5;  // StampS3 TX → Audio Playerモジュール RX
static const int AUDIO_RX_PIN = 3;  // StampS3 RX ← Audio Playerモジュール TX
static const int IR_TX_PIN = 1;     // 赤外線LED送信回路(自作)への出力

static const uint8_t LED_BRIGHTNESS_ACTIVE = 179; // 入室・滞在中 約70%
static const uint8_t LED_BRIGHTNESS_OFF = 0;      // 退室中 消灯

// ---- 赤外線送信 ----
IRsend irSender(IR_TX_PIN);
const uint16_t IR_FREQUENCY_KHZ = 38;

// ---- ミリ波センサー(LD2410) ----
ld2410 radar;
HardwareSerial radarSerial(2);
// presence detectedの判定をそのまま使うため、センサー内部の無人確定時間を設定する。
static const uint16_t SENSOR_NO_ONE_DURATION_SEC = 0;

// ---- 検知チャタリング対策(デバウンス) ----
// 生のpresenceDetected()は近距離ゲートかつ低感度でノイズにより短時間でON/OFFを繰り返すことがあるため、
// 上下カウンタ方式のヒステリシスで平滑化してから状態遷移に使う。
// ON/OFFで閾値を非対称にし、「あり」への反応は速く、「なし」への反応は遅くする
// (在室中に一瞬だけ未検知フレームが来ても退室扱いにならないようにする)。
// ※ 新しいフレームを受信した時だけ呼ばれる想定(loop()を参照)
static const uint8_t PRESENCE_DEBOUNCE_ON_COUNT  = 2;
static const uint8_t PRESENCE_DEBOUNCE_OFF_COUNT = 20;
uint8_t presenceDebounceCounter = 0;
bool smoothedPresence = false;

// ---- Home Assistant連携(WiFi + MQTT) ----
// 人感センサーの状態(smoothedPresence)をMQTT経由でHome Assistantへ通知する。
// HomeAssistantのMQTT Discoveryに対応しているため、ブローカーに接続すると
// binary_sensor(occupancy)が自動で登録される。
// ★ SSID/パスワード/ブローカーIPは環境に合わせて書き換えること
// ★人感センサー単体の動作確認用に一時停止中。再開する場合はtrueに戻す。
// WebUI/WLED連携にはWiFi接続が必要なため、この値に関わらずWiFiへは常時接続する
// (下記setup()参照。ENABLE_MQTTはMQTTブローカーへの接続要否のみを制御する)。
const bool ENABLE_MQTT = false;

static const char *WIFI_SSID = "4017-5Gs";
static const char *WIFI_PASSWORD = "77777777";
static const char *MQTT_HOST = "192.168.11.27";
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USER = "";     // 認証不要なブローカーの場合は空文字のまま
static const char *MQTT_PASSWORD = ""; // 認証不要なブローカーの場合は空文字のまま
static const char *MQTT_CLIENT_ID = "auto-toilet";

static const char *MQTT_AVAILABILITY_TOPIC = "auto-toilet/status";
static const char *MQTT_PRESENCE_STATE_TOPIC = "auto-toilet/presence/state";
static const char *MQTT_DISCOVERY_TOPIC = "homeassistant/binary_sensor/auto_toilet_presence/config";

static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;    // 起動時のWiFi接続待ち上限
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;  // WiFi再接続の試行間隔
static const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;  // MQTTブローカー再接続の試行間隔

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

AudioPlayerUnit audioPlayer;
bool audioAvailable = false; // Audio Playerモジュールが接続されているか(未接続でも起動を継続する)
static const uint8_t AUDIO_INIT_RETRY_COUNT = 5;
static const unsigned long AUDIO_INIT_RETRY_DELAY_MS = 500;

Preferences prefs;

// ---- 設定パラメータ(NVSに保存、再起動後も読込) ----
// WebUIバックエンドAPI(GET/PUT /api/settings, POST /api/settings/save)経由で調整する。
static const char *NVS_NAMESPACE = "toilet";
static const char *NVS_KEY_SENSITIVITY = "sensitivity";
static const char *NVS_KEY_MAX_GATE = "max_gate";
static const char *NVS_KEY_STAY_SEC = "stay_sec";

static const uint8_t DEFAULT_SENSITIVITY = 20;      // 感度 初期値
static const uint8_t DEFAULT_MAX_GATE = 1;          // ゲート 初期値(1ゲート=0.75m)
static const uint16_t DEFAULT_STAY_DURATION_SEC = 20; // 滞在継続時間 初期値

uint8_t sensitivity = DEFAULT_SENSITIVITY;
uint8_t maxGate = DEFAULT_MAX_GATE;
uint16_t stayDurationSec = DEFAULT_STAY_DURATION_SEC;

// loadParams()/saveParams()でのみ更新する「NVS保存済み」スナップショット。
uint8_t savedSensitivity = DEFAULT_SENSITIVITY;
uint8_t savedMaxGate = DEFAULT_MAX_GATE;
uint16_t savedStayDurationSec = DEFAULT_STAY_DURATION_SEC;

// ---- 在室状態 ----
RoomState roomState = RoomState::VACANT;
unsigned long enteringSinceMs = 0;

bool flushNeeded = false; // 滞在処理で立てる「トイレ流し必要」フラグ(退室処理で消費)

// ---- 関数宣言 ----
void loadParams();
void applySensitivity();
void applyMaxGate();
void updatePresenceDebounce();
void connectWiFi();
void maintainWiFi();
void maintainMqtt();
void publishDiscoveryConfig();
void publishPresenceState(bool presence);
void setStatusLed(uint32_t color, uint8_t brightness);
void updateOccupancy();
void printStatus();

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    loadParams();
    toiletIrBegin();

    radarSerial.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
    Serial.println("[RADAR] LD2410 初期化中...");
    if (!radar.begin(radarSerial, true)) {
        Serial.println("[ERROR] LD2410 の初期化に失敗しました");
    } else {
        Serial.println("[RADAR] LD2410 初期化OK");
        applySensitivity();
        applyMaxGate();
    }

    Serial.println("[AUDIO] Audio Playerモジュールに接続中...");
    for (uint8_t i = 0; i < AUDIO_INIT_RETRY_COUNT && !audioAvailable; i++) {
        audioAvailable = audioPlayer.begin(&Serial1, AUDIO_RX_PIN, AUDIO_TX_PIN);
        if (!audioAvailable) {
            Serial.println("[AUDIO] Audio Playerモジュールが見つかりません。再試行します...");
            delay(AUDIO_INIT_RETRY_DELAY_MS);
        }
    }
    if (audioAvailable) {
        Serial.println("[AUDIO] Audio Playerモジュールの準備完了");
        // デバッグ: SDカード上でモジュールが認識しているファイル数を確認する
        // (playAudioByNameが応答しない問題が、ファイル名不一致なのか初期化タイミングなのか切り分けるため)
        uint16_t totalAudio = audioPlayer.getTotalAudioNumber();
        uint16_t pathFileCount = audioPlayer.getCurrentPathFileCount();
        Serial.printf("[AUDIO][DEBUG] getTotalAudioNumber=%u getCurrentPathFileCount=%u\n", totalAudio, pathFileCount);
    } else {
        Serial.println("[WARN] Audio Playerモジュールが未接続のため、音声再生なしで起動します");
    }
    bgmControlBegin(); // 常に先頭曲をロードし無音(volume=0)で待機状態にする

    irSender.begin();

    // WebUI・WLED連携にはWiFi接続が必須のため、ENABLE_MQTTの値に関わらず接続する。
    connectWiFi();
    if (ENABLE_MQTT) {
        mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    } else {
        Serial.println("[MQTT] 一時停止中(ENABLE_MQTT=false)");
    }

    ledControlBegin();
    scenesBegin();
    webApiBegin();

    setStatusLed(TFT_GREEN, LED_BRIGHTNESS_OFF); // 起動時は退室状態として消灯
}

void loop() {
    M5.update();
    maintainWiFi();
    if (ENABLE_MQTT) maintainMqtt();
    ledTick();
    bgmTick();

    if (M5.BtnA.wasPressed()) {
        // 本体ボタン(G0)押下でトイレ流しを手動送信
        sendToiletFlush();
    }

    // 新しいフレームを受信した時だけデバウンスを更新する。
    // 毎ループ呼ぶと、loopの回転がセンサーのフレーム間隔より速いために
    // カウンタが実際のセンサー更新回数を無視して一瞬で振り切れてしまう。
    if (radar.read()) {
        updatePresenceDebounce();
    }
    updateOccupancy();
    printStatus();
    delay(20);
}

void updatePresenceDebounce() {
    // 上下カウンタ方式のヒステリシス(ON/OFF非対称): 生値が連続して
    // 同方向に振れて初めてsmoothedPresenceが切り替わる(チャタリング除去)
    bool rawPresence = radar.presenceDetected();
    if (rawPresence) {
        if (presenceDebounceCounter < PRESENCE_DEBOUNCE_OFF_COUNT) presenceDebounceCounter++;
    } else {
        if (presenceDebounceCounter > 0) presenceDebounceCounter--;
    }
    bool previousPresence = smoothedPresence;
    if (presenceDebounceCounter >= PRESENCE_DEBOUNCE_ON_COUNT) smoothedPresence = true;
    if (presenceDebounceCounter == 0) smoothedPresence = false;
    if (smoothedPresence != previousPresence) {
        publishPresenceState(smoothedPresence);
    }
}

void updateOccupancy() {
    bool presence = smoothedPresence;
    unsigned long now = millis();

    switch (roomState) {
        case RoomState::VACANT:
            if (presence) {
                // トイレ入室検知 → トイレ入室処理
                roomState = RoomState::ENTERING;
                enteringSinceMs = now;
                Serial.println("[STATE] 入室検知");
                setStatusLed(TFT_RED, LED_BRIGHTNESS_ACTIVE);
                bgmSetPlayingFromOccupancy(true);
            }
            break;

        case RoomState::ENTERING:
            if (!presence) {
                // トイレ退室検知 → トイレ退室処理(滞在前のため流さない)
                roomState = RoomState::VACANT;
                Serial.println("[STATE] 退室検知(滞在前)");
                setStatusLed(TFT_GREEN, LED_BRIGHTNESS_OFF);
                bgmSetPlayingFromOccupancy(false);
            } else if (now - enteringSinceMs >= (unsigned long)stayDurationSec * 1000UL) {
                // トイレ滞在開始検知 → トイレ滞在処理
                roomState = RoomState::STAYING;
                Serial.println("[STATE] 滞在開始検知");
                setStatusLed(TFT_YELLOW, LED_BRIGHTNESS_ACTIVE);
                flushNeeded = true;
            }
            break;

        case RoomState::STAYING:
            if (!presence) {
                // トイレ退室検知 → トイレ退室処理
                roomState = RoomState::VACANT;
                Serial.println("[STATE] 退室検知");
                setStatusLed(TFT_GREEN, LED_BRIGHTNESS_OFF);
                bgmSetPlayingFromOccupancy(false);
                if (flushNeeded) {
                    sendToiletFlush();
                }
                flushNeeded = false;
            }
            break;
    }
}

void setStatusLed(uint32_t color, uint8_t brightness) {
    M5.Led.setAllColor(color);
    M5.Led.setBrightness(brightness);
}

void sendToiletFlush() {
    // 本体ボタン用の単発「流す」送信。WebUI経由のflush_largeコマンドと同じ
    // 実行経路(IR送信+クールダウン)を共有する。
    JsonDocument doc;
    JsonObject state = doc.to<JsonObject>();
    toiletIrExecute("flush_large", state);
}

void loadParams() {
    prefs.begin(NVS_NAMESPACE, true);
    bool hasAll = prefs.isKey(NVS_KEY_SENSITIVITY) && prefs.isKey(NVS_KEY_MAX_GATE) && prefs.isKey(NVS_KEY_STAY_SEC);
    if (hasAll) {
        sensitivity = prefs.getUChar(NVS_KEY_SENSITIVITY);
        maxGate = prefs.getUChar(NVS_KEY_MAX_GATE);
        stayDurationSec = prefs.getUShort(NVS_KEY_STAY_SEC);
    }
    prefs.end();
    // 起動直後は「ライブ値」=「NVS保存済み値」(NVSが空の場合はデフォルト値)なので、
    // dirty判定の基準となるスナップショットもここで同期しておく。
    savedSensitivity = sensitivity;
    savedMaxGate = maxGate;
    savedStayDurationSec = stayDurationSec;

    if (hasAll) {
        Serial.printf("[NVS] 設定値を読込: 感度=%d ゲート=%d 滞在継続時間=%d秒\n", sensitivity, maxGate, stayDurationSec);
    } else {
        Serial.println("[NVS] 設定値が未保存のため初期値を使用します");
    }
}

void saveParams() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar(NVS_KEY_SENSITIVITY, sensitivity);
    prefs.putUChar(NVS_KEY_MAX_GATE, maxGate);
    prefs.putUShort(NVS_KEY_STAY_SEC, stayDurationSec);
    prefs.end();
    savedSensitivity = sensitivity;
    savedMaxGate = maxGate;
    savedStayDurationSec = stayDurationSec;
    Serial.printf("[NVS] 設定値を保存: 感度=%d ゲート=%d 滞在継続時間=%d秒\n", sensitivity, maxGate, stayDurationSec);
}

void applySensitivity() {
    for (uint8_t i = 0; i <= 8; i++) {
        radar.setGateSensitivityThreshold(i, sensitivity, sensitivity);
    }
    Serial.printf("[RADAR] 感度: %d\n", sensitivity);
}

void applyMaxGate() {
    radar.setMaxValues(maxGate, maxGate, SENSOR_NO_ONE_DURATION_SEC);
    Serial.printf("[RADAR] 最大検知距離: ゲート%d (%.2fm)\n", maxGate, maxGate * 0.75f);
}

void connectWiFi() {
    Serial.printf("[WIFI] 接続中: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] 接続完了: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WIFI] 接続タイムアウト。バックグラウンドで再試行します");
    }
}

// WiFiの接続維持を行う(WebUI/WLED連携・MQTT共通)。ブロッキングを避けるため、
// 切断中はWIFI_RECONNECT_INTERVAL_MSごとに再接続を試みるだけで即座に戻る。
void maintainWiFi() {
    static unsigned long lastReconnectAttemptMs = 0;
    if (WiFi.status() == WL_CONNECTED) return;
    unsigned long now = millis();
    if (now - lastReconnectAttemptMs < WIFI_RECONNECT_INTERVAL_MS) return;
    lastReconnectAttemptMs = now;
    Serial.println("[WIFI] 再接続を試みます");
    WiFi.reconnect();
}

// MQTTブローカーへの接続維持を行う(ENABLE_MQTT=trueの場合のみ呼ばれる)。
void maintainMqtt() {
    if (WiFi.status() != WL_CONNECTED) return;

    static unsigned long lastReconnectAttemptMs = 0;
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttemptMs < MQTT_RECONNECT_INTERVAL_MS) return;
        lastReconnectAttemptMs = now;

        Serial.println("[MQTT] ブローカーに接続中...");
        bool connected = (strlen(MQTT_USER) > 0)
            ? mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                                  MQTT_AVAILABILITY_TOPIC, 0, true, "offline")
            : mqttClient.connect(MQTT_CLIENT_ID,
                                  MQTT_AVAILABILITY_TOPIC, 0, true, "offline");

        if (connected) {
            Serial.println("[MQTT] 接続完了");
            mqttClient.publish(MQTT_AVAILABILITY_TOPIC, "online", true);
            publishDiscoveryConfig();
            publishPresenceState(smoothedPresence); // 現在の状態を再同期
        } else {
            Serial.printf("[MQTT] 接続失敗 rc=%d\n", mqttClient.state());
        }
        return;
    }

    mqttClient.loop();
}

// Home Assistant MQTT Discovery用のconfigペイロードを送信する(retained)。
// これによりブローカー接続のたびに binary_sensor.auto_toilet_presence が自動登録される。
void publishDiscoveryConfig() {
    char payload[640];
    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"presence\","
        "\"unique_id\":\"auto_toilet_presence\","
        "\"device_class\":\"occupancy\","
        "\"state_topic\":\"%s\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device\":{\"identifiers\":[\"auto_toilet_m5stack\"],\"name\":\"Auto Toilet\",\"manufacturer\":\"M5Stack + DIY\",\"model\":\"M5StampS3 + LD2410\"}"
        "}",
        MQTT_PRESENCE_STATE_TOPIC, MQTT_AVAILABILITY_TOPIC);
    mqttClient.publish(MQTT_DISCOVERY_TOPIC, payload, true);
}

void publishPresenceState(bool presence) {
    if (!mqttClient.connected()) return;
    mqttClient.publish(MQTT_PRESENCE_STATE_TOPIC, presence ? "ON" : "OFF", true);
}

void printStatus() {
    static unsigned long lastPrintMs = 0;
    unsigned long now = millis();
    if (now - lastPrintMs < 1000) return;
    lastPrintMs = now;

    const char *stateName = roomState == RoomState::VACANT
                                 ? "VACANT"
                                 : (roomState == RoomState::ENTERING ? "ENTERING" : "STAYING");

    Serial.printf("[STATUS] presence(raw)=%s presence(smoothed)=%s state=%s sensitivity=%d gate=%d stayDuration=%ds audio=%s wifi=%s mqtt=%s\n",
                  radar.presenceDetected() ? "YES" : "NO", smoothedPresence ? "YES" : "NO", stateName, sensitivity,
                  maxGate, stayDurationSec, bgmIsPlaying() ? "ON" : "OFF",
                  WiFi.status() == WL_CONNECTED ? "OK" : "NG", mqttClient.connected() ? "OK" : "NG");
}
