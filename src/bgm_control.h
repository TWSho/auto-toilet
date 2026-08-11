#pragma once
// ============================================================
// BGM(Audio Playerモジュール)制御。WebUIバックエンド仕様.md「BGM」対応。
//
// 仕様.md記載の「常に再生し続け、開始・停止は音量で表現する」という既存設計を
// そのまま踏襲し、在室検知による自動フェード(旧audioFadeIn/audioFadeOut)と
// WebUIのBGM操作(play/pause等)は同じ再生状態を共有する
// (在室検知の自動再生も、WebUI上は「BGMが再生中」として見える)。
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>

// 音声モジュール初期化(begin/audioAvailable確定)後に呼ぶ。
// デフォルト曲(先頭曲)をロードし、無音(volume=0)の待機状態にする。
void bgmControlBegin();

// フェード処理を進める。loop()から毎回呼ぶ(ブロッキングしない)。
void bgmTick();

bool bgmIsPlaying();

// GET/PUT /api/bgm, POST /api/bgm/next, /previous 用の状態JSON
void bgmWriteState(JsonObject out);

// PUT /api/bgm: 一部フィールドのみ指定可(action/volume/trackId/seekSec/repeat/shuffle)。
// 不正な値が含まれる場合はfalseを返す(呼び出し側で400を返す)。
bool bgmApplyPatch(JsonObjectConst patch, JsonObject out);

void bgmNext(JsonObject out);
void bgmPrevious(JsonObject out);

// GET /api/bgm/tracks 用の全曲一覧
void bgmWriteTracks(JsonArray tracks);

// 在室状態(ENTERING/STAYING⇔VACANT)の遷移から呼ぶ。WebUIのplay/pauseと同じ経路。
void bgmSetPlayingFromOccupancy(bool playing);
