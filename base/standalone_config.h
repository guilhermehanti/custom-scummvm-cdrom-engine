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

#ifndef BASE_STANDALONE_CONFIG_H
#define BASE_STANDALONE_CONFIG_H

// Default placeholder configuration for standalone builds.
// Out-of-tree standalone builds generate a game-specific base/standalone_config.h
// in the build directory, which takes precedence via -I. include ordering.
#ifndef STANDALONE_APP_ID
#define STANDALONE_APP_ID "scummvm-standalone"
#endif

#ifndef STANDALONE_DISPLAY_NAME
#define STANDALONE_DISPLAY_NAME "ScummVM Standalone"
#endif

#ifndef STANDALONE_ENGINE_ID
#define STANDALONE_ENGINE_ID ""
#endif

#ifndef STANDALONE_GAME_ID
#define STANDALONE_GAME_ID ""
#endif

#ifndef STANDALONE_EXPECTED_LANGUAGE
#define STANDALONE_EXPECTED_LANGUAGE ""
#endif

#ifndef STANDALONE_EXPECTED_PLATFORM
#define STANDALONE_EXPECTED_PLATFORM ""
#endif

#ifndef STANDALONE_EXPECTED_EXTRA
#define STANDALONE_EXPECTED_EXTRA ""
#endif

#ifndef STANDALONE_TARGET_NAME
#define STANDALONE_TARGET_NAME "standalone-game"
#endif

#ifndef STANDALONE_RUNTIME_ICON_FILE
#define STANDALONE_RUNTIME_ICON_FILE ""
#endif

#endif // BASE_STANDALONE_CONFIG_H
