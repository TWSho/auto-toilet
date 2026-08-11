#include "scenes.h"
#include "led_control.h"
#include "bgm_control.h"
#include "bgm_tracks.h"
#include <LittleFS.h>
#include <vector>

struct Scene {
    String id;
    String name;
    bool primary;
    uint8_t ledBrightness; // 0-100
    String ledColor;       // "#rrggbb"
    String bgmTrackId;
};

static const char *SCENES_FILE = "/scenes.json";

// 初期5件。LED色・曲IDは仕様.md「未確定・要検討事項」の通り仮値
// (実際に使う値はフロント側デザインと合わせて決定し、ここを書き換える)。
static std::vector<Scene> s_builtins = {
    {"relax", "リラックス", true, 40, "#ffcf8a", "1"},
    {"focus", "集中", true, 80, "#ffffff", "9"},
    {"party", "パーティ", true, 90, "#8a5cff", "19"},
    {"sleep", "おやすみ", false, 10, "#ff8a8a", "3"},
    {"guest", "来客", false, 60, "#f5f5f0", "1"},
};
static std::vector<Scene> s_custom;

static bool isValidHexColor(const String &s) {
    if (s.length() != 7 || s[0] != '#') return false;
    for (int i = 1; i < 7; i++) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

static const Scene *findScene(const String &id) {
    for (auto &sc : s_builtins)
        if (sc.id == id) return &sc;
    for (auto &sc : s_custom)
        if (sc.id == id) return &sc;
    return nullptr;
}

static void writeSceneJson(JsonObject o, const Scene &sc) {
    o["id"] = sc.id;
    o["name"] = sc.name;
    o["primary"] = sc.primary;
    JsonObject led = o["led"].to<JsonObject>();
    led["brightness"] = sc.ledBrightness;
    led["color"] = sc.ledColor;
    o["bgmTrackId"] = sc.bgmTrackId;
}

static void loadCustomScenes() {
    s_custom.clear();
    if (!LittleFS.exists(SCENES_FILE)) return;
    File f = LittleFS.open(SCENES_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[FS] /scenes.json の読込に失敗: %s\n", err.c_str());
        return;
    }
    for (JsonObject o : doc.as<JsonArray>()) {
        Scene sc;
        sc.id = o["id"].as<String>();
        sc.name = o["name"].as<String>();
        sc.primary = false;
        sc.ledBrightness = o["led"]["brightness"] | 0;
        sc.ledColor = o["led"]["color"].as<String>();
        sc.bgmTrackId = o["bgmTrackId"].as<String>();
        if (sc.id.length() > 0) s_custom.push_back(sc);
    }
}

static bool saveCustomScenes() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (auto &sc : s_custom) writeSceneJson(arr.add<JsonObject>(), sc);

    File f = LittleFS.open(SCENES_FILE, "w");
    if (!f) return false;
    size_t written = serializeJson(doc, f);
    f.close();
    return written > 0;
}

void scenesBegin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS初期化に失敗しました");
        return;
    }
    loadCustomScenes();
}

void scenesWriteList(JsonArray scenes) {
    for (auto &sc : s_builtins) writeSceneJson(scenes.add<JsonObject>(), sc);
    for (auto &sc : s_custom) writeSceneJson(scenes.add<JsonObject>(), sc);
}

bool sceneExecute(const String &id, JsonObject out) {
    const Scene *sc = findScene(id);
    if (!sc) return false;

    JsonDocument ledPatchDoc;
    JsonObject ledPatch = ledPatchDoc.to<JsonObject>();
    ledPatch["brightness"] = sc->ledBrightness;
    ledPatch["color"] = sc->ledColor;

    JsonObject ledOut = out["led"].to<JsonObject>();
    LedResult ledResult = ledSetState(ledPatch, ledOut);
    if (ledResult != LedResult::Ok) {
        out["led"] = nullptr;
        JsonObject ledErr = out["ledError"].to<JsonObject>();
        ledErr["code"] = "wled_unreachable";
        ledErr["message"] = "WLED did not respond within 1500ms";
    }

    JsonDocument bgmPatchDoc;
    JsonObject bgmPatch = bgmPatchDoc.to<JsonObject>();
    bgmPatch["trackId"] = sc->bgmTrackId;
    JsonObject bgmOut = out["bgm"].to<JsonObject>();
    bgmApplyPatch(bgmPatch, bgmOut);

    return true;
}

SceneCreateResult sceneCreate(JsonObjectConst body, JsonObject out) {
    if (body["name"].isNull() || body["led"].isNull() || body["bgmTrackId"].isNull())
        return SceneCreateResult::InvalidParameter;

    String name = body["name"].as<String>();
    if (name.length() == 0) return SceneCreateResult::InvalidParameter;

    JsonObjectConst led = body["led"];
    if (led["brightness"].isNull() || led["color"].isNull()) return SceneCreateResult::InvalidParameter;
    int brightness = led["brightness"].as<int>();
    if (brightness < 0 || brightness > 100) return SceneCreateResult::InvalidParameter;
    String color = led["color"].as<String>();
    if (!isValidHexColor(color)) return SceneCreateResult::InvalidParameter;

    String bgmTrackId = body["bgmTrackId"].as<String>();
    if (bgmTrackIndexById(bgmTrackId) < 0) return SceneCreateResult::InvalidParameter;

    Scene sc;
    sc.id = String("user_") + String((unsigned long)(s_custom.size() + 1));
    sc.name = name;
    sc.primary = false;
    sc.ledBrightness = (uint8_t)brightness;
    sc.ledColor = color;
    sc.bgmTrackId = bgmTrackId;
    s_custom.push_back(sc);

    if (!saveCustomScenes()) {
        s_custom.pop_back();
        return SceneCreateResult::StorageError;
    }

    writeSceneJson(out, sc);
    return SceneCreateResult::Ok;
}
