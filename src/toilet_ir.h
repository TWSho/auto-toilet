#pragma once
// ============================================================
// トイレ(ウォシュレット)IRコマンドテーブルと追跡状態。
// WebUIバックエンド仕様.md「トイレ（IRコマンドテーブル）」に対応。
// IRコードを収集次第、kIrCommands配列にrawData/rawLenを追加するだけで
// 有効化できる(コード変更なし)構成。
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>

struct IrCommand {
    const char *id;
    const char *kind;   // "momentary" | "select" | "toggle" | "step" | "cycle"
    const uint16_t *rawData; // 未収録の場合はnullptr
    uint16_t rawLen;
};

struct ToiletTrackedState {
    char spray[8]; // "off" | "rear" | "soft" | "bidet"
    uint8_t waterPressure;  // 1-5
    uint8_t nozzlePosition; // 1-5
    uint8_t seatTemp;       // 1-3 (低/中/高)
    uint8_t waterTemp;      // 1-3 (低/中/高)
    bool deodorizer;
    bool autoClean;
};

enum class ToiletExecuteResult { Ok, NotFound, NotImplemented, Cooldown };

void toiletIrBegin();

// コマンド一覧をJSON配列(GET /api/toilet/commands用)へ書き出す
void toiletIrWriteCommandList(JsonArray commands);

// 現在の追跡状態をJSONオブジェクトへ書き出す(GET /api/toilet/state用)
void toiletIrWriteState(JsonObject out);

// コマンドを実行する。成功時、変更のあったフィールドのみstateOutへ書き出す。
ToiletExecuteResult toiletIrExecute(const String &id, JsonObject stateOut);
