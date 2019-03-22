#include "interception.h"
#include <string>
#include <iostream>
#include <Windows.h>
#include <psapi.h>

std::string getTopWindowProcessName();

int main() {
	std::string foregroundProcessName = getTopWindowProcessName();

	std::cout << foregroundProcessName << std::endl;
	return 0;
}

std::string getTopWindowProcessName() {
	HWND hWnd = GetForegroundWindow();
	DWORD lpdwProcessId;
	TCHAR processName[MAX_PATH];
	GetWindowThreadProcessId(hWnd, &lpdwProcessId);

	// プロセスをオープン
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, lpdwProcessId);
	if (NULL != hProcess) {
		GetModuleBaseName(hProcess, NULL, processName, _countof(processName));
	}

	return processName;
}