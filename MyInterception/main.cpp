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
static constexpr const wchar_t* GENERAL_PROCESS = L"general";
static constexpr const wchar_t* GENERAL_DEVICE_STRING = L"general";
static constexpr InterceptionDevice GENERAL_DEVICE = INT_MAX;

enum class DeviceType {KEY_BOARD, MOUSE, INVALID};

using KeyCodeType = unsigned char;
using KeyMapType = std::array<int, NUM_OF_KEYS>;
using ProcessKeyMapsType = std::unordered_map<std::wstring, KeyMapType>;
using DeviceKeyMapsType = std::unordered_map<InterceptionDevice, ProcessKeyMapsType>;

InterceptionContext context;
std::unordered_map<std::wstring, KeyCodeType> stringAndKeyCodeRelationMap = {};
KeyMapType defaultKeyCodeMap = { {0} };
std::unordered_map<std::wstring, InterceptionDevice> hidAndDeviceRelation;
std::unordered_map<InterceptionDevice, DeviceType> deviceTypeRelation;

void init();
std::wstring getTopWindowProcessName();
DeviceKeyMapsType getKeyMaps(); // [HID][Process][OriginalKeyCode] = NewKeyCode
KeyCodeType keyStringToKeyCode(std::wstring ch);


static constexpr const wchar_t *s = L"settings.ini";

inline void deleteSpace(std::wstring& buf)
{
	size_t pos;
	while ((pos = buf.find_first_of(L" \t")) != std::wstring::npos) {
		buf.erase(pos, 1);
	}
}


int main() {
	init();

	// �{�^���������邽�т�ini(���ǂ�)��ǂނ킯�ɍs���Ȃ��̂�, ��ɂ��ׂēǂ݂���ł���
	auto keyMaps = getKeyMaps();
	auto generalHidKeyMapIterator = keyMaps.find(GENERAL_DEVICE);

	InterceptionDevice device;
	InterceptionStroke stroke;


	while (interception_receive(context, device = interception_wait(context), &stroke, 1) > 0) {
		// �őO�ʂ̃v���Z�X�����擾
		std::wstring foregroundProcessName = getTopWindowProcessName();
		std::wcout << foregroundProcessName << std::endl;
		std::wcout << L"device: " << device << std::endl;

		auto deviceType = deviceTypeRelation.find(device);
		if (deviceType->second == DeviceType::KEY_BOARD) {
			InterceptionKeyStroke& s = *(InterceptionKeyStroke*)& stroke; 
#ifdef _DEBUG
			std::wcout << L"Keyboard Input "
			<< L"ScanCode=" << s.code
			<< L" State=" << s.state << std::endl;
#endif
			{
				auto hidKeyMaps = keyMaps.find(device);
				ProcessKeyMapsType::iterator processKeyMap = (hidKeyMaps != keyMaps.end()) ? hidKeyMaps->second.find(foregroundProcessName) : generalHidKeyMapIterator->second.find(foregroundProcessName);
				if (hidKeyMaps != keyMaps.end()) { // �Y��HID�ɐݒ肪����
					if (processKeyMap != hidKeyMaps->second.end()) { // �Y���v���Z�X�ɐݒ肪����
#ifdef _DEBUG
						if (s.code != processKeyMap->second[s.code]) {
							std::wcout << "change code: " << s.code << std::endl;
						}
#endif
						s.code = processKeyMap->second[s.code];
					}
					else { // �Y���v���Z�X�ɐݒ肪�Ȃ�
						processKeyMap = hidKeyMaps->second.find(GENERAL_PROCESS);
						if (processKeyMap != hidKeyMaps->second.end()) {
#ifdef _DEBUG
							if (s.code != processKeyMap->second[s.code]) {
								std::wcout << "change code: " << s.code << std::endl;
							}
#endif
							s.code = processKeyMap->second[s.code];
						}
					}
				}
				else {
					if (processKeyMap != generalHidKeyMapIterator->second.end()) { // �Y���v���Z�X�ɐݒ肪����
#ifdef _DEBUG
						if (s.code != processKeyMap->second[s.code]) {
							std::wcout << "change code: " << s.code << std::endl;
						}
#endif
						s.code = processKeyMap->second[s.code];
					}
					else { // �Y���v���Z�X�ɐݒ肪�Ȃ�
						processKeyMap = generalHidKeyMapIterator->second.find(GENERAL_PROCESS);
						if (processKeyMap != generalHidKeyMapIterator->second.end()) {
#ifdef _DEBUG
							if (s.code != processKeyMap->second[s.code]) {
								std::wcout << "change code: " << s.code << std::endl;
							}
#endif
							s.code = processKeyMap->second[s.code];
						}
					}
				}
			}
			interception_send(context, device, &stroke, 1);

		}
		else if (deviceType->second == DeviceType::MOUSE) { // ������
			InterceptionMouseStroke& s = *(InterceptionMouseStroke*)& stroke;
			interception_send(context, device, &stroke, 1);
#ifdef _DEBUG
			std::wcout << L"Mouse Input"
				<< L" State=" << s.state
				<< L" Rolling=" << s.rolling
				<< L" Flags=" << s.flags
				<< L" (x,y)=(" << s.x << L"," << s.y << L")"
				<< std::endl;
#endif
		}
		else {
			//���̃f�o�C�X�̓��͂͒ʉ߂�����
			interception_send(context, device, &stroke, 1);
		}
#ifdef _DEBUG
		std::wcout << std::endl;
#endif
	}
	return 0;
}

