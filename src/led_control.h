#pragma once
// ============================================================
// LED(WLED経由)制御。WebUIバックエンド仕様.md「LED（WLED経由）」対応。
// M5StampS3がWLEDのローカルJSON API(GET/POST /json/state)を中継する。
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>

enum class LedResult { Ok, Unreachable, InvalidParameter };

void ledControlBegin();

// loop()から定期的に呼ぶ。数秒に1回だけWLEDへ問い合わせてキャッシュを更新する
// (GET /api/status用。ポーリング頻度分の通信負荷を避けるため、通信はここでのみ行う)。
void ledTick();

// GET /api/led: その場でWLEDへ問い合わせて返す。
LedResult ledGetState(JsonObject out);

// GET /api/status用: ledTick()が保持しているキャッシュのみを参照する(通信しない)。
LedResult ledGetCachedState(JsonObject out);

// PUT /api/led: 部分更新(brightness/color/presetId)をWLEDへ転送する。
LedResult ledSetState(JsonObjectConst patch, JsonObject out);

// GET /api/statusの"wled":{"reachable":...}用。
bool ledIsReachable();
