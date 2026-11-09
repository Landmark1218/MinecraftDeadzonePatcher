#pragma once

struct StickSetting
{
    float inner;   //これ以下は0にする
    float game;    //ゲーム側のデッドゾーン。ここまで持ち上げる
    float curve;   //1.0で直線
};
struct Settings
{
    bool enabled;
    int toggleKey;
    bool logStats;
    bool pauseInMenu;   //メニュー開いてる間は補正しない
    StickSetting left;
    StickSetting right;
};
void LoadSettings(const wchar_t* iniPath);
Settings GetSettings();
void SetEnabled(bool enabled);
FILETIME GetIniTime(const wchar_t* iniPath);
