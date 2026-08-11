#include "web_api.h"
#include "state.h"
#include "toilet_ir.h"
#include "led_control.h"
#include "bgm_control.h"
#include "scenes.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

static AsyncWebServer server(80);

// ---- 共通レスポンスヘルパー ----
static void sendJsonDoc(AsyncWebServerRequest *request, int code, JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    request->send(code, "application/json; charset=utf-8", out);
}

static void sendError(AsyncWebServerRequest *request, int code, const char *errCode, const char *message) {
    JsonDocument doc;
    JsonObject err = doc["error"].to<JsonObject>();
    err["code"] = errCode;
    err["message"] = message;
    sendJsonDoc(request, code, doc);
}

static bool validateStep(long value, long minV, long maxV, long step) {
    if (value < minV || value > maxV) return false;
    return ((value - minV) % step) == 0;
}

// GET/PUTはあるがPOSTは無い、等の「パスは存在するがメソッドが違う」場合に405を返すための一覧。
static const char *kApiPaths[] = {
    "/api/status",       "/api/settings",   "/api/settings/save", "/api/toilet/commands",
    "/api/toilet/state", "/api/led",        "/api/bgm",            "/api/bgm/next",
    "/api/bgm/previous", "/api/bgm/tracks", "/api/scenes",
};

// ---- GET /api/status ----
static void handleStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;
    const char *stateName =
        roomState == RoomState::VACANT ? "VACANT" : (roomState == RoomState::ENTERING ? "ENTERING" : "STAYING");
    doc["roomState"] = stateName;

    JsonObject presence = doc["presence"].to<JsonObject>();
    presence["raw"] = radar.presenceDetected();
    presence["smoothed"] = smoothedPresence;

    JsonObject toilet = doc["toilet"].to<JsonObject>();
    toiletIrWriteState(toilet);

    JsonObject led = doc["led"].to<JsonObject>();
    if (ledGetCachedState(led) != LedResult::Ok) doc["led"] = nullptr;

    JsonObject bgm = doc["bgm"].to<JsonObject>();
    bgmWriteState(bgm);

    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["sensitivity"] = sensitivity;
    settings["maxGate"] = maxGate;
    settings["stayDurationSec"] = stayDurationSec;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = WiFi.status() == WL_CONNECTED;
    wifi["ip"] = WiFi.localIP().toString();

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"] = ENABLE_MQTT;
    mqtt["connected"] = mqttClient.connected();

    JsonObject wled = doc["wled"].to<JsonObject>();
    wled["reachable"] = ledIsReachable();

    doc["uptimeMs"] = millis();

    sendJsonDoc(request, 200, doc);
}

// ---- GET /api/settings ----
static void handleSettingsGet(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["sensitivity"] = sensitivity;
    root["maxGate"] = maxGate;
    root["stayDurationSec"] = stayDurationSec;
    sendJsonDoc(request, 200, doc);
}

// ---- PUT /api/settings ----
static void handleSettingsPut(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_parameter", "invalid JSON body");
        return;
    }
    JsonObjectConst body = json.as<JsonObjectConst>();

    bool hasSensitivity = !body["sensitivity"].isNull();
    bool hasMaxGate = !body["maxGate"].isNull();
    bool hasStay = !body["stayDurationSec"].isNull();

    long newSensitivity = body["sensitivity"] | (long)sensitivity;
    long newMaxGate = body["maxGate"] | (long)maxGate;
    long newStay = body["stayDurationSec"] | (long)stayDurationSec;

    if (hasSensitivity && !validateStep(newSensitivity, SENSITIVITY_MIN, SENSITIVITY_MAX, SENSITIVITY_STEP)) {
        sendError(request, 400, "invalid_parameter", "sensitivity out of range/step");
        return;
    }
    if (hasMaxGate && !validateStep(newMaxGate, MAX_GATE_MIN, MAX_GATE_MAX, MAX_GATE_STEP)) {
        sendError(request, 400, "invalid_parameter", "maxGate out of range/step");
        return;
    }
    if (hasStay && !validateStep(newStay, STAY_DURATION_MIN, STAY_DURATION_MAX, STAY_DURATION_STEP)) {
        sendError(request, 400, "invalid_parameter", "stayDurationSec out of range/step");
        return;
    }

    // all-or-nothing: 検証を全て通過してから反映する
    sensitivity = (uint8_t)newSensitivity;
    maxGate = (uint8_t)newMaxGate;
    stayDurationSec = (uint16_t)newStay;
    applySensitivity();
    applyMaxGate();

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["sensitivity"] = sensitivity;
    root["maxGate"] = maxGate;
    root["stayDurationSec"] = stayDurationSec;
    sendJsonDoc(request, 200, doc);
}

