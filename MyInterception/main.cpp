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
#include "KeyStroke.h"

static constexpr int NUM_OF_KEYS = 256;
static constexpr const char* GENERAL_PROCESS = "general";
static constexpr const char* GENERAL_DEVICE_STRING = "general";
static constexpr InterceptionDevice GENERAL_DEVICE = INT_MAX;

enum class DeviceType {KEY_BOARD, MOUSE, INVALID};

using KeyMapType = std::array<std::array<KeyStroke, 2>, NUM_OF_KEYS>;
using ProcessKeyMapsType = std::unordered_map<std::string, KeyMapType>;
using DeviceKeyMapsType = std::unordered_map<InterceptionDevice, ProcessKeyMapsType>;

InterceptionContext context;
std::unordered_map<std::string, KeyStroke> stringAndKeyCodeRelationMap = {};
KeyMapType defaultKeyCodeMap = { { {KeyStroke()} } };
std::unordered_map<std::string, InterceptionDevice> hidAndDeviceRelation;
std::unordered_map<InterceptionDevice, DeviceType> deviceTypeRelation;

void init();
std::string getTopWindowProcessName();
DeviceKeyMapsType getKeyMaps(); // [HID][Process][OriginalKeyCode] = NewKeyCode
KeyStroke keyStringToKeyStroke(std::string ch);


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
#endif
            KeyStroke::KeyStateType oldState;
            if (s.state < 2) oldState = KeyStroke::KeyStateType::NORMAL;
            else if (s.state >= 2) oldState = KeyStroke::KeyStateType::ALTERNATE_KEY;

            KeyStroke oldStroke = KeyStroke(s.code, oldState), newStroke = oldStroke;
            if (oldStroke.state != KeyStroke::KeyStateType::INVALID) {
                auto hidKeyMaps = keyMaps.find(device);
                ProcessKeyMapsType::iterator processKeyMap = (hidKeyMaps != keyMaps.end()) ? hidKeyMaps->second.find(foregroundProcessName) : generalHidKeyMapIterator->second.find(foregroundProcessName);
                if (hidKeyMaps != keyMaps.end()) { // 該当HIDに設定がある
                    if (processKeyMap != hidKeyMaps->second.end()) { // 該当プロセスに設定がある
                        newStroke = processKeyMap->second[oldStroke.code][static_cast<int>(oldStroke.state)];
                    }
                    else { // 該当プロセスに設定がない
                        processKeyMap = hidKeyMaps->second.find(GENERAL_PROCESS);
                        if (processKeyMap != hidKeyMaps->second.end()) {
                            newStroke = processKeyMap->second[oldStroke.code][static_cast<int>(oldStroke.state)];
                        }
                    }
                }
                else {
                    if (processKeyMap != generalHidKeyMapIterator->second.end()) { // 該当プロセスに設定がある
                        newStroke = processKeyMap->second[oldStroke.code][static_cast<int>(oldStroke.state)];
                    }
                    else { // 該当プロセスに設定がない
                        processKeyMap = generalHidKeyMapIterator->second.find(GENERAL_PROCESS);
                        if (processKeyMap != generalHidKeyMapIterator->second.end()) {
                            newStroke = processKeyMap->second[oldStroke.code][static_cast<int>(oldStroke.state)];
                        }
                    }
                }
            }
#ifdef _DEBUG
            if (oldStroke != newStroke) {
                std::cout << "change stroke: " << newStroke << std::endl;
            }
