/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Disable symbol overrides for Win32 system APIs
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#define WIN32_LEAN_AND_MEAN
#include <sdkddkver.h>
#if !defined(_WIN32_IE) || (_WIN32_IE < 0x500)
 // required for SHGetSpecialFolderPath and SHGFP_TYPE_CURRENT in shlobj.h
#undef _WIN32_IE
#define _WIN32_IE 0x500
#endif
#if defined(SCUMMVM_STANDALONE)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x500)
 // required for VER_MAJORVERSION, VER_MINORVERSION and VER_GREATER_EQUAL in winnt.h
 // set to Windows 2000 which is the minimum needed for these constants
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x500
#endif
#include <windows.h>
#include <shellapi.h> // for CommandLineToArgvW()
#include <shlobj.h>
#include <tchar.h>

#include "common/ptr.h"
#include "common/scummsys.h"
#include "common/textconsole.h"
#include "backends/platform/sdl/win32/win32_wrapper.h"
#ifdef SCUMMVM_STANDALONE
#include "base/standalone_config.h"
#include "common/array.h"
#include "common/endian.h"
#include "common/formats/iso9660.h"
#include "common/formats/winexe.h"
#include "common/stream.h"
#endif

// VerSetConditionMask, VerifyVersionInfo and SHGetFolderPath didn't appear until Windows 2000,
// so we need to check for them at runtime
ULONGLONG VerSetConditionMaskFunc(ULONGLONG dwlConditionMask, DWORD dwTypeMask, BYTE dwConditionMask) {
	typedef ULONGLONG(WINAPI *VerSetConditionMaskFunction)(ULONGLONG conditionMask, DWORD typeMask, BYTE conditionOperator);

	VerSetConditionMaskFunction verSetConditionMask = (VerSetConditionMaskFunction)(void *)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "VerSetConditionMask");
	if (verSetConditionMask == nullptr)
		return 0;

	return verSetConditionMask(dwlConditionMask, dwTypeMask, dwConditionMask);
}

BOOL VerifyVersionInfoFunc(LPOSVERSIONINFOEXA lpVersionInformation, DWORD dwTypeMask, DWORDLONG dwlConditionMask) {
	typedef BOOL(WINAPI *VerifyVersionInfoFunction)(LPOSVERSIONINFOEXA versionInformation, DWORD typeMask, DWORDLONG conditionMask);

	VerifyVersionInfoFunction verifyVersionInfo = (VerifyVersionInfoFunction)(void *)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "VerifyVersionInfoA");
	if (verifyVersionInfo == nullptr)
		return FALSE;

	return verifyVersionInfo(lpVersionInformation, dwTypeMask, dwlConditionMask);
}

HRESULT SHGetFolderPathFunc(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPTSTR pszPath) {
	typedef HRESULT (WINAPI *SHGetFolderPathFunc)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPTSTR pszPath);

	SHGetFolderPathFunc pSHGetFolderPath = (SHGetFolderPathFunc)(void *)GetProcAddress(GetModuleHandle(TEXT("shell32.dll")), 
#ifndef UNICODE
		"SHGetFolderPathA"
#else
		"SHGetFolderPathW"
#endif
	);
	if (pSHGetFolderPath)
		return pSHGetFolderPath(hwnd, csidl, hToken, dwFlags, pszPath);

	typedef BOOL (WINAPI *SHGetSpecialFolderPathFunc)(HWND hwnd, LPTSTR pszPath, int csidl, BOOL fCreate);

	SHGetSpecialFolderPathFunc pSHGetSpecialFolderPath = (SHGetSpecialFolderPathFunc)(void *)GetProcAddress(GetModuleHandle(TEXT("shell32.dll")),
#ifndef UNICODE
		"SHGetSpecialFolderPathA"
#else
		"SHGetSpecialFolderPathW"
#endif
	);
	if (pSHGetSpecialFolderPath)
		return pSHGetSpecialFolderPath(hwnd, pszPath, csidl & ~CSIDL_FLAG_MASK, csidl & CSIDL_FLAG_CREATE) ? S_OK : E_FAIL;

	return E_NOTIMPL;
}