// ---- POST /api/settings/save ----
static void handleSettingsSave(AsyncWebServerRequest *request) {
    saveParams();
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["sensitivity"] = sensitivity;
    root["maxGate"] = maxGate;
    root["stayDurationSec"] = stayDurationSec;
    sendJsonDoc(request, 200, doc);
}

// ---- GET /api/toilet/commands ----
static void handleToiletCommands(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray commands = doc["commands"].to<JsonArray>();
    toiletIrWriteCommandList(commands);
    sendJsonDoc(request, 200, doc);
}

// ---- GET /api/toilet/state ----
static void handleToiletState(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    toiletIrWriteState(root);
    sendJsonDoc(request, 200, doc);
}

// ---- GET /api/led ----
static void handleLedGet(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    if (ledGetState(root) != LedResult::Ok) {
        sendError(request, 502, "wled_unreachable", "WLED did not respond within 1500ms");
        return;
    }
    sendJsonDoc(request, 200, doc);
}

// ---- PUT /api/led ----
static void handleLedPut(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_parameter", "invalid JSON body");
        return;
    }
    JsonObjectConst patch = json.as<JsonObjectConst>();
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    LedResult r = ledSetState(patch, root);
    if (r == LedResult::InvalidParameter) {
        sendError(request, 400, "invalid_parameter", "invalid brightness/color/presetId");
        return;
    }
    if (r == LedResult::Unreachable) {
        sendError(request, 502, "wled_unreachable", "WLED did not respond within 1500ms");
        return;
    }
    sendJsonDoc(request, 200, doc);
}

// ---- GET /api/bgm ----
static void handleBgmGet(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    bgmWriteState(root);
    sendJsonDoc(request, 200, doc);
}

// ---- PUT /api/bgm ----
static void handleBgmPut(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_parameter", "invalid JSON body");
        return;
    }
    JsonObjectConst patch = json.as<JsonObjectConst>();
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    if (!bgmApplyPatch(patch, root)) {
        sendError(request, 400, "invalid_parameter", "invalid bgm parameter");
        return;
    }
    sendJsonDoc(request, 200, doc);
}

// ---- POST /api/bgm/next, /api/bgm/previous ----
static void handleBgmNext(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    bgmNext(root);
    sendJsonDoc(request, 200, doc);
}
static void handleBgmPrevious(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    bgmPrevious(root);
    sendJsonDoc(request, 200, doc);
}

// ---- GET /api/bgm/tracks ----
static void handleBgmTracks(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray tracks = doc["tracks"].to<JsonArray>();
    bgmWriteTracks(tracks);
    sendJsonDoc(request, 200, doc);
}

// ---- GET /api/scenes ----
static void handleScenesGet(AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray scenes = doc["scenes"].to<JsonArray>();
    scenesWriteList(scenes);
    sendJsonDoc(request, 200, doc);
}

// ---- POST /api/scenes ----
static void handleSceneCreate(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!json.is<JsonObject>()) {
        sendError(request, 400, "invalid_parameter", "invalid JSON body");
        return;
    }
    JsonObjectConst body = json.as<JsonObjectConst>();
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    SceneCreateResult r = sceneCreate(body, root);
    if (r == SceneCreateResult::InvalidParameter) {
        sendError(request, 400, "invalid_parameter", "invalid scene parameters");
        return;
    }
    if (r == SceneCreateResult::StorageError) {
        sendError(request, 500, "storage_error", "failed to persist scene to LittleFS");
        return;
    }
    sendJsonDoc(request, 201, doc);
}

