#include "stdafx.h"
#include <string>
#include <vector>
#include <WeaselCommon.h>

using namespace std;
using boost::filesystem::wpath;

BOOL copy_file(const wstring& src, const wstring& dest)
{
	BOOL ret = CopyFile(src.c_str(), dest.c_str(), FALSE);
	if (!ret)
	{
		for (int i = 0; i < 10; ++i)
		{
			wstring old = (boost::wformat(L"%1%.old.%2%") % dest % i).str();
			if (MoveFileEx(dest.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				MoveFileEx(old.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
				break;
			}
		}
		ret = CopyFile(src.c_str(), dest.c_str(), FALSE);
	}
	return ret;
}

BOOL delete_file(const wstring& file)
{
	BOOL ret = DeleteFile(file.c_str());
	if (!ret)
	{
		for (int i = 0; i < 10; ++i)
		{
			wstring old = (boost::wformat(L"%1%.old.%2%") % file % i).str();
			if (MoveFileEx(file.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				MoveFileEx(old.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
				return TRUE;
			}
		}
	}
	return ret;
}

int install(bool hant, bool silent)
{
	wpath srcPath;
	wpath destPath;
	wpath wow64Path;

	WCHAR path[MAX_PATH];
	GetModuleFileName(GetModuleHandle(NULL), path, _countof(path));
	srcPath = path;

	bool is_x64 = (sizeof(HANDLE) == 8);
	wstring srcFileName = (hant ? L"weaselt" : L"weasel");
	srcFileName += (is_x64 ? L"x64.ime" : L".ime");
	srcPath = srcPath.remove_leaf() / srcFileName;

	GetSystemDirectory(path, _countof(path));
	destPath = path;
	destPath /= WEASEL_IME_FILE;

	if (GetSystemWow64Directory(path, _countof(path)))
	{
		wow64Path = path;
		wow64Path /= WEASEL_IME_FILE;
	}

	// ���� .ime ��ϵͳĿ¼
	if (!copy_file(srcPath.native_file_string(), destPath.native_file_string()))
	{
		if (!silent) MessageBox(NULL, destPath.native_file_string().c_str(), L"���bʧ��", MB_ICONERROR | MB_OK);
		return 1;
	}
	if (!wow64Path.empty())
	{
		wstring x86 = srcPath.native_file_string();
		boost::algorithm::ireplace_last(x86, L"x64.ime", L".ime");
		if (!copy_file(x86, wow64Path.native_file_string()))
		{
			if (!silent) MessageBox(NULL, wow64Path.native_file_string().c_str(), L"���bʧ��", MB_ICONERROR | MB_OK);
			return 1;
		}
	}

	// дע���
	HKEY hKey;
	LSTATUS ret = RegCreateKeyEx(HKEY_LOCAL_MACHINE, WEASEL_REG_KEY,
		                         0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, 0, &hKey, NULL);
	if (FAILED(HRESULT_FROM_WIN32(ret)))
	{
		if (!silent) MessageBox(NULL, WEASEL_REG_KEY, L"���bʧ��", MB_ICONERROR | MB_OK);
		return 1;
	}

	wstring rootDir = srcPath.parent_path().native_directory_string();
	ret = RegSetValueEx(hKey, L"WeaselRoot", 0, REG_SZ,
		                (const BYTE*)rootDir.c_str(),
						(rootDir.length() + 1) * sizeof(WCHAR));
	if (FAILED(HRESULT_FROM_WIN32(ret)))
	{
		if (!silent) MessageBox(NULL, L"�o������ WeaselRoot", L"���bʧ��", MB_ICONERROR | MB_OK);
		return 1;
	}

	const wstring executable = L"WeaselServer.exe";
	ret = RegSetValueEx(hKey, L"ServerExecutable", 0, REG_SZ,
		                (const BYTE*)executable.c_str(),
						(executable.length() + 1) * sizeof(WCHAR));
	if (FAILED(HRESULT_FROM_WIN32(ret)))
	{
		if (!silent) MessageBox(NULL, L"�o�������]�Ա��Iֵ ServerExecutable", L"���bʧ��", MB_ICONERROR | MB_OK);
		return 1;
	}

	RegCloseKey(hKey);

	// ע�����뷨
	HKL hKL = ImmInstallIME(destPath.native_file_string().c_str(), WEASEL_IME_NAME L" (IME)");
	if (!hKL)
	{
		DWORD dwErr = GetLastError();
		WCHAR msg[100];
		wsprintf(msg, L"�]��ݔ�뷨�e�` ImmInstallIME: HKL=%x Err=%x", hKL, dwErr);
		if (!silent) MessageBox(NULL, msg, L"���bʧ��", MB_ICONERROR | MB_OK);
		return 1;
	}

	if (!silent) MessageBox(NULL, L"����ʹ��С�Ǻ��������� :)", L"���b���", MB_ICONINFORMATION | MB_OK);
	return 0;
}

int uninstall(bool silent)
{
	const WCHAR KL_KEY[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";
	const WCHAR PRELOAD_KEY[] = L"Keyboard Layout\\Preload";

	HKEY hKey;
	LSTATUS ret = RegOpenKey(HKEY_LOCAL_MACHINE, KL_KEY, &hKey);
	if (ret != ERROR_SUCCESS)
	{
		if (!silent) MessageBox(NULL, KL_KEY, L"ж�dʧ��", MB_ICONERROR | MB_OK);
		return 1;
	}

	for (int i = 0; true; ++i)
	{
		WCHAR subKey[16];
		ret = RegEnumKey(hKey, i, subKey, _countof(subKey));
		if (ret != ERROR_SUCCESS)
			break;

		// ���ļ��̲���?
		if (wcscmp(subKey + 4, L"0804") == 0 || wcscmp(subKey + 4, L"0404") == 0)
		{
			HKEY hSubKey;
			ret = RegOpenKey(hKey, subKey, &hSubKey);
			if (ret != ERROR_SUCCESS)
				continue;

			WCHAR imeFile[32];
			DWORD len = sizeof(imeFile);
			DWORD type = 0;
			ret = RegQueryValueEx(hSubKey, L"Ime File", NULL, &type, (LPBYTE)imeFile, &len);
			RegCloseKey(hSubKey);
			if (ret != ERROR_SUCCESS)
				continue;

			// С�Ǻ�?
			if (_wcsicmp(imeFile, WEASEL_IME_FILE) == 0)
			{
				DWORD value;
				swscanf_s(subKey, L"%x", &value);
				UnloadKeyboardLayout((HKL)value);

				RegDeleteKey(hKey, subKey);

				// �Ƴ�preload
				HKEY hPreloadKey;
				ret = RegOpenKey(HKEY_CURRENT_USER, PRELOAD_KEY, &hPreloadKey);
				if (ret != ERROR_SUCCESS)
					continue;
				vector<wstring> preloads;
				wstring number;
				for (size_t i = 1; true; ++i)
				{
					number = (boost::wformat(L"%1%") % i).str();
					DWORD type = 0;
					WCHAR value[32];
					DWORD len = sizeof(value);
					ret = RegQueryValueEx(hPreloadKey, number.c_str(), 0, &type, (LPBYTE)value, &len);
					if (ret != ERROR_SUCCESS)
					{
						if (i > preloads.size())
						{
							// ɾ�����һ��ע���ֵ
							number = (boost::wformat(L"%1%") % (i - 1)).str();
							RegDeleteValue(hPreloadKey, number.c_str());
						}
						break;
					}
					if (_wcsicmp(subKey, value) != 0)
					{
						preloads.push_back(value);
					}
				}
				// ��дpreloads
				for (size_t i = 0; i < preloads.size(); ++i)
				{
					number = (boost::wformat(L"%1%") % (i + 1)).str();
					RegSetValueEx(hPreloadKey, number.c_str(), 0, REG_SZ,
						          (const BYTE*)preloads[i].c_str(),
								  (preloads[i].length() + 1) * sizeof(WCHAR));
				}
				RegCloseKey(hPreloadKey);
			}
		}
	}

	RegCloseKey(hKey);

	// ���ע����Ϣ
	RegDeleteKey(HKEY_LOCAL_MACHINE, WEASEL_REG_KEY);
	RegDeleteKey(HKEY_LOCAL_MACHINE, RIME_REG_KEY);

	// ɾ���ļ�
	WCHAR path[MAX_PATH];
	GetSystemDirectory(path, _countof(path));
	wpath imePath = path;
	imePath /= WEASEL_IME_FILE;
	if (!delete_file(imePath.native_file_string()))
	{
		if (!silent) MessageBox(NULL, imePath.native_file_string().c_str(), L"ж�dʧ��", MB_ICONERROR | MB_OK);
		return 1;
	}
	if (GetSystemWow64Directory(path, _countof(path)))
	{
		wpath imePath = path;
		imePath /= WEASEL_IME_FILE;
		if (!delete_file(imePath.native_file_string()))
		{
			if (!silent) MessageBox(NULL, imePath.native_file_string().c_str(), L"ж�dʧ��", MB_ICONERROR | MB_OK);
			return 1;
		}
	}

	if (!silent) MessageBox(NULL, L"С�Ǻ� :)", L"ж�d���", MB_ICONINFORMATION | MB_OK);
	return 0;
}

bool has_installed() {
	WCHAR path[MAX_PATH];
	GetSystemDirectory(path, _countof(path));
	wpath imePath = path;
	imePath /= WEASEL_IME_FILE;
	return boost::filesystem::exists(imePath);
}
