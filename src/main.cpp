#include <Arduino.h>
#include <M5Unified.h>
#include <ld2410.h>
#include <unit_audioplayer.hpp>
#include <Preferences.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
// ハードウェア接続
//   ミリ波センサー LD2410 : GROVEポート(PortA, UART)  RX=G21 TX=G22
//   Audio Player          : PortC(UART2)              RX=G16 TX=G17
//   IR REMOTE              : PortB                     TX=G26 (送信のみ、受信は使用しない)
//   本体スピーカーは使用しない(オフ)。GPIO25は本体スピーカーのDAC出力ピンのため、
//   オフにすることで未使用となり、G25をGNDに落としても問題ない。
// ============================================================
static const int RADAR_RX_PIN = 21;
static const int RADAR_TX_PIN = 22;
static const uint32_t RADAR_BAUD = 256000;
static const int AUDIO_RX_PIN = 16;
static const int AUDIO_TX_PIN = 17;
static const int IR_TX_PIN = 26;

static const char *AUDIO_FILE_NAME = "mori.mp3";
static const uint8_t AUDIO_VOLUME = 20;       // 通常再生時の音量(0-30)
static const uint8_t AUDIO_FADE_STEPS = 10;   // フェードの分割数
static const unsigned long AUDIO_FADE_STEP_MS = 200; // フェード1段あたりの待機時間(合計2000ms)

static const uint8_t LCD_BRIGHTNESS_ACTIVE = 179; // 入室・滞在中 約70%
static const uint8_t LCD_BRIGHTNESS_VACANT = 0;   // 退室中 0%

// ---- 赤外線送信(トイレ流し) ----
IRsend irSender(IR_TX_PIN);
static const uint16_t kIrRawData[] = {5930, 2964, 568, 578, 542, 1700, 542, 554, 566, 554, 566, 552, 566, 552, 540, 576, 542, 576, 542, 576,540, 550, 564, 550, 564, 1672, 568, 552, 542, 576, 542, 576, 542, 576, 540, 550, 566, 550, 564, 552, 566, 550, 538, 576, 540, 574, 540, 576,540, 1668, 566, 552, 566, 1672, 542, 1700, 544, 578, 542, 1676, 568, 1678, 544, 580, 542, 1702, 544, 580, 542, 1676, 568, 1678, 542, 580, 542, 1702, 544, 1678, 568, 554, 568, 32920, 5932, 2964, 570, 578, 542, 1674, 568, 554, 568, 552, 566, 552, 566, 550, 540, 576, 542, 578, 540, 576, 540, 550, 564, 552, 564, 1670, 540, 578, 542, 576, 542, 578, 540, 576, 540, 552, 564, 550, 566, 550, 564, 550, 538, 574, 540, 574, 542, 574, 540, 1670, 566, 552, 566, 1672, 542, 1702, 542, 580, 542, 1676, 568, 1678, 542, 580, 542, 1702, 544, 552, 568, 1676, 568, 1678, 542, 580,542, 1702, 544, 1678, 568, 554, 568};
static const uint16_t kIrRawLen = 163;
static const uint16_t IR_FREQUENCY_KHZ = 38;

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

static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;   // 起動時のWiFi接続待ち上限
static const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000; // WiFi/MQTT再接続の試行間隔

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

AudioPlayerUnit audioPlayer;
bool audioAvailable = false; // Audio Playerモジュールが接続されているか(未接続でも起動を継続する)
static const uint8_t AUDIO_INIT_RETRY_COUNT = 5;
static const unsigned long AUDIO_INIT_RETRY_DELAY_MS = 500;

Preferences prefs;

// ---- 設定パラメータ(NVSに保存、再起動後も読込) ----
static const char *NVS_NAMESPACE = "toilet";
static const char *NVS_KEY_SENSITIVITY = "sensitivity";
static const char *NVS_KEY_MAX_GATE = "max_gate";
static const char *NVS_KEY_STAY_SEC = "stay_sec";

