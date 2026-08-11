#include "led_control.h"
#include "state.h"
#include <HTTPClient.h>
#include <string.h>
#include <stdlib.h>

// ★ WLEDのIPアドレス。固定IP運用を推奨(仕様.md「未確定・要検討事項」参照)。
//   環境に合わせて書き換えること。
static const char *WLED_HOST = "192.168.11.11";
static const uint16_t WLED_TIMEOUT_MS = 1500;
static const unsigned long LED_CACHE_TTL_MS = 3000;

// プリセット⇔RGB対応表(仮の値。実際に使う色は仕様.md「未確定・要検討事項」の通り
// フロント側デザインと合わせて決定し、ここを書き換える)。
struct LedPreset {
    const char *id;
    uint8_t r, g, b;
};
static const LedPreset kLedPresets[] = {
    {"warm", 255, 207, 138},    // 電球色
    {"neutral", 245, 245, 240}, // 昼白色
    {"night", 255, 138, 138},   // 常夜灯
    {"pink", 255, 138, 200},    // ピンク
};
static const size_t kLedPresetCount = sizeof(kLedPresets) / sizeof(kLedPresets[0]);

static bool s_reachable = false;
static uint8_t s_cachedBrightness = 0; // 0-100
static char s_cachedColor[8] = "#000000";
static char s_presetId[16] = ""; // 空文字列 = null(直近選択プリセットなし/カスタム色)
static unsigned long s_lastFetchMs = 0;

