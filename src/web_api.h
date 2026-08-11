#pragma once
// WebUIバックエンド仕様.md 準拠のHTTP APIサーバー。
// WiFi接続完了後に呼ぶこと(ESPAsyncWebServerはWiFi未接続でもbegin自体は可能だが、
// 待ち受け開始はWiFi接続後に行う運用とする)。
void webApiBegin();