void init() {
	// ���D��x��
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	//Interception�̃C���X�^���X����
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

	// HID�ƃf�o�C�X��R�t����
	InterceptionDevice keyboard = INTERCEPTION_MAX_DEVICE, mouse = INTERCEPTION_MAX_DEVICE;
	WCHAR buf[500] = { 0 };
	for (int i = 0; i < INTERCEPTION_MAX_KEYBOARD; i++) {
		InterceptionDevice d = INTERCEPTION_KEYBOARD(i);
		if (interception_get_hardware_id(context, d, buf, sizeof(buf))) {
			hidAndDeviceRelation[buf] = d;
			deviceTypeRelation[d] = DeviceType::KEY_BOARD;
		}
	}
	for (int i = 0; i < INTERCEPTION_MAX_MOUSE; i++) {
		InterceptionDevice d = INTERCEPTION_MOUSE(i);
		if (interception_get_hardware_id(context, d, buf, sizeof(buf))) {
			hidAndDeviceRelation[buf] = d;
			deviceTypeRelation[d] = DeviceType::MOUSE;
		}
	}

	// ��������L�[�R�[�h�ɕϊ�����p�̃}�b�v���쐬
	std::iota(defaultKeyCodeMap.begin(), defaultKeyCodeMap.end(), 0);

	stringAndKeyCodeRelationMap[L"esc"] = 1;
	stringAndKeyCodeRelationMap[L"1"] = 2;
	stringAndKeyCodeRelationMap[L"2"] = 3;
	stringAndKeyCodeRelationMap[L"3"] = 4;
	stringAndKeyCodeRelationMap[L"4"] = 5;
	stringAndKeyCodeRelationMap[L"5"] = 6;
	stringAndKeyCodeRelationMap[L"6"] = 7;
	stringAndKeyCodeRelationMap[L"7"] = 8;
	stringAndKeyCodeRelationMap[L"8"] = 9;
	stringAndKeyCodeRelationMap[L"9"] = 10;
	stringAndKeyCodeRelationMap[L"0"] = 11;
	stringAndKeyCodeRelationMap[L"-"] = 12;
	stringAndKeyCodeRelationMap[L"="] = 12;
	stringAndKeyCodeRelationMap[L"^"] = 13;
	stringAndKeyCodeRelationMap[L"up"] = 72;
	stringAndKeyCodeRelationMap[L"left"] = 75;
	stringAndKeyCodeRelationMap[L"right"] = 77;
	stringAndKeyCodeRelationMap[L"down"] = 80;
	stringAndKeyCodeRelationMap[L"~"] = 13;
	stringAndKeyCodeRelationMap[L"bs"] = 14;
	stringAndKeyCodeRelationMap[L"tab"] = 15;
	stringAndKeyCodeRelationMap[L"enter"] = 28;
	stringAndKeyCodeRelationMap[L"capclock"] = 58;
	stringAndKeyCodeRelationMap[L"|"] = 125;
	stringAndKeyCodeRelationMap[L"]"] = 43;
	stringAndKeyCodeRelationMap[L"_"] = 115;
	stringAndKeyCodeRelationMap[L"lshift"] = 42;
	stringAndKeyCodeRelationMap[L"rshift"] = 54;
	stringAndKeyCodeRelationMap[L"lctrl"] = 29;
	stringAndKeyCodeRelationMap[L"lalt"] = 56;
	stringAndKeyCodeRelationMap[L"noconvert"] = 123;
	stringAndKeyCodeRelationMap[L"lwin"] = 91;
	stringAndKeyCodeRelationMap[L"rwin"] = 92;
	stringAndKeyCodeRelationMap[L"space"] = 57;
	stringAndKeyCodeRelationMap[L"convert"] = 121;
	stringAndKeyCodeRelationMap[L"hirakana"] = 112;
	stringAndKeyCodeRelationMap[L"F1"] = 59;
	stringAndKeyCodeRelationMap[L"F2"] = 60;
	stringAndKeyCodeRelationMap[L"F3"] = 61;
	stringAndKeyCodeRelationMap[L"F4"] = 62;
	stringAndKeyCodeRelationMap[L"F5"] = 63;
	stringAndKeyCodeRelationMap[L"F6"] = 64;
	stringAndKeyCodeRelationMap[L"F7"] = 65;
	stringAndKeyCodeRelationMap[L"F8"] = 66;
	stringAndKeyCodeRelationMap[L"F9"] = 67;
	stringAndKeyCodeRelationMap[L"F10"] = 68;
	stringAndKeyCodeRelationMap[L"F11"] = 87;
	stringAndKeyCodeRelationMap[L"F12"] = 88;
	std::wstring keys = L"qwertyuiop@[";
	for (int i = 0; i < keys.size(); i++) {
		stringAndKeyCodeRelationMap[keys.substr(i, 1)] = 16 + i;
	}
	keys = L"asdfghjkl;:";
	for (int i = 0; i < keys.size(); i++) {
		stringAndKeyCodeRelationMap[keys.substr(i, 1)] = 30 + i;
	}
	keys = L"zxcvbnm,./";
	for (int i = 0; i < keys.size(); i++) {
		stringAndKeyCodeRelationMap[keys.substr(i, 1)] = 44 + i;
	}
}

