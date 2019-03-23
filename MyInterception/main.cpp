#include "interception.h"
#include <string>
#include <iostream>
#include <Windows.h>
#include <psapi.h>
#include <sstream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <array>
#include <numeric>
#include <cctype>

static constexpr int NUM_OF_KEYS = 256;
static constexpr const char* GENERAL_PROCESS = "general";
static constexpr const char* GENERAL_DEVICE_STRING = "general";
static constexpr InterceptionDevice GENERAL_DEVICE = INT_MAX;

enum class DeviceType {KEY_BOARD, MOUSE, INVALID};

using KeyCodeType = unsigned char;
using KeyMapType = std::array<int, NUM_OF_KEYS>;
using ProcessKeyMapsType = std::unordered_map<std::string, KeyMapType>;
using DeviceKeyMapsType = std::unordered_map<InterceptionDevice, ProcessKeyMapsType>;

InterceptionContext context;
std::unordered_map<std::string, KeyCodeType> stringAndKeyCodeRelationMap = {};
std::array<KeyCodeType, 256> keyCodeAndStateRelationMap = {};
KeyMapType defaultKeyCodeMap = { {0} };
std::unordered_map<std::string, InterceptionDevice> hidAndDeviceRelation;
std::unordered_map<InterceptionDevice, DeviceType> deviceTypeRelation;

void init();
std::string getTopWindowProcessName();
DeviceKeyMapsType getKeyMaps(); // [HID][Process][OriginalKeyCode] = NewKeyCode
KeyCodeType keyStringToKeyCode(std::string ch);


static constexpr const char *s = "settings.ini";

inline void deleteSpace(std::string& buf)
{
    size_t pos;
    while ((pos = buf.find_first_of(" \t")) != std::string::npos) {
        buf.erase(pos, 1);
    }
}


int main() {
    init();
    if (!context) return 0;

    // ボタン押させるたびにini(もどき)を読むわけに行かないので, 先にすべて読みこんでおく
    auto keyMaps = getKeyMaps();
    auto generalHidKeyMapIterator = keyMaps.find(GENERAL_DEVICE);

    InterceptionDevice device;
    InterceptionStroke stroke;

    std::cout << "キー入力受付中..." << std::endl;

    while (interception_receive(context, device = interception_wait(context), &stroke, 1) > 0) {
        // 最前面のプロセス名を取得
        std::string foregroundProcessName = getTopWindowProcessName();
#ifdef _DEBUG
        std::cout << foregroundProcessName << std::endl;
        std::cout << "device: " << device << std::endl;
#endif
        auto deviceType = deviceTypeRelation.find(device);
        if (deviceType->second == DeviceType::KEY_BOARD) {
            InterceptionKeyStroke& s = *(InterceptionKeyStroke*)& stroke; 
#ifdef _DEBUG
            std::cout << "Keyboard Input " 
                << "Code=" << s.code 
                << " State=" << s.state
                << std::endl;
            int oldState = s.state;
#endif
            {
                auto hidKeyMaps = keyMaps.find(device);
                ProcessKeyMapsType::iterator processKeyMap = (hidKeyMaps != keyMaps.end()) ? hidKeyMaps->second.find(foregroundProcessName) : generalHidKeyMapIterator->second.find(foregroundProcessName);
                if (hidKeyMaps != keyMaps.end()) { // 該当HIDに設定がある
                    if (processKeyMap != hidKeyMaps->second.end()) { // 該当プロセスに設定がある
#ifdef _DEBUG
                        if (s.code != processKeyMap->second[s.code]) {
                            std::cout << "change code: " << processKeyMap->second[s.code] << std::endl;
                        }
#endif
                        s.code = processKeyMap->second[s.code];
                    }
                    else { // 該当プロセスに設定がない
                        processKeyMap = hidKeyMaps->second.find(GENERAL_PROCESS);
                        if (processKeyMap != hidKeyMaps->second.end()) {
#ifdef _DEBUG
                            if (s.code != processKeyMap->second[s.code]) {
                                std::cout << "change code: " << processKeyMap->second[s.code] << std::endl;
                            }
#endif
                            s.code = processKeyMap->second[s.code];
                        }
                    }
                }
                else {
                    if (processKeyMap != generalHidKeyMapIterator->second.end()) { // 該当プロセスに設定がある
#ifdef _DEBUG
                        if (s.code != processKeyMap->second[s.code]) {
                            std::cout << "change code: " << processKeyMap->second[s.code] << std::endl;
                        }
#endif
                        s.code = processKeyMap->second[s.code];
                    }
                    else { // 該当プロセスに設定がない
                        processKeyMap = generalHidKeyMapIterator->second.find(GENERAL_PROCESS);
                        if (processKeyMap != generalHidKeyMapIterator->second.end()) {
#ifdef _DEBUG
                            if (s.code != processKeyMap->second[s.code]) {
                                std::cout << "change code: " << processKeyMap->second[s.code] << std::endl;
                            }
#endif
                            s.code = processKeyMap->second[s.code];
                        }
                    }
                }
            }
            int newState = keyCodeAndStateRelationMap[s.code];
            if (oldState >= 2 && newState < 2) s.state -= 2;
            else if (oldState < 2 && newState >= 2) s.state += 2;
            interception_send(context, device, &stroke, 1);

        }
        else if (deviceType->second == DeviceType::MOUSE) { // 未実装
            InterceptionMouseStroke& s = *(InterceptionMouseStroke*)& stroke;
            interception_send(context, device, &stroke, 1);
#ifdef _DEBUG
            std::cout << "Mouse Input"
                << " State=" << s.state
                << " Rolling=" << s.rolling
                << " Flags=" << s.flags
                << " (x, y)=(" << s.x << ", " << s.y << ")"
                << std::endl;
#endif
        }
        else {
            //他のデバイスの入力は通過させる
            interception_send(context, device, &stroke, 1);
        }
#ifdef _DEBUG
        std::cout << std::endl;
#endif
    }
    return 0;
}

