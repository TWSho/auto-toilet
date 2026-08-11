#pragma once
// ============================================================
// シーン(LED+BGM一括変更)。WebUIバックエンド仕様.md「シーン」対応。
// 初期5件はファームウェア組み込み、ユーザー追加分はLittleFS(/scenes.json)へ
// 追記保存する(編集・削除は仕様上未対応のため追加のみ)。
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>

enum class SceneCreateResult { Ok, InvalidParameter, StorageError };

void scenesBegin();

// GET /api/scenes
void scenesWriteList(JsonArray scenes);

// POST /api/scenes/{id}/execute
// LED反映(WLED転送)とBGM選局・再生開始を順に実行する。LEDが失敗しても
// BGM側は実行したまま返す(部分成功を許容)。idが存在しない場合はfalse。
bool sceneExecute(const String &id, JsonObject out);

// POST /api/scenes (新規作成・永続化)
SceneCreateResult sceneCreate(JsonObjectConst body, JsonObject out);