static bool isValidHexColor(const String &s) {
    if (s.length() != 7 || s[0] != '#') return false;
    for (int i = 1; i < 7; i++) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

static void hexToRgb(const char *hex, uint8_t &r, uint8_t &g, uint8_t &b) {
    const char *p = (hex[0] == '#') ? hex + 1 : hex;
    long v = strtol(p, nullptr, 16);
    r = (uint8_t)((v >> 16) & 0xFF);
    g = (uint8_t)((v >> 8) & 0xFF);
    b = (uint8_t)(v & 0xFF);
}

static void writeCachedState(JsonObject out) {
    out["on"] = s_cachedBrightness > 0;
    out["brightness"] = s_cachedBrightness;
    out["color"] = s_cachedColor;
    if (strlen(s_presetId) > 0) out["presetId"] = s_presetId;
    else out["presetId"] = nullptr;
}

// WLEDの GET /json/state を実行し、成功したらキャッシュを更新する。
static bool fetchFromWled() {
    if (WiFi.status() != WL_CONNECTED) {
        s_reachable = false;
        return false;
    }
    HTTPClient http;
    http.setConnectTimeout(WLED_TIMEOUT_MS);
    http.setTimeout(WLED_TIMEOUT_MS);
    String url = String("http://") + WLED_HOST + "/json/state";
    if (!http.begin(url)) {
        s_reachable = false;
        return false;
    }
    int code = http.GET();
    if (code != 200) {
        http.end();
        s_reachable = false;
        return false;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        s_reachable = false;
        return false;
    }

    uint8_t bri255 = doc["bri"] | 0;
    uint8_t r = 0, g = 0, b = 0;
    JsonArray seg = doc["seg"].as<JsonArray>();
    if (seg.size() > 0) {
        JsonArray col = seg[0]["col"].as<JsonArray>();
        if (col.size() > 0) {
            JsonArray rgb = col[0].as<JsonArray>();
            if (rgb.size() >= 3) {
                r = rgb[0].as<uint8_t>();
                g = rgb[1].as<uint8_t>();
                b = rgb[2].as<uint8_t>();
            }
        }
    }
    s_cachedBrightness = (uint8_t)(((uint16_t)bri255 * 100 + 127) / 255);
    snprintf(s_cachedColor, sizeof(s_cachedColor), "#%02x%02x%02x", r, g, b);
    s_reachable = true;
    s_lastFetchMs = millis();
    return true;
}

// WLEDへ現在のキャッシュ内容(brightness/color)を POST /json/state する。
static bool pushToWled(uint8_t brightness, const char *color) {
    if (WiFi.status() != WL_CONNECTED) {
        s_reachable = false;
        return false;
    }
    uint8_t r, g, b;
    hexToRgb(color, r, g, b);
    bool on = brightness > 0;
    uint8_t bri255 = (uint8_t)(((uint16_t)brightness * 255 + 50) / 100);

    JsonDocument reqDoc;
    reqDoc["on"] = on;
    reqDoc["bri"] = bri255;
    JsonArray seg = reqDoc["seg"].to<JsonArray>();
    JsonObject seg0 = seg.add<JsonObject>();
    JsonArray col = seg0["col"].to<JsonArray>();
    JsonArray col0 = col.add<JsonArray>();
    col0.add(r);
    col0.add(g);
    col0.add(b);
    String body;
    serializeJson(reqDoc, body);

    HTTPClient http;
    http.setConnectTimeout(WLED_TIMEOUT_MS);
    http.setTimeout(WLED_TIMEOUT_MS);
    String url = String("http://") + WLED_HOST + "/json/state";
    if (!http.begin(url)) {
        s_reachable = false;
        return false;
    }
    http.addHeader("Content-Type", "application/json; charset=utf-8");
    int code = http.POST(body);
    http.end();
    if (code != 200) {
        s_reachable = false;
        return false;
    }
    s_reachable = true;
    s_lastFetchMs = millis();
    return true;
}

void ledControlBegin() {
    s_reachable = false;
    s_lastFetchMs = 0;
    s_presetId[0] = '\0';
}

void ledTick() {
    unsigned long now = millis();
    if (s_lastFetchMs != 0 && now - s_lastFetchMs < LED_CACHE_TTL_MS) return;
    fetchFromWled();
}

LedResult ledGetState(JsonObject out) {
    if (!fetchFromWled()) return LedResult::Unreachable;
    writeCachedState(out);
    return LedResult::Ok;
}

LedResult ledGetCachedState(JsonObject out) {
    if (!s_reachable) return LedResult::Unreachable;
    writeCachedState(out);
    return LedResult::Ok;
}

LedResult ledSetState(JsonObjectConst patch, JsonObject out) {
    uint8_t brightness = s_cachedBrightness;
    char color[8];
    strncpy(color, s_cachedColor, sizeof(color) - 1);
    color[sizeof(color) - 1] = '\0';

    if (!patch["brightness"].isNull()) {
        int b = patch["brightness"].as<int>();
        if (b < 0) b = 0;
        if (b > 100) b = 100;
        brightness = (uint8_t)b;
    }

    if (!patch["color"].isNull()) {
        String c = patch["color"].as<String>();
        if (!isValidHexColor(c)) return LedResult::InvalidParameter;
        strncpy(color, c.c_str(), sizeof(color) - 1);
        color[sizeof(color) - 1] = '\0';
        s_presetId[0] = '\0'; // カスタムカラー適用でプリセットはnullにする
    } else if (!patch["presetId"].isNull()) {
        String p = patch["presetId"].as<String>();
        int idx = -1;
        for (size_t i = 0; i < kLedPresetCount; i++) {
            if (p == kLedPresets[i].id) {
                idx = (int)i;
                break;
            }
        }
        if (idx < 0) return LedResult::InvalidParameter;
        snprintf(color, sizeof(color), "#%02x%02x%02x", kLedPresets[idx].r, kLedPresets[idx].g,
                 kLedPresets[idx].b);
        strncpy(s_presetId, kLedPresets[idx].id, sizeof(s_presetId) - 1);
        s_presetId[sizeof(s_presetId) - 1] = '\0';
    }

    // 明るさ0%は消灯として扱う(仕様通り)。presetId/colorが未指定の場合は直前の色を維持する。
    if (!pushToWled(brightness, color)) return LedResult::Unreachable;

    s_cachedBrightness = brightness;
    strncpy(s_cachedColor, color, sizeof(s_cachedColor) - 1);
    s_cachedColor[sizeof(s_cachedColor) - 1] = '\0';

    writeCachedState(out);
    return LedResult::Ok;
}

bool ledIsReachable() { return s_reachable; }
