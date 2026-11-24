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

#ifndef COMMON_FS_ISO_H
#define COMMON_FS_ISO_H

#include "common/str.h"

class AbstractFSNode;

namespace Common {

/**
 * If nativePath refers to an existing ISO 9660 image file (".iso") or an
 * internal subpath inside one ("path/to/disc.iso/DATA/FILE"), return a
 * virtual read-only AbstractFSNode backed directly by the ISO image.
 * Otherwise return nullptr so callers fall back to the host filesystem.
 */
AbstractFSNode *tryCreateIsoFilesystemNode(const String &nativePath);

} // End of namespace Common

#endif // COMMON_FS_ISO_H