namespace Win32 {

bool getApplicationDataDirectory(TCHAR *applicationDataDirectory) {
	HRESULT hr = SHGetFolderPathFunc(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, applicationDataDirectory);
	if (hr != S_OK) {
		if (hr != E_NOTIMPL) {
			warning("Unable to locate application data directory");
		}
		return false;
	}

	_tcscat(applicationDataDirectory, TEXT("\\ScummVM"));
	if (!CreateDirectory(applicationDataDirectory, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			error("Cannot create ScummVM application data folder");
		}
	}

	return true;
}

void getProcessDirectory(TCHAR *processDirectory, DWORD size) {
	GetModuleFileName(NULL, processDirectory, size);
	processDirectory[size - 1] = '\0'; // termination not guaranteed

	// remove executable and final path separator
	TCHAR *lastSeparator = _tcsrchr(processDirectory, '\\');
	if (lastSeparator != NULL) {
		*lastSeparator = '\0';
	}
}

bool confirmWindowsVersion(int majorVersion, int minorVersion) {
	OSVERSIONINFOEXA versionInfo;
	DWORDLONG conditionMask = 0;

	ZeroMemory(&versionInfo, sizeof(OSVERSIONINFOEXA));
	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
	versionInfo.dwMajorVersion = majorVersion;
	versionInfo.dwMinorVersion = minorVersion;

	conditionMask = VerSetConditionMaskFunc(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	conditionMask = VerSetConditionMaskFunc(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

	return VerifyVersionInfoFunc(&versionInfo, VER_MAJORVERSION | VER_MINORVERSION, conditionMask);
}

// for using ScopedPtr with malloc/free
template <typename T>
struct Freer {
	inline void operator()(T *object) {
		free(object);
	}
};

bool moveFile(const Common::String &src, const Common::String &dst) {
	Common::ScopedPtr<TCHAR, Freer<TCHAR>> tSrc(stringToTchar(src));
	Common::ScopedPtr<TCHAR, Freer<TCHAR>> tDst(stringToTchar(dst));

	if (MoveFileEx(tSrc.get(), tDst.get(), MOVEFILE_REPLACE_EXISTING)) {
		return true;
	}

	// MoveFileEx may not be supported on the platform (Win9x)
	if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) {
		// Fall back to deleting the destination before using MoveFile.
		// MoveFile requires that the destination not already exist.
		DeleteFile(tDst.get());
		return MoveFile(tSrc.get(), tDst.get());
	}

	return false;
}

bool isDriveCD(char driveLetter) {
	TCHAR drivePath[] = TEXT("x:\\");
	drivePath[0] = (TCHAR)driveLetter;

	return (GetDriveType(drivePath) == DRIVE_CDROM);
}

wchar_t *ansiToUnicode(const char *s) {
#ifndef UNICODE
	uint codePage = CP_ACP;
#else
	uint codePage = CP_UTF8;
#endif
	DWORD size = MultiByteToWideChar(codePage, 0, s, -1, nullptr, 0);

	if (size > 0) {
		LPWSTR result = (LPWSTR)calloc(size, sizeof(WCHAR));
		if (MultiByteToWideChar(codePage, 0, s, -1, result, size) != 0)
			return result;
	}

	return nullptr;
}

char *unicodeToAnsi(const wchar_t *s) {
#ifndef UNICODE
	uint codePage = CP_ACP;
#else
	uint codePage = CP_UTF8;
#endif
	DWORD size = WideCharToMultiByte(codePage, 0, s, -1, nullptr, 0, nullptr, nullptr);

	if (size > 0) {
		char *result = (char *)calloc(size, sizeof(char));
		if (WideCharToMultiByte(codePage, 0, s, -1, result, size, nullptr, nullptr) != 0)
			return result;
	}

	return nullptr;
}

TCHAR *stringToTchar(const Common::String& s) {
#ifndef UNICODE
	char *t = (char *)malloc(s.size() + 1);
	Common::strcpy_s(t, s.size() + 1, s.c_str());
	return t;
#else
	return ansiToUnicode(s.c_str());
#endif
}

Common::String tcharToString(const TCHAR *t) {
#ifndef UNICODE
	return t;
#else
	char *utf8 = unicodeToAnsi(t);
	Common::String s = utf8;
	free(utf8);
	return s;
#endif
}

#ifdef UNICODE
char **getArgvUtf8(int *argc) {
	// get command line arguments in wide-character
	LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), argc);

	// convert each argument to utf8
	char **argv = (char **)malloc((*argc + 1) * sizeof(char *));
	for (int i = 0; i < *argc; ++i) {
		argv[i] = Win32::unicodeToAnsi(wargv[i]);
	}
	argv[*argc] = nullptr; // null terminated array

	LocalFree(wargv);
	return argv;
}

void freeArgvUtf8(int argc, char **argv) {
	for (int i = 0; i < argc; ++i) {
		free(argv[i]);
	}
	free(argv);
}
#endif

#ifdef SCUMMVM_STANDALONE

