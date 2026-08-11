#include "toilet_ir.h"
#include "state.h"
#include <Preferences.h>
#include <string.h>

// ---- 既存の「トイレ流し」IR RAWデータ(flush_large用、収録済み) ----
static const uint16_t kFlushLargeRawData[] = {5930, 2964, 568, 578, 542, 1700, 542, 554, 566, 554, 566, 552, 566, 552, 540, 576, 542, 576, 542, 576,540, 550, 564, 550, 564, 1672, 568, 552, 542, 576, 542, 576, 542, 576, 540, 550, 566, 550, 564, 552, 566, 550, 538, 576, 540, 574, 540, 576,540, 1668, 566, 552, 566, 1672, 542, 1700, 544, 578, 542, 1676, 568, 1678, 544, 580, 542, 1702, 544, 580, 542, 1676, 568, 1678, 542, 580, 542, 1702, 544, 1678, 568, 554, 568, 32920, 5932, 2964, 570, 578, 542, 1674, 568, 554, 568, 552, 566, 552, 566, 550, 540, 576, 542, 578, 540, 576, 540, 550, 564, 552, 564, 1670, 540, 578, 542, 576, 542, 578, 540, 576, 540, 552, 564, 550, 566, 550, 564, 550, 538, 574, 540, 574, 542, 574, 540, 1670, 566, 552, 566, 1672, 542, 1702, 542, 580, 542, 1676, 568, 1678, 542, 580, 542, 1702, 544, 552, 568, 1676, 568, 1678, 542, 580,542, 1702, 544, 1678, 568, 554, 568};
static const uint16_t kFlushLargeRawLen = 163;

// ---- コマンドテーブル(WebUIバックエンド仕様.md「トイレ（IRコマンドテーブル）」) ----
static const IrCommand kIrCommands[] = {
    {"flush_large", "momentary", kFlushLargeRawData, kFlushLargeRawLen},
    {"flush_small", "momentary", nullptr, 0},
    {"flush_eco", "momentary", nullptr, 0},
    {"spray_off", "select", nullptr, 0},
    {"spray_rear", "select", nullptr, 0},
    {"spray_soft", "select", nullptr, 0},
    {"spray_bidet", "select", nullptr, 0},
    {"water_pressure_up", "step", nullptr, 0},
    {"water_pressure_down", "step", nullptr, 0},
    {"nozzle_position_forward", "step", nullptr, 0},
    {"nozzle_position_backward", "step", nullptr, 0},
    {"seat_temp_cycle", "cycle", nullptr, 0},
    {"water_temp_cycle", "cycle", nullptr, 0},
    {"deodorizer_on", "toggle", nullptr, 0},
    {"deodorizer_off", "toggle", nullptr, 0},
    {"nozzle_clean", "momentary", nullptr, 0},
    {"auto_clean_on", "toggle", nullptr, 0},
    {"auto_clean_off", "toggle", nullptr, 0},
};
static const size_t kIrCommandCount = sizeof(kIrCommands) / sizeof(kIrCommands[0]);

// kindごとの連続実行間引き間隔(ms)
static unsigned long cooldownMsForKind(const char *kind) {
    if (strcmp(kind, "momentary") == 0) return 2000;
    if (strcmp(kind, "step") == 0) return 150;
    if (strcmp(kind, "cycle") == 0) return 150;
    return 300; // select / toggle
}

static unsigned long lastExecuteMs[kIrCommandCount] = {0};

static const char *NVS_STATE_NAMESPACE = "toiletst";

ToiletTrackedState toiletState = {
    "off", // spray
    3,     // waterPressure
    3,     // nozzlePosition
    1,     // seatTemp
    1,     // waterTemp
    false, // deodorizer
    true,  // autoClean
};

static void saveToiletState() {
    Preferences p;
    p.begin(NVS_STATE_NAMESPACE, false);
    p.putString("spray", toiletState.spray);
    p.putUChar("waterPressure", toiletState.waterPressure);
    p.putUChar("nozzlePosition", toiletState.nozzlePosition);
    p.putUChar("seatTemp", toiletState.seatTemp);
    p.putUChar("waterTemp", toiletState.waterTemp);
    p.putBool("deodorizer", toiletState.deodorizer);
    p.putBool("autoClean", toiletState.autoClean);
    p.end();
}