std::wstring getTopWindowProcessName() {
	DWORD lpdwProcessId;
	wchar_t processName[MAX_PATH] = { 0 };

	// �őO�ʂ̃E�B���h�E���擾
	HWND hWnd = GetForegroundWindow();

	// �őO�ʂ̃v���Z�X��PID���擾
	GetWindowThreadProcessId(hWnd, &lpdwProcessId);

	// �v���Z�X���I�[�v��
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, lpdwProcessId);
	if (NULL != hProcess) {
		GetModuleBaseName(hProcess, NULL, processName, _countof(processName));
	}

	return processName;
}

KeyCodeType keyStringToKeyCode(std::wstring ch) {
	if (stringAndKeyCodeRelationMap.count(ch)) { // �Y���̃L�[���L��
		return stringAndKeyCodeRelationMap[ch];
	}

	if (ch.size() > 4 && ch.find(L"code") == 0) { // ���̑���codexx�̌`�Ȃ�L�[�R�[�h�������Ă���͂�
		std::wstring rawCode = ch.substr(4);
		if (std::all_of(rawCode.cbegin(), rawCode.cend(), std::isdigit)) {
			int code = std::stoi(rawCode);
			if (0 <= code && code < NUM_OF_KEYS) {
				return code;
			}
		}
	}

	return 0; // �Ӗ��̂Ȃ��l
}