void init() {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IOFBF, 1024);
    // 高優先度化
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    //Interceptionのインスタンス生成
    context = interception_create_context();
    if (!context) return;
    interception_set_filter(context, interception_is_keyboard,
        INTERCEPTION_FILTER_KEY_DOWN |
        INTERCEPTION_FILTER_KEY_UP |
        INTERCEPTION_KEY_E0 |
        INTERCEPTION_KEY_E1);
    interception_set_filter(context, interception_is_mouse,
        INTERCEPTION_MOUSE_WHEEL |
        INTERCEPTION_FILTER_MOUSE_MIDDLE_BUTTON_DOWN |
        INTERCEPTION_FILTER_MOUSE_MIDDLE_BUTTON_UP
    );

    // HIDとデバイスを紐付ける
    wchar_t wbuf[501] = { 0 };
    char buf[501] = { 0 };
    for (int i = 0; i < INTERCEPTION_MAX_KEYBOARD; i++) {
        InterceptionDevice d = INTERCEPTION_KEYBOARD(i);
        if (interception_get_hardware_id(context, d, wbuf, sizeof(buf))) {
            size_t n;
            wcstombs_s(&n, buf, 500, wbuf, 500);
            hidAndDeviceRelation[buf] = d;
            deviceTypeRelation[d] = DeviceType::KEY_BOARD;
        }
    }
    for (int i = 0; i < INTERCEPTION_MAX_MOUSE; i++) {
        InterceptionDevice d = INTERCEPTION_MOUSE(i);
        if (interception_get_hardware_id(context, d, wbuf, sizeof(buf))) {
            size_t n;
            wcstombs_s(&n, buf, 500, wbuf, 500);
            hidAndDeviceRelation[buf] = d;
            deviceTypeRelation[d] = DeviceType::MOUSE;
        }
    }

    // 文字からキーコードに変換する用のマップを作成
    std::iota(defaultKeyCodeMap.begin(), defaultKeyCodeMap.end(), 0);
    keyCodeAndStateRelationMap.fill(0);

    stringAndKeyCodeRelationMap["esc"] = 1;
    stringAndKeyCodeRelationMap["1"] = 2;
    stringAndKeyCodeRelationMap["2"] = 3;
    stringAndKeyCodeRelationMap["3"] = 4;
    stringAndKeyCodeRelationMap["4"] = 5;
    stringAndKeyCodeRelationMap["5"] = 6;
    stringAndKeyCodeRelationMap["6"] = 7;
    stringAndKeyCodeRelationMap["7"] = 8;
    stringAndKeyCodeRelationMap["8"] = 9;
    stringAndKeyCodeRelationMap["9"] = 10;
    stringAndKeyCodeRelationMap["0"] = 11;
    stringAndKeyCodeRelationMap["-"] = 12;
    stringAndKeyCodeRelationMap["^"] = 13;
    stringAndKeyCodeRelationMap["up"] = 72;
    stringAndKeyCodeRelationMap["left"] = 75;
    stringAndKeyCodeRelationMap["right"] = 77;
    stringAndKeyCodeRelationMap["down"] = 80;
    keyCodeAndStateRelationMap[72] = 2;
    keyCodeAndStateRelationMap[75] = 2;
    keyCodeAndStateRelationMap[77] = 2;
    keyCodeAndStateRelationMap[80] = 2;
    stringAndKeyCodeRelationMap["~"] = 13;
    stringAndKeyCodeRelationMap["bs"] = 14;
    stringAndKeyCodeRelationMap["tab"] = 15;
    stringAndKeyCodeRelationMap["enter"] = 28;
    stringAndKeyCodeRelationMap["capclock"] = 58;
    stringAndKeyCodeRelationMap["|"] = 125;
    stringAndKeyCodeRelationMap["]"] = 43;
    stringAndKeyCodeRelationMap["_"] = 115;
    stringAndKeyCodeRelationMap["lshift"] = 42;
    stringAndKeyCodeRelationMap["rshift"] = 54;
    stringAndKeyCodeRelationMap["lctrl"] = 29;
    stringAndKeyCodeRelationMap["lalt"] = 56;
    stringAndKeyCodeRelationMap["noconvert"] = 123;
    stringAndKeyCodeRelationMap["lwin"] = 91;
    stringAndKeyCodeRelationMap["rwin"] = 92;
    stringAndKeyCodeRelationMap["space"] = 57;
    stringAndKeyCodeRelationMap["convert"] = 121;
    stringAndKeyCodeRelationMap["hirakana"] = 112;
    stringAndKeyCodeRelationMap["F1"] = 59;
    stringAndKeyCodeRelationMap["F2"] = 60;
    stringAndKeyCodeRelationMap["F3"] = 61;
    stringAndKeyCodeRelationMap["F4"] = 62;
    stringAndKeyCodeRelationMap["F5"] = 63;
    stringAndKeyCodeRelationMap["F6"] = 64;
    stringAndKeyCodeRelationMap["F7"] = 65;
    stringAndKeyCodeRelationMap["F8"] = 66;
    stringAndKeyCodeRelationMap["F9"] = 67;
    stringAndKeyCodeRelationMap["F10"] = 68;
    stringAndKeyCodeRelationMap["F11"] = 87;
    stringAndKeyCodeRelationMap["F12"] = 88;
    std::string keys = "qwertyuiop@[";
    for (int i = 0; i < keys.size(); i++) {
        stringAndKeyCodeRelationMap[keys.substr(i, 1)] = 16 + i;
    }
    keys = "asdfghjkl;:";
    for (int i = 0; i < keys.size(); i++) {
        stringAndKeyCodeRelationMap[keys.substr(i, 1)] = 30 + i;
    }
    keys = "zxcvbnm,./";
    for (int i = 0; i < keys.size(); i++) {
        stringAndKeyCodeRelationMap[keys.substr(i, 1)] = 44 + i;
    }
}