static const uint8_t DEFAULT_SENSITIVITY = 20;      // 感度 初期値
static const uint8_t DEFAULT_MAX_GATE = 1;          // ゲート 初期値(1ゲート=0.75m)
static const uint16_t DEFAULT_STAY_DURATION_SEC = 20; // 滞在継続時間 初期値

static const uint8_t SENSITIVITY_MIN = 0, SENSITIVITY_MAX = 100, SENSITIVITY_STEP = 10;
static const uint8_t MAX_GATE_MIN = 1, MAX_GATE_MAX = 8, MAX_GATE_STEP = 1;
static const uint16_t STAY_DURATION_MIN = 5, STAY_DURATION_MAX = 60, STAY_DURATION_STEP = 5;

uint8_t sensitivity = DEFAULT_SENSITIVITY;
uint8_t maxGate = DEFAULT_MAX_GATE;
uint16_t stayDurationSec = DEFAULT_STAY_DURATION_SEC;

// ---- 在室状態 ----
enum class RoomState { VACANT, ENTERING, STAYING };
RoomState roomState = RoomState::VACANT;
unsigned long enteringSinceMs = 0;

bool audioActive = false; // フェードイン状態(常時再生中、これは音量表現上の状態)
bool flushNeeded = false; // 滞在処理で立てる「トイレ流し必要」フラグ(退室処理で消費)

// ---- 設定画面 ----
enum class SettingParam : uint8_t { SENSITIVITY = 0, MAX_GATE = 1, STAY_DURATION = 2, COUNT = 3 };
bool inSettingsMode = false;
SettingParam selectedParam = SettingParam::SENSITIVITY;

// ---- 関数宣言 ----
void loadParams();
void saveParams();
void applySensitivity();
void applyMaxGate();
void updatePresenceDebounce();
void connectWiFi();
void maintainMqtt();
void publishDiscoveryConfig();
void publishPresenceState(bool presence);
void fadeVolume(uint8_t fromVolume, uint8_t toVolume);
void audioFadeIn();
void audioFadeOut();
void sendToiletFlush();
void updateOccupancy();
void handleButtons();
void updateDisplay();
void drawNormalScreen();
void drawSettingsScreen();
void printStatus();

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = false; // 本体スピーカーは使用しない(ノイズ防止)
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Power.setExtOutput(true);

    loadParams();

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
        // 常に再生し続け、開始・停止は音量で表現する
        audioPlayer.setPlayMode(AUDIO_PLAYER_MODE_SINGLE_LOOP); // 最後まで再生したら最初から再生
        audioPlayer.setVolume(0);
        audioPlayer.playAudioByName(AUDIO_FILE_NAME);
        delay(300); // モジュールがファイルを読み込むのを待つ
        audioPlayer.setVolume(0);
        Serial.println("[AUDIO] Audio Playerモジュールの準備完了(常時再生・無音)");
    } else {
        Serial.println("[WARN] Audio Playerモジュールが未接続のため、音声再生なしで起動します");
    }

    irSender.begin();

    connectWiFi();
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);

    updateDisplay();
}

void loop() {
    M5.update();
    maintainMqtt();
    // 新しいフレームを受信した時だけデバウンスを更新する。
    // 毎ループ呼ぶと、loopの回転がセンサーのフレーム間隔より速いために
    // カウンタが実際のセンサー更新回数を無視して一瞬で振り切れてしまう。
    if (radar.read()) {
        updatePresenceDebounce();
    }
    handleButtons();
    updateOccupancy();
    updateDisplay();
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
                audioFadeIn();
            }
            break;

        case RoomState::ENTERING:
            if (!presence) {
                // トイレ退室検知 → トイレ退室処理(滞在前のため流さない)
                roomState = RoomState::VACANT;
                Serial.println("[STATE] 退室検知(滞在前)");
                audioFadeOut();
            } else if (now - enteringSinceMs >= (unsigned long)stayDurationSec * 1000UL) {
                // トイレ滞在開始検知 → トイレ滞在処理
                roomState = RoomState::STAYING;
                Serial.println("[STATE] 滞在開始検知");
                flushNeeded = true;
            }
            break;

        case RoomState::STAYING:
            if (!presence) {
                // トイレ退室検知 → トイレ退室処理
                roomState = RoomState::VACANT;
                Serial.println("[STATE] 退室検知");
                audioFadeOut();
                if (flushNeeded) {
                    sendToiletFlush();
                }
                flushNeeded = false;
            }
            break;
    }
}

