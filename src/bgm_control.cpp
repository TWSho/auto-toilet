#include "bgm_control.h"
#include "bgm_tracks.h"
#include "state.h"
#include <string.h>

static const uint8_t AUDIO_FADE_STEPS = 10;           // フェードの分割数
static const unsigned long AUDIO_FADE_STEP_MS = 200;  // フェード1段あたりの待機時間(合計2000ms)
static const uint8_t DEFAULT_VOLUME_PERCENT = 67;      // 既定音量(0-100)。旧AUDIO_VOLUME=20/30相当

// ==== デバッグ用一時措置(現在は無効化済み) ====
// 起動時の音声再生疎通確認用に、番号指定で強制再生するデバッグ経路。
// ハードウェア側の問題ではないことを確認済みのためfalseにしてある。
static const bool USE_DEBUG_BOOT_TRACK = false;
static const uint16_t DEBUG_BOOT_TRACK_INDEX = 23;

static uint8_t s_trackIndex = 0;
static bool s_playing = false;
static uint8_t s_volume = DEFAULT_VOLUME_PERCENT; // 0-100(Web向け)
static char s_repeat[4] = "off";                   // "off" | "one" | "all"
static bool s_shuffle = false;
static unsigned long s_trackStartMs = 0;
static unsigned long s_pausedElapsedMs = 0;
static uint16_t s_durationSec = 0;

// ---- 音量フェード(モジュール側0-30スケール)。loop()から非ブロッキングで進める ----
static uint8_t s_currentModuleVol = 0;
static uint8_t s_fadeFromModuleVol = 0;
static uint8_t s_fadeToModuleVol = 0;
static uint8_t s_fadeStepIndex = 0;
static unsigned long s_fadeLastStepMs = 0;
static bool s_fading = false;

static uint8_t webVolumeToModule(uint8_t webVol) {
    return (uint8_t)(((uint16_t)webVol * 30 + 50) / 100); // 0-100 -> 0-30(四捨五入)
}

static void setModuleVolume(uint8_t v) {
    s_currentModuleVol = v;
    if (audioAvailable) audioPlayer.setVolume(v);
}

static void startFade(uint8_t toVol) {
    s_fadeFromModuleVol = s_currentModuleVol;
    s_fadeToModuleVol = toVol;
    s_fadeStepIndex = 0;
    s_fadeLastStepMs = millis();
    s_fading = (s_fadeFromModuleVol != s_fadeToModuleVol) && audioAvailable;
    if (!s_fading) setModuleVolume(toVol);
}

void bgmTick() {
    if (!s_fading) return;
    unsigned long now = millis();
    if (now - s_fadeLastStepMs < AUDIO_FADE_STEP_MS) return;
    s_fadeLastStepMs = now;
    s_fadeStepIndex++;
    if (s_fadeStepIndex >= AUDIO_FADE_STEPS) {
        setModuleVolume(s_fadeToModuleVol);
        s_fading = false;
        return;
    }
    int16_t v = (int16_t)s_fadeFromModuleVol +
                ((int32_t)((int16_t)s_fadeToModuleVol - (int16_t)s_fadeFromModuleVol) * s_fadeStepIndex) /
                    AUDIO_FADE_STEPS;
    setModuleVolume((uint8_t)v);
}

static void applyPlayModeFromState() {
    if (!audioAvailable) return;
    play_mode_t mode;
    if (s_shuffle) {
        mode = AUDIO_PLAYER_MODE_RANDOM; // ディスク全体からランダム
    } else if (strcmp(s_repeat, "one") == 0) {
        mode = AUDIO_PLAYER_MODE_SINGLE_LOOP;
    } else {
        mode = AUDIO_PLAYER_MODE_ALL_LOOP; // "off" / "all"ともにリストを順番にループ
    }
    audioPlayer.setPlayMode(mode);
}

static uint32_t currentElapsedSec() {
    if (s_playing) return (uint32_t)((millis() - s_trackStartMs) / 1000);
    return (uint32_t)(s_pausedElapsedMs / 1000);
}

// AudioPlayerUnit::playAudioByName()(M5UnitAudioPlayerライブラリ)は内部で固定長
// message[32]バッファへ [0x04,~0x04,dataLen, 0x07,<ファイル名バイト列>, checksum] を
// 詰めており、境界チェックが無い。ファイル名がnameLen<=27文字を超えるとスタックの
// message配列を書き潰しstack smashing protect failureで再起動ループに陥るため、
// 送信前にここで長さを検証してライブラリ側のバグを踏まないようにする。
static const size_t AUDIO_PLAYER_MAX_FILENAME_LEN = 27;

// 指定したファイル名を実際にモジュールへロードする(曲テーブルを介さない下位関数)。
static void loadTrackByFileName(const char *fileName) {
    s_durationSec = 0;
    if (!audioAvailable) return;
    if (strlen(fileName) > AUDIO_PLAYER_MAX_FILENAME_LEN) {
        Serial.printf("[AUDIO] ファイル名が%d文字を超えるため再生をスキップ(モジュール側の制約): %s\n",
                      AUDIO_PLAYER_MAX_FILENAME_LEN, fileName);
        setModuleVolume(0);
        return;
    }
    audioPlayer.playAudioByName(fileName);
    delay(300); // モジュールがファイルを読み込むのを待つ(既存実装踏襲)
    setModuleVolume(s_playing ? webVolumeToModule(s_volume) : 0);
    uint8_t buf[3] = {0, 0, 0};
    audioPlayer.getTotalPlayTime(buf);
    s_durationSec = (uint16_t)buf[0] * 3600 + (uint16_t)buf[1] * 60 + buf[2];
}

