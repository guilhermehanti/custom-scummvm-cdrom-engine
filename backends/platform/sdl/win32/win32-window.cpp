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

// Disable symbol overrides so that we can use system headers.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#ifdef WIN32

#include "backends/platform/sdl/win32/win32-window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void SdlWindow_Win32::setupIcon() {
	HMODULE handle = GetModuleHandle(nullptr);
	HICON icoBig = (HICON)LoadImage(handle, MAKEINTRESOURCE(1001 /* IDI_ICON */), IMAGE_ICON,
		0, 0, LR_DEFAULTSIZE | LR_SHARED);
	HICON icoSmall = (HICON)LoadImage(handle, MAKEINTRESOURCE(1001 /* IDI_ICON */), IMAGE_ICON,
		16, 16, LR_SHARED);
	if (!icoBig)
		icoBig = LoadIcon(handle, MAKEINTRESOURCE(1001 /* IDI_ICON */));
	if (icoBig || icoSmall) {
		HWND hwnd = getHwnd();
		if (hwnd) {
			SetLastError(ERROR_SUCCESS);
			if (icoBig) {
				SetClassLongPtr(hwnd, GCLP_HICON, (ULONG_PTR)icoBig);
				SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icoBig);
			}
			if (icoSmall) {
				SetClassLongPtr(hwnd, GCLP_HICONSM, (ULONG_PTR)icoSmall);
				SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icoSmall);
			}
			return;
		}
	}

#ifndef SCUMMVM_STANDALONE
	// If no icon has been set, fallback to default path
	SdlWindow::setupIcon();
#endif
}

HWND SdlWindow_Win32::getHwnd() {
	SDL_SysWMinfo wminfo;
	if (getSDLWMInformation(&wminfo)) {
#if SDL_VERSION_ATLEAST(2, 0, 0)
		return wminfo.info.win.window;
#else
		return wminfo.window;
#endif
	}
	return nullptr;
}

#endif
