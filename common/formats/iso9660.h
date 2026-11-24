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

#ifndef COMMON_FORMATS_ISO9660_H
#define COMMON_FORMATS_ISO9660_H

#include "common/archive.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/path.h"
#include "common/ptr.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/types.h"

namespace Common {

struct Iso9660Extent {
	uint32 lba;
	uint32 size;
};

struct Iso9660Entry {
	String name;
	String lowerName;
	Path relativePath;
	bool isDirectory;
	uint64 totalSize;
	Array<Iso9660Extent> extents;
	Array<Iso9660Entry *> children;
	HashMap<String, Iso9660Entry *, IgnoreCase_Hash, IgnoreCase_EqualTo> childMap;

	Iso9660Entry() : isDirectory(false), totalSize(0) {}
	~Iso9660Entry();
};

class Iso9660Image {
public:
	~Iso9660Image();

	static SharedPtr<Iso9660Image> open(const String &hostPath);
	static SharedPtr<Iso9660Image> openCached(const String &hostPath);
	static void evictCached(const String &hostPath);
	static void clearCache();

	const String &getHostPath() const { return _hostPath; }
	const String &getVolumeId() const { return _volumeId; }
	bool isJoliet() const { return _isJoliet; }
	uint32 getSectorSize() const { return _sectorSize; }
	uint32 getDataOffset() const { return _dataOffset; }

	const Iso9660Entry *getRoot() const { return _root; }
	const Iso9660Entry *findEntry(const String &internalPath) const;
	const Iso9660Entry *findChild(const Iso9660Entry *dir, const String &name) const;

	SeekableReadStream *createReadStream(const Iso9660Entry *entry) const;

private:
	Iso9660Image(const String &hostPath);

	bool load();
	bool detectGeometry(SeekableReadStream &stream);
	bool readLogicalSector(SeekableReadStream &stream, uint32 lba, byte *buf) const;
	bool parseDirectory(SeekableReadStream &stream, uint32 lba, uint32 size, Iso9660Entry *dir, bool joliet, int depth);
	SeekableReadStream *openHostStream() const;

	String _hostPath;
	String _volumeId;
	uint32 _sectorSize;
	uint32 _dataOffset;
	bool _isJoliet;
	Iso9660Entry *_root;
};

class Iso9660Archive : public Archive {
public:
	explicit Iso9660Archive(const SharedPtr<Iso9660Image> &image);
	~Iso9660Archive() override {}

	static Iso9660Archive *create(const String &hostPath);

	bool hasFile(const Path &path) const override;
	int listMembers(ArchiveMemberList &list) const override;
	const ArchiveMemberPtr getMember(const Path &path) const override;
	SeekableReadStream *createReadStreamForMember(const Path &path) const override;

private:
	void collectMembers(const Iso9660Entry *dir, ArchiveMemberList &list, int &count) const;

	SharedPtr<Iso9660Image> _image;
};

} // End of namespace Common

#endif // COMMON_FORMATS_ISO9660_H
