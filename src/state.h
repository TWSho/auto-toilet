#pragma once
// ============================================================
// main.cpp とWebUIバックエンド各モジュール(toilet_ir/bgm_control/led_control/
// scenes/web_api)が共有するグローバル状態・定数の宣言。
// 実体(定義)はmain.cppに置く。
// ============================================================
#include <Arduino.h>
#include <ld2410.h>
#include <unit_audioplayer.hpp>
#include <IRsend.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>

// ---- 在室状態(人感センサー状態機械) ----
enum class RoomState { VACANT, ENTERING, STAYING };
extern RoomState roomState;
extern bool smoothedPresence;

// ---- 人感センサー設定値(NVSに保存、WebUI設定APIから調整) ----
static const uint8_t SENSITIVITY_MIN = 0, SENSITIVITY_MAX = 100, SENSITIVITY_STEP = 10;
static const uint8_t MAX_GATE_MIN = 1, MAX_GATE_MAX = 8, MAX_GATE_STEP = 1;
static const uint16_t STAY_DURATION_MIN = 5, STAY_DURATION_MAX = 60, STAY_DURATION_STEP = 5;

extern uint8_t sensitivity;
extern uint8_t maxGate;
extern uint16_t stayDurationSec;

// NVSへ最後に保存された値のスナップショット(GET/PUT /api/settingsの
// dirty判定用)。sensitivity等のライブ値とは別に保持し、loadParams()での
// 初期化時とsaveParams()成功時にのみ同期する。
extern uint8_t savedSensitivity;
extern uint8_t savedMaxGate;
extern uint16_t savedStayDurationSec;

void applySensitivity();
void applyMaxGate();
void saveParams();

// ---- ハードウェア/通信ハンドル ----
extern ld2410 radar;
extern AudioPlayerUnit audioPlayer;
extern bool audioAvailable;
extern IRsend irSender;
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;
extern Preferences prefs;
extern const bool ENABLE_MQTT;
extern const uint16_t IR_FREQUENCY_KHZ;

void sendToiletFlush(); // 本体ボタン用の単発「流す」送信(= flush_largeコマンド相当)
