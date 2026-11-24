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

#ifndef BASE_STANDALONE_H
#define BASE_STANDALONE_H

#ifdef SCUMMVM_STANDALONE

#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/path.h"
#include "common/str.h"
#include "common/ustr.h"
#include "engines/game.h"

namespace Standalone {

struct GameConfig {
	const char *appId;
	const char *displayName;
	const char *engineId;
	const char *gameId;
	const char *expectedLanguage;
	const char *expectedPlatform;
	const char *expectedExtra;
	const char *targetName;
	const char *runtimeIconFile;
};

const GameConfig &getConfig();

void initializeDefaults();

bool validateIso(const Common::Path &isoPath, DetectedGame &matchedGame, Common::U32String &errorMessage);

void configureTargetDomain(const Common::Path &isoPath, const DetectedGame &matchedGame);

bool ensureGameReady(const Common::StringMap &cliSettings);

bool selectNewIsoInteractive(bool isReselect);

void openOptionsDialog();

void applyWindowBranding();

void requestRestart();
bool consumeRestartRequested();

int runValidateIsoCommand(const Common::String &isoPathStr);

} // End of namespace Standalone

#endif // SCUMMVM_STANDALONE

#endif // BASE_STANDALONE_H