#endif
            // キーが無効でなければ置き換え
            if (newStroke.code && newStroke.state != KeyStroke::KeyStateType::INVALID) {
                if (oldStroke.state == KeyStroke::KeyStateType::ALTERNATE_KEY && newStroke.state == KeyStroke::KeyStateType::NORMAL) s.state -= 2;
                else if (oldStroke.state == KeyStroke::KeyStateType::NORMAL && newStroke.state == KeyStroke::KeyStateType::ALTERNATE_KEY) s.state += 2;
                s.code = newStroke.code;
            }
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
    for (int i = 0; i < NUM_OF_KEYS; i++) {
        for (auto& state : { KeyStroke::KeyStateType::NORMAL, KeyStroke::KeyStateType::ALTERNATE_KEY }) {
            defaultKeyCodeMap[i][static_cast<int>(state)] = KeyStroke(i, state);
        }
    }

    stringAndKeyCodeRelationMap["esc"] = KeyStroke(1, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["1"] = KeyStroke(2, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["2"] = KeyStroke(3, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["3"] = KeyStroke(4, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["4"] = KeyStroke(5, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["5"] = KeyStroke(6, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["6"] = KeyStroke(7, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["7"] = KeyStroke(8, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["8"] = KeyStroke(9, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["9"] = KeyStroke(10, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["0"] = KeyStroke(11, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["-"] = KeyStroke(12, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["^"] = KeyStroke(13, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["up"] = KeyStroke(72, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["left"] = KeyStroke(75, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["right"] = KeyStroke(77, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["down"] = KeyStroke(80, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["bs"] = KeyStroke(14, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["tab"] = KeyStroke(15, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["enter"] = KeyStroke(28, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["capclock"] = KeyStroke(58, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["|"] = KeyStroke(125, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["]"] = KeyStroke(43, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["_"] = KeyStroke(115, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["lshift"] = KeyStroke(42, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["rshift"] = KeyStroke(54, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["lctrl"] = KeyStroke(29, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["rctrl"] = KeyStroke(29, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["lalt"] = KeyStroke(56, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["ralt"] = KeyStroke(56, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["noconvert"] = KeyStroke(123, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["lwin"] = KeyStroke(91, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["rwin"] = KeyStroke(92, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["space"] = KeyStroke(57, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["convert"] = KeyStroke(121, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["hirakana"] = KeyStroke(112, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F1"] = KeyStroke(59, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F2"] = KeyStroke(60, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F3"] = KeyStroke(61, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F4"] = KeyStroke(62, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F5"] = KeyStroke(63, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F6"] = KeyStroke(64, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F7"] = KeyStroke(65, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F8"] = KeyStroke(66, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F9"] = KeyStroke(67, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F10"] = KeyStroke(68, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F11"] = KeyStroke(87, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["F12"] = KeyStroke(88, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["ins"] = KeyStroke(82, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["delete"] = KeyStroke(83, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["home"] = KeyStroke(71, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["end"] = KeyStroke(79, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["pgup"] = KeyStroke(73, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["pgdn"] = KeyStroke(81, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["ps"] = KeyStroke(55, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["sclock"] = KeyStroke(70, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["pb"] = KeyStroke(69, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["menu"] = KeyStroke(93, KeyStroke::KeyStateType::ALTERNATE_KEY);
    std::string keys = "qwertyuiop@[";
    for (int i = 0; i < keys.size(); i++) {
        stringAndKeyCodeRelationMap[keys.substr(i, 1)] = KeyStroke(16 + i, KeyStroke::KeyStateType::NORMAL);
    }
    keys = "asdfghjkl;:";
    for (int i = 0; i < keys.size(); i++) {
        stringAndKeyCodeRelationMap[keys.substr(i, 1)] = KeyStroke(30 + i, KeyStroke::KeyStateType::NORMAL);
    }
    keys = "zxcvbnm,./";
    for (int i = 0; i < keys.size(); i++) {
        stringAndKeyCodeRelationMap[keys.substr(i, 1)] = KeyStroke(44 + i, KeyStroke::KeyStateType::NORMAL);
    }
    
    // ここからテンキー
    stringAndKeyCodeRelationMap["t0"] = KeyStroke(82, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t1"] = KeyStroke(79, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t2"] = KeyStroke(80, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t3"] = KeyStroke(81, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t4"] = KeyStroke(75, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t5"] = KeyStroke(76, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t6"] = KeyStroke(77, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t7"] = KeyStroke(71, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t8"] = KeyStroke(72, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t9"] = KeyStroke(73, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["tenter"] = KeyStroke(28, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["t+"] = KeyStroke(78, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t-"] = KeyStroke(74, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t*"] = KeyStroke(55, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t/"] = KeyStroke(53, KeyStroke::KeyStateType::ALTERNATE_KEY);
    stringAndKeyCodeRelationMap["numlock"] = KeyStroke(69, KeyStroke::KeyStateType::NORMAL);
    stringAndKeyCodeRelationMap["t."] = KeyStroke(83, KeyStroke::KeyStateType::NORMAL);
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

KeyStroke keyStringToKeyStroke(std::string ch) {
    if (stringAndKeyCodeRelationMap.count(ch)) { // 該当のキーが有る
        return stringAndKeyCodeRelationMap[ch];
    }

    if (ch.size() > 4 && ch.find("code") == 0) { // その他でcodexxの形ならキーコードが入っているはず. stateはとりあえずNORMALとする
        std::string rawCode = ch.substr(4);
        if (std::all_of(rawCode.cbegin(), rawCode.cend(), std::isdigit)) {
            int code = std::stoi(rawCode);
            if (0 <= code && code < NUM_OF_KEYS) {
                return KeyStroke(code, KeyStroke::KeyStateType::NORMAL);
            }
        }
    }

    return KeyStroke(); // 意味のない値
}

DeviceKeyMapsType getKeyMaps() {
    DeviceKeyMapsType keyMaps;
    std::unordered_map<InterceptionDevice, std::unordered_map<std::string, std::unordered_map<KeyStroke, KeyStroke, KeyStrokeHash>>> tmpKeyMaps;
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
                tmpKeyMaps.insert(std::make_pair(device, std::unordered_map<std::string, std::unordered_map<KeyStroke, KeyStroke, KeyStrokeHash>>()));
            }
            continue;
        }

        // section
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            std::cout << "setting process name: " << section << std::endl;
            if (!keyMaps[device].count(section)) {
                tmpKeyMaps[device].insert(std::make_pair(section, std::unordered_map<KeyStroke, KeyStroke, KeyStrokeHash>()));
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
        tmpKeyMaps[device][section][keyStringToKeyStroke(key)] = keyStringToKeyStroke(value);
        std::cout << "assign key: " << keyStringToKeyStroke(key) << " to " << keyStringToKeyStroke(value) << std::endl;
    }

    for (auto eachHidKeyMaps : tmpKeyMaps) {
        for (auto eachProcessKeyMap : eachHidKeyMaps.second) { // 各HIDの各プロセスごとに見ていく
            for (int i = 0; i < NUM_OF_KEYS; i++) {
                for (auto& state : { KeyStroke::KeyStateType::NORMAL, KeyStroke::KeyStateType::ALTERNATE_KEY }) {
                    KeyStroke newKeyCode = KeyStroke(), targetStroke = KeyStroke(i, state);
                    // HID, プロセス毎の設定にあれば, それを使用
                    if (eachProcessKeyMap.second.count(targetStroke)) {
                        newKeyCode = eachProcessKeyMap.second[targetStroke];
                    } // なければHIDごとの, 全プロセス共通の設定を使用
                    else if (tmpKeyMaps.count(eachHidKeyMaps.first) && tmpKeyMaps[eachHidKeyMaps.first][GENERAL_PROCESS].count(targetStroke)) {
                        newKeyCode = tmpKeyMaps[eachHidKeyMaps.first][GENERAL_PROCESS][targetStroke];
                    } // なければ, 全HID共通の, プロセスごとの設定から使用
                    else if (tmpKeyMaps[GENERAL_DEVICE].count(eachProcessKeyMap.first) && tmpKeyMaps[GENERAL_DEVICE][eachProcessKeyMap.first].count(targetStroke)) {
                        newKeyCode = tmpKeyMaps[GENERAL_DEVICE][eachProcessKeyMap.first][targetStroke];
                    } // それもなければ, 全HID共通, 全プロセス共通の設定から使用
                    else if (tmpKeyMaps[GENERAL_DEVICE][GENERAL_PROCESS].count(targetStroke)) {
                        newKeyCode = tmpKeyMaps[GENERAL_DEVICE][GENERAL_PROCESS][targetStroke];
                    } // 設定がなければ既定値
                    else {
                        newKeyCode = targetStroke;
                    }
                    keyMaps[eachHidKeyMaps.first][eachProcessKeyMap.first][targetStroke.code][static_cast<int>(targetStroke.state)] = newKeyCode;
                }
            }
        }
    }

    return keyMaps;
}