// 曲を切り替える。再生中/一時停止中の状態(volume)は維持したまま曲だけ差し替える。
static void loadTrack(uint8_t index) { loadTrackByFileName(kBgmTracks[index].fileName); }

// デバッグ用: ファイル名ではなく通し番号(getTotalAudioNumber基準)で直接ロードする。
static void loadTrackByIndex(uint16_t audioIndex) {
    s_durationSec = 0;
    if (!audioAvailable) return;
    audioPlayer.playAudioByIndex(audioIndex);
    delay(300);
    setModuleVolume(s_playing ? webVolumeToModule(s_volume) : 0);
    uint8_t buf[3] = {0, 0, 0};
    audioPlayer.getTotalPlayTime(buf);
    s_durationSec = (uint16_t)buf[0] * 3600 + (uint16_t)buf[1] * 60 + buf[2];
}

static void doPlay() {
    if (s_playing) return;
    s_trackStartMs = millis() - s_pausedElapsedMs;
    s_playing = true;
    startFade(webVolumeToModule(s_volume));
}

static void doPause() {
    if (!s_playing) return;
    s_pausedElapsedMs = millis() - s_trackStartMs;
    s_playing = false;
    startFade(0);
}

bool bgmIsPlaying() { return s_playing; }

void bgmControlBegin() {
    s_trackIndex = 0;
    s_playing = false;
    s_volume = DEFAULT_VOLUME_PERCENT;
    strcpy(s_repeat, "off");
    s_shuffle = false;
    s_pausedElapsedMs = 0;
    s_currentModuleVol = 0;
    applyPlayModeFromState();
    if (USE_DEBUG_BOOT_TRACK) {
        // 音量0だと「読み込み失敗で無音」なのか「音量0で聞こえないだけ」なのか区別できないため、
        // デバッグ時は強制的に再生中・音量50(0-100)にしてから曲をロードする。
        s_playing = true;
        s_volume = 50;
        Serial.printf("[AUDIO][DEBUG] 起動疎通確認用に番号%u番を音量50で再生します\n", DEBUG_BOOT_TRACK_INDEX);
        loadTrackByIndex(DEBUG_BOOT_TRACK_INDEX);
    } else {
        loadTrack(s_trackIndex);
    }
    s_trackStartMs = millis();
}

void bgmWriteState(JsonObject out) {
    out["playing"] = s_playing;
    out["trackId"] = kBgmTracks[s_trackIndex].id;
    out["elapsedSec"] = currentElapsedSec();
    out["durationSec"] = s_durationSec;
    out["volume"] = s_volume;
    out["repeat"] = s_repeat;
    out["shuffle"] = s_shuffle;
}

bool bgmApplyPatch(JsonObjectConst patch, JsonObject out) {
    if (!patch["trackId"].isNull()) {
        String tid = patch["trackId"].as<String>();
        int idx = bgmTrackIndexById(tid);
        if (idx < 0) return false;
        s_trackIndex = (uint8_t)idx;
        s_playing = true; // 選局＋自動再生開始
        s_pausedElapsedMs = 0;
        loadTrack(s_trackIndex);
        s_trackStartMs = millis();
        setModuleVolume(webVolumeToModule(s_volume)); // フェードなしで即再生開始
    }

    if (!patch["action"].isNull()) {
        String action = patch["action"].as<String>();
        if (action == "play") doPlay();
        else if (action == "pause") doPause();
        else return false;
    }

    if (!patch["volume"].isNull()) {
        int v = patch["volume"].as<int>();
        if (v < 0 || v > 100) return false;
        s_volume = (uint8_t)v;
        if (s_playing) setModuleVolume(webVolumeToModule(s_volume));
    }

    if (!patch["seekSec"].isNull()) {
        int sec = patch["seekSec"].as<int>();
        if (sec < 0) return false;
        if (audioAvailable) audioPlayer.playCurrentAudioAtTime((uint8_t)(sec / 60), (uint8_t)(sec % 60));
        if (s_playing) s_trackStartMs = millis() - (unsigned long)sec * 1000UL;
        else s_pausedElapsedMs = (unsigned long)sec * 1000UL;
    }

    bool modeChanged = false;
    if (!patch["repeat"].isNull()) {
        String r = patch["repeat"].as<String>();
        if (r != "off" && r != "one" && r != "all") return false;
        strncpy(s_repeat, r.c_str(), sizeof(s_repeat) - 1);
        s_repeat[sizeof(s_repeat) - 1] = '\0';
        modeChanged = true;
    }
    if (!patch["shuffle"].isNull()) {
        s_shuffle = patch["shuffle"].as<bool>();
        modeChanged = true;
    }
    if (modeChanged) applyPlayModeFromState();

    bgmWriteState(out);
    return true;
}

void bgmNext(JsonObject out) {
    s_trackIndex = (s_trackIndex + 1) % kBgmTrackCount;
    s_pausedElapsedMs = 0;
    loadTrack(s_trackIndex);
    s_trackStartMs = millis();
    bgmWriteState(out);
}

void bgmPrevious(JsonObject out) {
    s_trackIndex = (uint8_t)((s_trackIndex + kBgmTrackCount - 1) % kBgmTrackCount);
    s_pausedElapsedMs = 0;
    loadTrack(s_trackIndex);
    s_trackStartMs = millis();
    bgmWriteState(out);
}

void bgmWriteTracks(JsonArray tracks) {
    for (size_t i = 0; i < kBgmTrackCount; i++) {
        JsonObject t = tracks.add<JsonObject>();
        t["id"] = kBgmTracks[i].id;
        t["title"] = kBgmTracks[i].title;
        t["fileName"] = kBgmTracks[i].fileName;
    }
}

void bgmSetPlayingFromOccupancy(bool playing) {
    if (playing) doPlay();
    else doPause();
}
