#pragma once
// ============================================================
// BGM曲一覧(全27曲)。WebUIバックエンド仕様.md「BGM（Audio Playerモジュール）」対応。
// SDカード上の実ファイル名と対応させる手動保守テーブル。
// ============================================================
#include <Arduino.h>

struct BgmTrack {
    const char *id;       // "1".."27" (1始まりの連番文字列)
    const char *title;    // 表示名(=ファイル名から拡張子を除いたもの)
    const char *fileName; // SDカード上のファイル名(playAudioByNameで使用)
};

static const BgmTrack kBgmTracks[] = {
    // fileNameは表示名(title)と異なり、SDカード上の実ファイル名。
    // M5UnitAudioPlayerライブラリのplayAudioByName()は27文字を超えるファイル名を
    // 渡すとバッファオーバーフローで再起動する制約があるため(bgm_control.cpp参照)、
    // 元の「At last I can breathe freely#N.mp3」(34文字)から「freely」を除いて
    // 27文字に短縮している。SDカード側のファイル名もこれに合わせてリネームすること。
    {"1", "At last I can breathe freely#1", "At last I can breathe#1.mp3"},
    {"2", "At last I can breathe freely#2", "At last I can breathe#2.mp3"},
    {"3", "At last I can breathe freely#3", "At last I can breathe#3.mp3"},
    {"4", "At last I can breathe freely#4", "At last I can breathe#4.mp3"},
    {"5", "At last I can breathe freely#5", "At last I can breathe#5.mp3"},
    {"6", "At last I can breathe freely#6", "At last I can breathe#6.mp3"},
    {"7", "At last I can breathe freely#7", "At last I can breathe#7.mp3"},
    {"8", "At last I can breathe freely#8", "At last I can breathe#8.mp3"},
    {"9", "The Forest Path#1", "The Forest Path#1.mp3"},
    {"10", "The Forest Path#2", "The Forest Path#2.mp3"},
    {"11", "The Forest Path#3", "The Forest Path#3.mp3"},
    {"12", "The Forest Path#4", "The Forest Path#4.mp3"},
    {"13", "The Forest Path#5", "The Forest Path#5.mp3"},
    {"14", "The Forest Path#6", "The Forest Path#6.mp3"},
    {"15", "The Forest Path#7", "The Forest Path#7.mp3"},
    {"16", "The Forest Path#8", "The Forest Path#8.mp3"},
    {"17", "The Forest Path#9", "The Forest Path#9.mp3"},
    {"18", "The Forest Path#10", "The Forest Path#10.mp3"},
    {"19", "The sky#1", "The sky#1.mp3"},
    {"20", "The sky#2", "The sky#2.mp3"},
    {"21", "The sky#3", "The sky#3.mp3"},
    {"22", "The sky#4", "The sky#4.mp3"},
    {"23", "The sky#5", "The sky#5.mp3"},
    {"24", "The sky#6", "The sky#6.mp3"},
    {"25", "Under water#1", "Under water#1.mp3"},
    {"26", "Under water#2", "Under water#2.mp3"},
    {"27", "Under water#3", "Under water#3.mp3"},
};
static const size_t kBgmTrackCount = sizeof(kBgmTracks) / sizeof(kBgmTracks[0]);

// idからテーブルのインデックスを引く。見つからない場合は-1。
inline int bgmTrackIndexById(const String &id) {
    for (size_t i = 0; i < kBgmTrackCount; i++) {
        if (id == kBgmTracks[i].id) return (int)i;
    }
    return -1;
}