static bool isDirectoryWritableWin32(const TCHAR *dirPath) {
	CreateDirectory(dirPath, nullptr);
	DWORD attrs = GetFileAttributes(dirPath);
	if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
		return false;

	TCHAR probeFile[MAX_PATH];
	_tcsncpy(probeFile, dirPath, MAX_PATH - 20);
	probeFile[MAX_PATH - 20] = '\0';
	_tcscat(probeFile, TEXT("\\.write_test"));

	HANDLE h = CreateFile(probeFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	CloseHandle(h);
	return true;
}

bool getStandaloneDataDirectory(TCHAR *dataDirectory, DWORD size) {
	if (!dataDirectory || size < 32)
		return false;

	Common::String folderSuffix = Common::String(STANDALONE_APP_ID) + "-data";
	TCHAR *tSuffix = stringToTchar(folderSuffix);

	// Prefer portable folder next to the executable unless running inside Program Files or read-only location.
	TCHAR exeDir[MAX_PATH];
	getProcessDirectory(exeDir, MAX_PATH);

	bool inProgramFiles = false;
	TCHAR programFiles[MAX_PATH];
	if (SHGetFolderPathFunc(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, programFiles) == S_OK) {
		if (_tcsnicmp(exeDir, programFiles, _tcslen(programFiles)) == 0)
			inProgramFiles = true;
	}

	if (!inProgramFiles) {
		TCHAR portableDir[MAX_PATH];
		_tcsncpy(portableDir, exeDir, MAX_PATH - 1);
		portableDir[MAX_PATH - 1] = '\0';
		_tcscat(portableDir, TEXT("\\"));
		_tcscat(portableDir, tSuffix);

		if (isDirectoryWritableWin32(portableDir)) {
			_tcsncpy(dataDirectory, portableDir, size - 1);
			dataDirectory[size - 1] = '\0';
			free(tSuffix);
			return true;
		}
	}

	free(tSuffix);

	// Fallback to %APPDATA%\ScummVM-Standalone\<appId>
	TCHAR appData[MAX_PATH];
	HRESULT hr = SHGetFolderPathFunc(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData);
	if (hr != S_OK)
		return false;

	_tcscat(appData, TEXT("\\ScummVM-Standalone"));
	CreateDirectory(appData, nullptr);

	TCHAR *tAppId = stringToTchar(STANDALONE_APP_ID);
	_tcscat(appData, TEXT("\\"));
	_tcscat(appData, tAppId);
	free(tAppId);
	CreateDirectory(appData, nullptr);

	_tcsncpy(dataDirectory, appData, size - 1);
	dataDirectory[size - 1] = '\0';
	return true;
}

bool showIsoFileDialog(HWND owner, const Common::U32String &title, const Common::Path &initialDir, Common::Path &selectedPath) {
	HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool didInit = SUCCEEDED(hrInit);

	IFileOpenDialog *dialog = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		IID_IFileOpenDialog, reinterpret_cast<void **>(&dialog));
	if (FAILED(hr) || !dialog) {
		if (didInit)
			CoUninitialize();
		return false;
	}

	DWORD dwOptions = 0;
	if (SUCCEEDED(dialog->GetOptions(&dwOptions))) {
		dwOptions |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
		dialog->SetOptions(dwOptions);
	}

	static const COMDLG_FILTERSPEC kFilters[] = {
		{ L"Imagens ISO (*.iso)", L"*.iso" },
		{ L"Todos os arquivos (*.*)", L"*.*" }
	};
	dialog->SetFileTypes(ARRAYSIZE(kFilters), kFilters);
	dialog->SetFileTypeIndex(1);
	dialog->SetDefaultExtension(L"iso");

	LPWSTR wTitle = (LPWSTR)title.encodeUTF16Native();
	if (wTitle) {
		dialog->SetTitle(wTitle);
		free(wTitle);
	}

	if (!initialDir.empty()) {
		LPWSTR wInit = ansiToUnicode(initialDir.toString(Common::Path::kNativeSeparator).c_str());
		if (wInit) {
			typedef HRESULT(WINAPI *SHCreateItemFunc)(PCWSTR, IBindCtx *, REFIID, void **);
			SHCreateItemFunc fn = (SHCreateItemFunc)(void *)GetProcAddress(GetModuleHandle(TEXT("shell32.dll")), "SHCreateItemFromParsingName");
			if (fn) {
				IShellItem *folder = nullptr;
				if (SUCCEEDED(fn(wInit, nullptr, IID_IShellItem, reinterpret_cast<void **>(&folder))) && folder) {
					dialog->SetDefaultFolder(folder);
					folder->Release();
				}
			}
			free(wInit);
		}
	}

	bool ok = false;
	hr = dialog->Show(owner);
	if (SUCCEEDED(hr)) {
		IShellItem *item = nullptr;
		if (SUCCEEDED(dialog->GetResult(&item)) && item) {
			LPWSTR wPath = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wPath)) && wPath) {
				char *utf8 = unicodeToAnsi(wPath);
				if (utf8) {
					selectedPath = Common::Path(utf8, Common::Path::kNativeSeparator);
					free(utf8);
					ok = true;
				}
				CoTaskMemFree(wPath);
			}
			item->Release();
		}
	}

	dialog->Release();
	if (didInit)
		CoUninitialize();
	return ok;
}