std::string getTopWindowProcessName() {
    DWORD lpdwProcessId;
    char processName[MAX_PATH] = { 0 };

    // 最前面のウィンドウを取得
    HWND hWnd = GetForegroundWindow();

    // 最前面のプロセスのPIDを取得
    GetWindowThreadProcessId(hWnd, &lpdwProcessId);

    // プロセスをオープン
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, lpdwProcessId);
    if (NULL != hProcess) {
        GetModuleBaseName(hProcess, NULL, processName, _countof(processName));
    }

    return processName;
}

KeyCodeType keyStringToKeyCode(std::string ch) {
    if (stringAndKeyCodeRelationMap.count(ch)) { // 該当のキーが有る
        return stringAndKeyCodeRelationMap[ch];
    }

    if (ch.size() > 4 && ch.find("code") == 0) { // その他でcodexxの形ならキーコードが入っているはず
        std::string rawCode = ch.substr(4);
        if (std::all_of(rawCode.cbegin(), rawCode.cend(), std::isdigit)) {
            int code = std::stoi(rawCode);
            if (0 <= code && code < NUM_OF_KEYS) {
                return code;
            }
        }
    }

    return 0; // 意味のない値
}

DeviceKeyMapsType getKeyMaps() {
    DeviceKeyMapsType keyMaps;
    std::unordered_map<InterceptionDevice, std::unordered_map<std::string, std::unordered_map<KeyCodeType, KeyCodeType>>> tmpKeyMaps;
    keyMaps.insert(std::make_pair(NULL, ProcessKeyMapsType()));
    keyMaps[GENERAL_DEVICE].insert(std::make_pair(GENERAL_PROCESS, defaultKeyCodeMap));

    // 設定読み込み
    std::ifstream ifs(s);
    std::string line, section = "";
    InterceptionDevice device = GENERAL_DEVICE;
    while (std::getline(ifs, line)) {
        deleteSpace(line);
        if (line.size() > 128) continue; // 文字が長すぎ

        if (line.empty() || line.front() == ';') continue;

        // hid
        if (line.size() >= 5
            && line.front() == '[' && line[1] == '[' && line.back() == ']' && line[line.size() - 2] == ']') {
            std::string hid = line.substr(2, line.size() - 4);
            std::cout << "setting HID: " << hid << std::endl;

            if (hid == GENERAL_DEVICE_STRING) {
                device = GENERAL_DEVICE;
                continue;
            }

            // HIDに対応するデバイスが存在しない
            if (!hidAndDeviceRelation.count(hid)) {
                continue;
            }

            device = hidAndDeviceRelation[hid];
            if (!keyMaps.count(device)) {
                tmpKeyMaps.insert(std::make_pair(device, std::unordered_map<std::string, std::unordered_map<KeyCodeType, KeyCodeType>>()));
            }
            continue;
        }

        // section
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            std::cout << "setting process name: " << section << std::endl;
            if (!keyMaps[device].count(section)) {
                tmpKeyMaps[device].insert(std::make_pair(section, std::unordered_map<KeyCodeType, KeyCodeType>()));
            }
            continue;
        }

        // key = value
        size_t equalPos = std::string::npos;
        enum class State {NORMAL, ESCAPED};
        State state = State::NORMAL;
        int pos = 0;
        for (auto ch : line) {
            switch (state)
            {
            case State::NORMAL:
                if (ch == '=') {
                    equalPos = pos;
                    break;
                }
                else if (ch == '\\') {
                    state = State::ESCAPED;
                }
                break;
            case State::ESCAPED:
                state = State::NORMAL;
                break;
            default:
                break;
            }
            pos++;
        }
        // 正しい = が見つからなかった
        if (!equalPos || equalPos == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, equalPos);
        std::string value = line.substr(equalPos + 1);
        tmpKeyMaps[device][section][keyStringToKeyCode(key)] = keyStringToKeyCode(value);
        std::cout << "assign key: " << static_cast<int>(keyStringToKeyCode(key)) << " to " << static_cast<int>(keyStringToKeyCode(value)) << std::endl;
    }

    for (auto eachHidKeyMaps : tmpKeyMaps) {
        for (auto eachProcessKeyMap : eachHidKeyMaps.second) { // 各HIDの各プロセスごとに見ていく
            for (int i = 0; i < NUM_OF_KEYS; i++) {
                KeyCodeType newKeyCode = 0;
                // HID, プロセス毎の設定にあれば, それを使用
                if (eachProcessKeyMap.second.count(i)) {
                    newKeyCode = eachProcessKeyMap.second[i];
                } // なければHIDごとの, 全プロセス共通の設定を使用
                else if (tmpKeyMaps.count(eachHidKeyMaps.first) && tmpKeyMaps[eachHidKeyMaps.first][GENERAL_PROCESS].count(i)) {
                    newKeyCode = tmpKeyMaps[eachHidKeyMaps.first][GENERAL_PROCESS][i];
                } // なければ, 全HID共通の, プロセスごとの設定から使用
                else if (tmpKeyMaps[GENERAL_DEVICE].count(eachProcessKeyMap.first) && tmpKeyMaps[GENERAL_DEVICE][eachProcessKeyMap.first].count(i)) {
                    newKeyCode = tmpKeyMaps[GENERAL_DEVICE][eachProcessKeyMap.first][i];
                } // それもなければ, 全HID共通, 全プロセス共通の設定から使用
                else if (tmpKeyMaps[GENERAL_DEVICE][GENERAL_PROCESS].count(i)) {
                    newKeyCode = tmpKeyMaps[GENERAL_DEVICE][GENERAL_PROCESS][i];
                } // 設定がなければ既定値
                else {
                    newKeyCode = i;
                }
                keyMaps[eachHidKeyMaps.first][eachProcessKeyMap.first][i] = newKeyCode;
            }
        }
    }

    return keyMaps;
}