// ---- 動的パス(POST /api/toilet/commands/{id}/execute, POST /api/scenes/{id}/execute) ----
// ESPAsyncWebServerの正規表現ルーティングに依存せず、onNotFound内でURLを手動解析する。
static bool tryExtractDynamicId(const String &url, const String &prefix, const String &suffix, String &idOut) {
    if (!url.startsWith(prefix) || !url.endsWith(suffix)) return false;
    if (url.length() < prefix.length() + suffix.length()) return false;
    String id = url.substring(prefix.length(), url.length() - suffix.length());
    if (id.length() == 0 || id.indexOf('/') >= 0) return false;
    idOut = id;
    return true;
}

static void handleNotFound(AsyncWebServerRequest *request) {
    String url = request->url();

    String id;
    if (tryExtractDynamicId(url, "/api/toilet/commands/", "/execute", id)) {
        if (request->method() != HTTP_POST) {
            sendError(request, 405, "method_not_allowed", "only POST is supported");
            return;
        }
        JsonDocument doc;
        JsonObject stateOut = doc["state"].to<JsonObject>();
        ToiletExecuteResult r = toiletIrExecute(id, stateOut);
        switch (r) {
            case ToiletExecuteResult::NotFound:
                sendError(request, 404, "not_found", "unknown command id");
                return;
            case ToiletExecuteResult::NotImplemented:
                sendError(request, 501, "not_implemented", "IR raw data not recorded yet");
                return;
            case ToiletExecuteResult::Cooldown:
                sendError(request, 409, "cooldown", "command is on cooldown");
                return;
            case ToiletExecuteResult::Ok:
                doc["id"] = id;
                sendJsonDoc(request, 200, doc);
                return;
        }
        return;
    }

    if (tryExtractDynamicId(url, "/api/scenes/", "/execute", id)) {
        if (request->method() != HTTP_POST) {
            sendError(request, 405, "method_not_allowed", "only POST is supported");
            return;
        }
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        if (!sceneExecute(id, root)) {
            sendError(request, 404, "not_found", "unknown scene id");
            return;
        }
        sendJsonDoc(request, 200, doc);
        return;
    }

    for (const char *p : kApiPaths) {
        if (url == p) {
            sendError(request, 405, "method_not_allowed", "method not allowed for this endpoint");
            return;
        }
    }

    if (url.startsWith("/api/")) {
        sendError(request, 404, "not_found", "no such endpoint");
        return;
    }
    request->send(404, "text/plain", "Not Found");
}

void webApiBegin() {
    server.on("/api/status", HTTP_GET, handleStatus);

    server.on("/api/settings", HTTP_GET, handleSettingsGet);
    auto *settingsPutHandler = new AsyncCallbackJsonWebHandler("/api/settings", handleSettingsPut);
    settingsPutHandler->setMethod(HTTP_PUT);
    server.addHandler(settingsPutHandler);
    server.on("/api/settings/save", HTTP_POST, handleSettingsSave);

    server.on("/api/toilet/commands", HTTP_GET, handleToiletCommands);
    server.on("/api/toilet/state", HTTP_GET, handleToiletState);

    server.on("/api/led", HTTP_GET, handleLedGet);
    auto *ledPutHandler = new AsyncCallbackJsonWebHandler("/api/led", handleLedPut);
    ledPutHandler->setMethod(HTTP_PUT);
    server.addHandler(ledPutHandler);

    server.on("/api/bgm", HTTP_GET, handleBgmGet);
    auto *bgmPutHandler = new AsyncCallbackJsonWebHandler("/api/bgm", handleBgmPut);
    bgmPutHandler->setMethod(HTTP_PUT);
    server.addHandler(bgmPutHandler);
    server.on("/api/bgm/next", HTTP_POST, handleBgmNext);
    server.on("/api/bgm/previous", HTTP_POST, handleBgmPrevious);
    server.on("/api/bgm/tracks", HTTP_GET, handleBgmTracks);

    server.on("/api/scenes", HTTP_GET, handleScenesGet);
    auto *sceneCreateHandler = new AsyncCallbackJsonWebHandler("/api/scenes", handleSceneCreate);
    sceneCreateHandler->setMethod(HTTP_POST);
    server.addHandler(sceneCreateHandler);

    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("[HTTP] WebUIバックエンドAPIを開始しました(port 80)");
}