void handleButtons() {
    if (inSettingsMode) {
        if (M5.BtnB.wasPressed()) {
            // 選択中のパラメータ項目を次の項目に切り替える(ループ)
            selectedParam = static_cast<SettingParam>(
                (static_cast<uint8_t>(selectedParam) + 1) % static_cast<uint8_t>(SettingParam::COUNT));
        }

        if (M5.BtnC.wasPressed()) {
            // 選択中のパラメータの値を1段階増加(上限を超えたら下限に戻る)。即座にセンサー・状態判定へ反映
            switch (selectedParam) {
                case SettingParam::SENSITIVITY:
                    sensitivity = (sensitivity >= SENSITIVITY_MAX) ? SENSITIVITY_MIN : sensitivity + SENSITIVITY_STEP;
                    applySensitivity();
                    break;
                case SettingParam::MAX_GATE:
                    maxGate = (maxGate >= MAX_GATE_MAX) ? MAX_GATE_MIN : maxGate + MAX_GATE_STEP;
                    applyMaxGate();
                    break;
                case SettingParam::STAY_DURATION:
                default:
                    stayDurationSec = (stayDurationSec >= STAY_DURATION_MAX) ? STAY_DURATION_MIN : stayDurationSec + STAY_DURATION_STEP;
                    break;
            }
        }

        if (M5.BtnA.wasPressed()) {
            // 選択中の変更内容を確定し、3項目まとめてNVSに保存したうえで通常画面に戻る
            saveParams();
            inSettingsMode = false;
        }
    } else {
        if (M5.BtnA.wasPressed()) {
            // 設定画面に入る(先頭の項目「感度」が選択された状態で表示される)
            inSettingsMode = true;
            selectedParam = SettingParam::SENSITIVITY;
        }

        if (M5.BtnB.wasPressed()) {
            // トイレ流し
            sendToiletFlush();
        }
        // ボタンCは通常画面では使用しない
    }
}

void fadeVolume(uint8_t fromVolume, uint8_t toVolume) {
    if (fromVolume == toVolume) {
        audioPlayer.setVolume(toVolume);
        return;
    }
    for (uint8_t i = 1; i <= AUDIO_FADE_STEPS; i++) {
        int16_t v = (int16_t)fromVolume + ((int32_t)((int16_t)toVolume - (int16_t)fromVolume) * i) / AUDIO_FADE_STEPS;
        audioPlayer.setVolume((uint8_t)v);
        delay(AUDIO_FADE_STEP_MS);
    }
}

void audioFadeIn() {
    if (!audioAvailable) return; // モジュール未接続時は何もしない
    audioActive = true;
    fadeVolume(0, AUDIO_VOLUME);
    Serial.println("[AUDIO] フェードイン(再生開始)");
}

void audioFadeOut() {
    if (!audioAvailable) return; // モジュール未接続時は何もしない
    uint8_t currentVolume = audioPlayer.getVolume();
    fadeVolume(currentVolume, 0);
    audioActive = false;
    Serial.println("[AUDIO] フェードアウト(再生停止)");
}

void sendToiletFlush() {
    irSender.sendRaw(kIrRawData, kIrRawLen, IR_FREQUENCY_KHZ);
    Serial.println("[IR] トイレ流し送信");
}