void toiletIrBegin() {
    Preferences p;
    p.begin(NVS_STATE_NAMESPACE, true);
    if (p.isKey("spray")) {
        String spray = p.getString("spray", toiletState.spray);
        strncpy(toiletState.spray, spray.c_str(), sizeof(toiletState.spray) - 1);
        toiletState.spray[sizeof(toiletState.spray) - 1] = '\0';
        toiletState.waterPressure = p.getUChar("waterPressure", toiletState.waterPressure);
        toiletState.nozzlePosition = p.getUChar("nozzlePosition", toiletState.nozzlePosition);
        toiletState.seatTemp = p.getUChar("seatTemp", toiletState.seatTemp);
        toiletState.waterTemp = p.getUChar("waterTemp", toiletState.waterTemp);
        toiletState.deodorizer = p.getBool("deodorizer", toiletState.deodorizer);
        toiletState.autoClean = p.getBool("autoClean", toiletState.autoClean);
    }
    p.end();
}

void toiletIrWriteCommandList(JsonArray commands) {
    for (size_t i = 0; i < kIrCommandCount; i++) {
        JsonObject c = commands.add<JsonObject>();
        c["id"] = kIrCommands[i].id;
        c["kind"] = kIrCommands[i].kind;
        c["implemented"] = kIrCommands[i].rawData != nullptr;
    }
}

void toiletIrWriteState(JsonObject out) {
    out["spray"] = toiletState.spray;
    out["waterPressure"] = toiletState.waterPressure;
    out["nozzlePosition"] = toiletState.nozzlePosition;
    out["seatTemp"] = toiletState.seatTemp;
    out["waterTemp"] = toiletState.waterTemp;
    out["deodorizer"] = toiletState.deodorizer;
    out["autoClean"] = toiletState.autoClean;
}

static void setSpray(const char *value) {
    strncpy(toiletState.spray, value, sizeof(toiletState.spray) - 1);
    toiletState.spray[sizeof(toiletState.spray) - 1] = '\0';
}

ToiletExecuteResult toiletIrExecute(const String &id, JsonObject stateOut) {
    int index = -1;
    for (size_t i = 0; i < kIrCommandCount; i++) {
        if (id == kIrCommands[i].id) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) return ToiletExecuteResult::NotFound;

    const IrCommand &cmd = kIrCommands[index];
    if (cmd.rawData == nullptr) return ToiletExecuteResult::NotImplemented;

    unsigned long now = millis();
    unsigned long cooldown = cooldownMsForKind(cmd.kind);
    if (now - lastExecuteMs[index] < cooldown) return ToiletExecuteResult::Cooldown;
    lastExecuteMs[index] = now;

    irSender.sendRaw(cmd.rawData, cmd.rawLen, IR_FREQUENCY_KHZ);
    Serial.printf("[IR] トイレコマンド送信: %s\n", cmd.id);

    bool stateChanged = true;
    if (strcmp(cmd.kind, "momentary") == 0) {
        stateChanged = false;
    } else if (strcmp(cmd.kind, "select") == 0) {
        if (id == "spray_off") setSpray("off");
        else if (id == "spray_rear") setSpray("rear");
        else if (id == "spray_soft") setSpray("soft");
        else if (id == "spray_bidet") setSpray("bidet");
        stateOut["spray"] = toiletState.spray;
    } else if (strcmp(cmd.kind, "toggle") == 0) {
        if (id == "deodorizer_on") toiletState.deodorizer = true;
        else if (id == "deodorizer_off") toiletState.deodorizer = false;
        else if (id == "auto_clean_on") toiletState.autoClean = true;
        else if (id == "auto_clean_off") toiletState.autoClean = false;
        if (id.startsWith("deodorizer")) stateOut["deodorizer"] = toiletState.deodorizer;
        else stateOut["autoClean"] = toiletState.autoClean;
    } else if (strcmp(cmd.kind, "step") == 0) {
        if (id == "water_pressure_up") {
            if (toiletState.waterPressure < 5) toiletState.waterPressure++;
            stateOut["waterPressure"] = toiletState.waterPressure;
        } else if (id == "water_pressure_down") {
            if (toiletState.waterPressure > 1) toiletState.waterPressure--;
            stateOut["waterPressure"] = toiletState.waterPressure;
        } else if (id == "nozzle_position_forward") {
            if (toiletState.nozzlePosition < 5) toiletState.nozzlePosition++;
            stateOut["nozzlePosition"] = toiletState.nozzlePosition;
        } else if (id == "nozzle_position_backward") {
            if (toiletState.nozzlePosition > 1) toiletState.nozzlePosition--;
            stateOut["nozzlePosition"] = toiletState.nozzlePosition;
        }
    } else if (strcmp(cmd.kind, "cycle") == 0) {
        if (id == "seat_temp_cycle") {
            toiletState.seatTemp = (toiletState.seatTemp % 3) + 1;
            stateOut["seatTemp"] = toiletState.seatTemp;
        } else if (id == "water_temp_cycle") {
            toiletState.waterTemp = (toiletState.waterTemp % 3) + 1;
            stateOut["waterTemp"] = toiletState.waterTemp;
        }
    }

    if (stateChanged) saveToiletState();
    return ToiletExecuteResult::Ok;
}