DeviceKeyMapsType getKeyMaps() {
	DeviceKeyMapsType keyMaps;
	std::unordered_map<InterceptionDevice, std::unordered_map<std::wstring, std::unordered_map<KeyCodeType, KeyCodeType>>> tmpKeyMaps;
	keyMaps.insert(std::make_pair(NULL, ProcessKeyMapsType()));
	keyMaps[GENERAL_DEVICE].insert(std::make_pair(GENERAL_PROCESS, defaultKeyCodeMap));

	// �ݒ�ǂݍ���
	std::wifstream ifs(s);
	std::wstring line, section = L"";
	InterceptionDevice device = GENERAL_DEVICE;
	while (std::getline(ifs, line)) {
		deleteSpace(line);

		if (line.empty() || line.front() == ';') continue;

		// hid
		if (line.size() >= 5
			&& line.front() == '[' && line[1] == '[' && line.back() == ']' && line[line.size() - 2] == ']') {
			std::wstring hid = line.substr(2, line.size() - 4);
			std::wcout << L"setting HID: " << hid << std::endl;

			if (hid == GENERAL_DEVICE_STRING) {
				device = GENERAL_DEVICE;
				continue;
			}

			// HID�ɑΉ�����f�o�C�X�����݂��Ȃ�
			if (!hidAndDeviceRelation.count(hid)) {
				continue;
			}

			device = hidAndDeviceRelation[hid];
			if (!keyMaps.count(device)) {
				tmpKeyMaps.insert(std::make_pair(device, std::unordered_map<std::wstring, std::unordered_map<KeyCodeType, KeyCodeType>>()));
			}
			continue;
		}

		// section
		if (line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			std::wcout << L"setting process name: " << section << std::endl;
			if (!keyMaps[device].count(section)) {
				tmpKeyMaps[device].insert(std::make_pair(section, std::unordered_map<KeyCodeType, KeyCodeType>()));
			}
			continue;
		}

		// key = value
		size_t equalPos = std::wstring::npos;
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
		// ������ = ��������Ȃ�����
		if (!equalPos || equalPos == std::wstring::npos) {
			continue;
		}
		std::wstring key = line.substr(0, equalPos);
		std::wstring value = line.substr(equalPos + 1);
		tmpKeyMaps[device][section][keyStringToKeyCode(key)] = keyStringToKeyCode(value);
		std::wcout << L"assign key: " << keyStringToKeyCode(key) << L" to " << keyStringToKeyCode(value) << std::endl;
	}

	for (auto eachHidKeyMaps : tmpKeyMaps) {
		for (auto eachProcessKeyMap : eachHidKeyMaps.second) { // �eHID�̊e�v���Z�X���ƂɌ��Ă���
			for (int i = 0; i < NUM_OF_KEYS; i++) {
				KeyCodeType newKeyCode = 0;
				// HID, �v���Z�X���̐ݒ�ɂ����, ������g�p
				if (eachProcessKeyMap.second.count(i)) {
					newKeyCode = eachProcessKeyMap.second[i];
				} // �Ȃ����HID���Ƃ�, �S�v���Z�X���ʂ̐ݒ���g�p
				else if (tmpKeyMaps.count(eachHidKeyMaps.first) && tmpKeyMaps[eachHidKeyMaps.first][GENERAL_PROCESS].count(i)) {
					newKeyCode = tmpKeyMaps[eachHidKeyMaps.first][GENERAL_PROCESS][i];
				} // �Ȃ����, �SHID���ʂ�, �v���Z�X���Ƃ̐ݒ肩��g�p
				else if (tmpKeyMaps[GENERAL_DEVICE].count(eachProcessKeyMap.first) && tmpKeyMaps[GENERAL_DEVICE][eachProcessKeyMap.first].count(i)) {
					newKeyCode = tmpKeyMaps[GENERAL_DEVICE][eachProcessKeyMap.first][i];
				} // ������Ȃ����, �SHID����, �S�v���Z�X���ʂ̐ݒ肩��g�p
				else if (tmpKeyMaps[GENERAL_DEVICE][GENERAL_PROCESS].count(i)) {
					newKeyCode = tmpKeyMaps[GENERAL_DEVICE][GENERAL_PROCESS][i];
				} // �ݒ肪�Ȃ���Ί���l
				else {
					newKeyCode = i;
				}
				keyMaps[eachHidKeyMaps.first][eachProcessKeyMap.first][i] = newKeyCode;
			}
		}
	}

	return keyMaps;
}