static HICON createHIconFromIcoData(const byte *data, uint32 size, int desiredSize) {
	if (size < 22)
		return nullptr;
	uint16 reserved = READ_LE_UINT16(data);
	uint16 type = READ_LE_UINT16(data + 2);
	uint16 count = READ_LE_UINT16(data + 4);
	if (reserved != 0 || type != 1 || count == 0)
		return nullptr;

	int bestIdx = -1;
	int bestScore = -1;
	for (uint16 i = 0; i < count; ++i) {
		uint32 entryOff = 6 + i * 16;
		if (entryOff + 16 > size)
			break;
		int w = data[entryOff] == 0 ? 256 : data[entryOff];
		int bpp = READ_LE_UINT16(data + entryOff + 6);
		uint32 imgSize = READ_LE_UINT32(data + entryOff + 8);
		uint32 imgOff = READ_LE_UINT32(data + entryOff + 12);
		if (imgOff + imgSize > size || imgSize == 0)
			continue;
		int score = (w == desiredSize ? 1000 : (256 - abs(w - desiredSize))) + bpp;
		if (score > bestScore) {
			bestScore = score;
			bestIdx = i;
		}
	}

	if (bestIdx < 0)
		return nullptr;

	uint32 entryOff = 6 + bestIdx * 16;
	uint32 imgSize = READ_LE_UINT32(data + entryOff + 8);
	uint32 imgOff = READ_LE_UINT32(data + entryOff + 12);
	return CreateIconFromResourceEx(const_cast<byte *>(data + imgOff), imgSize, TRUE, 0x00030000, desiredSize, desiredSize, LR_DEFAULTCOLOR);
}

bool applyWindowIconFromIso(HWND hwnd, const Common::String &isoPath, const Common::String &iconRelPath) {
	if (!hwnd || isoPath.empty() || iconRelPath.empty())
		return false;

	Common::SharedPtr<Common::Iso9660Image> img = Common::Iso9660Image::openCached(isoPath);
	if (!img)
		return false;

	const Common::Iso9660Entry *entry = img->findEntry(iconRelPath);
	if (!entry || entry->isDirectory)
		return false;

	Common::ScopedPtr<Common::SeekableReadStream> stream(img->createReadStream(entry));
	if (!stream)
		return false;

	HICON hBig = nullptr;
	HICON hSmall = nullptr;

	if (iconRelPath.hasSuffixIgnoreCase(".ico")) {
		int64 sz = stream->size();
		if (sz > 0 && sz <= 1024 * 1024) {
			Common::Array<byte> buf((uint32)sz);
			if (stream->read(buf.data(), (uint32)sz) == (uint32)sz) {
				hBig = createHIconFromIcoData(buf.data(), (uint32)sz, GetSystemMetrics(SM_CXICON));
				hSmall = createHIconFromIcoData(buf.data(), (uint32)sz, GetSystemMetrics(SM_CXSMICON));
			}
		}
	} else {
		Common::ScopedPtr<Common::WinResources> res(Common::WinResources::createFromEXE(stream.get()));
		if (res) {
			Common::Array<Common::WinResourceID> iconIds = res->getIDList(Common::kWinIcon);
			for (uint i = 0; i < iconIds.size(); ++i) {
				Common::ScopedPtr<Common::SeekableReadStream> iconStream(res->getResource(Common::kWinIcon, iconIds[i]));
				if (!iconStream)
					continue;
				int64 sz = iconStream->size();
				if (sz <= 0 || sz > 1024 * 1024)
					continue;
				Common::Array<byte> buf((uint32)sz);
				if (iconStream->read(buf.data(), (uint32)sz) != (uint32)sz)
					continue;
				if (!hBig)
					hBig = CreateIconFromResourceEx(buf.data(), (uint32)sz, TRUE, 0x00030000, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
				if (!hSmall)
					hSmall = CreateIconFromResourceEx(buf.data(), (uint32)sz, TRUE, 0x00030000, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
				if (hBig && hSmall)
					break;
			}
		}
	}

	if (!hBig && !hSmall)
		return false;

	if (hBig)
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hBig);
	if (hSmall)
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);
	return true;
}

#endif // SCUMMVM_STANDALONE

} // End of namespace Win32
