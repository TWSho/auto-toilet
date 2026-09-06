#pragma once
// ============================================================
// MediaPad T5(タブレット)の画面ON/OFF・ロック解除を、マイコンから
// Bluetooth LE HID(HID over GATT Profile)経由で制御するモジュール。
// 2種類のHIDレポートを実装している。
//   ・System Control(USB HIDと同じUsage体系。System Sleep=0x82 /
//     System Wake Up=0x83)で画面のON/OFFを行う
//   ・Mouse(相対移動+左ボタン)でロック画面のスワイプ解除ジェスチャを
//     ドラッグ操作としてシミュレートする(パスワード無し・スワイプのみの
//     ロック画面のため。System ControlのUsageだけでは画面点灯までしか
//     できずスワイプ自体は表現できない)
// 事前にタブレット側のBluetooth設定から本機(広告名"AutoToilet-Tablet-HID")を
// ペアリングしておく必要がある。
// ============================================================

void tabletHidBegin();
void tabletHidWake();  // 画面点灯(System Wake Up) → ロック画面解除スワイプ
void tabletHidSleep(); // System Sleep相当を送信し、画面を消灯+ロックする