void updateDisplay() {
    static bool firstDraw = true;
    static bool lastInSettingsMode = false;
    static RoomState lastRoomState = RoomState::VACANT;
    static SettingParam lastParam = SettingParam::SENSITIVITY;
    static uint8_t lastSensitivity = 0;
    static uint8_t lastMaxGate = 0;
    static uint16_t lastStayDuration = 0;

    if (inSettingsMode) {
        bool changed = firstDraw || !lastInSettingsMode || lastParam != selectedParam ||
                       lastSensitivity != sensitivity || lastMaxGate != maxGate ||
                       lastStayDuration != stayDurationSec;
        if (changed) {
            drawSettingsScreen();
            lastParam = selectedParam;
            lastSensitivity = sensitivity;
            lastMaxGate = maxGate;
            lastStayDuration = stayDurationSec;
        }
    } else {
        bool changed = firstDraw || lastInSettingsMode || lastRoomState != roomState;
        if (changed) {
            drawNormalScreen();
            lastRoomState = roomState;
        }
    }

    lastInSettingsMode = inSettingsMode;
    firstDraw = false;
}

void drawNormalScreen() {
    uint32_t color;
    uint8_t brightness;
    const char *label;
    switch (roomState) {
        case RoomState::ENTERING:
            color = TFT_RED;
            brightness = LCD_BRIGHTNESS_ACTIVE;
            label = "ENTERING";
            break;
        case RoomState::STAYING:
            color = TFT_YELLOW;
            brightness = LCD_BRIGHTNESS_ACTIVE;
            label = "STAYING";
            break;
        default:
            color = TFT_GREEN;
            brightness = LCD_BRIGHTNESS_VACANT;
            label = "VACANT";
            break;
    }

    M5.Display.setBrightness(brightness);
    M5.Display.fillScreen(color);
    M5.Display.setTextColor(TFT_BLACK, color);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.print(label);
}

void drawSettingsScreen() {
    const char *name;
    char valueText[24];
    char rangeText[24];
    switch (selectedParam) {
        case SettingParam::SENSITIVITY:
            name = "感度";
            snprintf(valueText, sizeof(valueText), "%d", sensitivity);
            snprintf(rangeText, sizeof(rangeText), "0-100");
            break;
        case SettingParam::MAX_GATE:
            name = "ゲート";
            snprintf(valueText, sizeof(valueText), "%d", maxGate);
            snprintf(rangeText, sizeof(rangeText), "1-8");
            break;
        case SettingParam::STAY_DURATION:
        default:
            name = "滞在継続時間";
            snprintf(valueText, sizeof(valueText), "%d秒", stayDurationSec);
            snprintf(rangeText, sizeof(rangeText), "5-60秒");
            break;
    }

    M5.Display.setBrightness(LCD_BRIGHTNESS_ACTIVE);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.print(name);

    M5.Display.setTextSize(3);
    M5.Display.setCursor(10, 45);
    M5.Display.print(valueText);

    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 90);
    M5.Display.printf("range: %s", rangeText);

    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 220);
    M5.Display.print("B:項目切替 C:値調整 A:決定");
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

// WiFi/MQTTの接続維持を行う。ブロッキングを避けるため、切断中は
// MQTT_RECONNECT_INTERVAL_MSごとに再接続を試みるだけで即座に戻る。
void maintainMqtt() {
    static unsigned long lastReconnectAttemptMs = 0;

    if (WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - lastReconnectAttemptMs >= MQTT_RECONNECT_INTERVAL_MS) {
            lastReconnectAttemptMs = now;
            Serial.println("[WIFI] 再接続を試みます");
            WiFi.reconnect();
        }
        return;
    }

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
        "\"device\":{\"identifiers\":[\"auto_toilet_m5stack\"],\"name\":\"Auto Toilet\",\"manufacturer\":\"M5Stack + DIY\",\"model\":\"M5Stack Grey + LD2410\"}"
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

    Serial.printf("[STATUS] presence(raw)=%s presence(smoothed)=%s state=%s sensitivity=%d gate=%d stayDuration=%ds audio=%s settings=%s wifi=%s mqtt=%s\n",
                  radar.presenceDetected() ? "YES" : "NO", smoothedPresence ? "YES" : "NO", stateName, sensitivity,
                  maxGate, stayDurationSec, audioActive ? "ON" : "OFF", inSettingsMode ? "ON" : "OFF",
                  WiFi.status() == WL_CONNECTED ? "OK" : "NG", mqttClient.connected() ? "OK" : "NG");
}
