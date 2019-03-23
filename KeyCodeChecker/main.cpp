#include "interception.h"
#include <iostream>
#include <Windows.h>
#include <unordered_map>

enum class DeviceType { KEY_BOARD, MOUSE, INVALID };
std::unordered_map<InterceptionDevice, DeviceType> deviceTypeRelation;
std::unordered_map<std::string, InterceptionDevice> hidAndDeviceRelation;

InterceptionContext context;

void init();

int main() {
	init();
	if (!context) return 0;

	// 入力を処理する
	InterceptionDevice device;
	InterceptionStroke stroke;
	while (interception_receive(context, device = interception_wait(context), &stroke, 1) > 0) {
		auto deviceType = deviceTypeRelation.find(device);
		if (deviceType->second == DeviceType::KEY_BOARD) {
			InterceptionKeyStroke& s = *(InterceptionKeyStroke*)& stroke;
			interception_send(context, device, &stroke, 1);
			if (s.state == 0 || s.state == 2) {
				std::cout << "Keyboard Input "
					<< "ScanCode=" << s.code
					<< " State=" << s.state << std::endl;
			}
		}
		else if (deviceType->second == DeviceType::MOUSE) {
			InterceptionMouseStroke& s = *(InterceptionMouseStroke*)& stroke;
			interception_send(context, device, &stroke, 1);
			std::cout << "Mouse Input"
				<< " State=" << s.state
				<< " Rolling=" << s.rolling
				<< " Flags=" << s.flags
				<< " (x,y)=(" << s.x << "," << s.y << ")"
				<< std::endl;
		}
	}
}

void init() {
	//Interceptionのインスタンス生成
	context = interception_create_context();
	if (!context) return;
	interception_set_filter(context, interception_is_keyboard,
		INTERCEPTION_FILTER_KEY_DOWN |
		INTERCEPTION_FILTER_KEY_UP |
		INTERCEPTION_KEY_E0 |
		INTERCEPTION_KEY_E1
	);
	interception_set_filter(context, interception_is_mouse,
		INTERCEPTION_FILTER_MOUSE_ALL
	);

	// HIDとデバイスを紐付ける
	InterceptionDevice keyboard = INTERCEPTION_MAX_DEVICE, mouse = INTERCEPTION_MAX_DEVICE;
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
}