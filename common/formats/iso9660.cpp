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

#include "common/formats/iso9660.h"

#include "backends/fs/abstract-fs.h"
#include "backends/fs/fs-factory.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/mutex.h"
#include "common/system.h"
#include "common/ustr.h"

namespace Common {

namespace {

static const uint32 kLogicalSectorSize = 2048;
static const uint32 kFirstVolumeDescSector = 16;
static const int kMaxDirectoryDepth = 32;

struct GeometryCandidate {
	uint32 sectorSize;
	uint32 dataOffset;
};

static const GeometryCandidate kGeometryCandidates[] = {
	{ 2048, 0 },
	{ 2352, 16 },
	{ 2352, 24 },
	{ 2336, 8 }
};

static bool isJolietEscape(const byte *esc) {
	if (esc[0] != 0x25 || esc[1] != 0x2F)
		return false;
	return esc[2] == 0x40 || esc[2] == 0x43 || esc[2] == 0x45;
}

static String decodeIsoName(const byte *raw, uint32 len, bool joliet, bool isDir) {
	String result;
	if (joliet) {
		U32String u32;
		uint32 pairs = len / 2;
		for (uint32 i = 0; i < pairs; ++i) {
			uint16 ch = READ_BE_UINT16(raw + i * 2);
			if (ch == 0)
				break;
			u32 += (u32char_type_t)ch;
		}
		result = u32.encode(kUtf8);
	} else {
		result = String((const char *)raw, len);
	}

	// Strip ISO 9660 version suffix ";1"
	size_t semi = result.findLastOf(';');
	if (semi != String::npos)
		result.erase(semi);

	// Strip trailing dot on extensionless PVD file names
	if (!isDir && !result.empty() && result.lastChar() == '.')
		result.deleteLastChar();

	return result;
}

static String normalizeCacheKey(const String &path) {
	String key = path;
	for (uint i = 0; i < key.size(); ++i) {
		if (key[i] == '/')
			key.setChar('\\', i);
	}
	while (key.size() > 1 && key.lastChar() == '\\')
		key.deleteLastChar();
	key.toLowercase();
	return key;
}

class Iso9660FileStream : public SeekableReadStream {
public:
	Iso9660FileStream(SeekableReadStream *hostStream, uint32 sectorSize, uint32 dataOffset,
	                  const Array<Iso9660Extent> &extents, uint64 totalSize)
		: _hostStream(hostStream),
		  _sectorSize(sectorSize),
		  _dataOffset(dataOffset),
		  _extents(extents),
		  _totalSize(totalSize),
		  _pos(0),
		  _eos(false) {
	}

	~Iso9660FileStream() override {
		delete _hostStream;
	}

	bool eos() const override { return _eos; }
	bool err() const override { return _hostStream->err(); }
	void clearErr() override {
		_eos = false;
		_hostStream->clearErr();
	}

	int64 pos() const override { return (int64)_pos; }
	int64 size() const override { return (int64)_totalSize; }

	bool seek(int64 offset, int whence = SEEK_SET) override {
		int64 target = 0;
		switch (whence) {
		case SEEK_SET:
			target = offset;
			break;
		case SEEK_CUR:
			target = (int64)_pos + offset;
			break;
		case SEEK_END:
			target = (int64)_totalSize + offset;
			break;
		default:
			return false;
		}

		if (target < 0 || (uint64)target > _totalSize)
			return false;

		_pos = (uint64)target;
		_eos = false;
		return true;
	}

	uint32 read(void *dataPtr, uint32 dataSize) override {
		if (dataSize == 0)
			return 0;

		if (_pos >= _totalSize) {
			_eos = true;
			return 0;
		}

		uint64 remainingInFile = _totalSize - _pos;
		uint32 toRead = dataSize;
		if ((uint64)toRead > remainingInFile) {
			toRead = (uint32)remainingInFile;
		}

		byte *dst = (byte *)dataPtr;
		uint32 totalRead = 0;

		while (totalRead < toRead) {
			// Locate the extent covering _pos
			uint64 extBase = 0;
			const Iso9660Extent *curExt = nullptr;
			for (uint i = 0; i < _extents.size(); ++i) {
				if (_pos < extBase + _extents[i].size) {
					curExt = &_extents[i];
					break;
				}
				extBase += _extents[i].size;
			}
			if (!curExt) {
				_eos = true;
				break;
			}

			uint64 offsetInExt = _pos - extBase;
			uint64 availInExt = curExt->size - offsetInExt;
			uint32 chunk = toRead - totalRead;
			if ((uint64)chunk > availInExt)
				chunk = (uint32)availInExt;

			if (_sectorSize == kLogicalSectorSize && _dataOffset == 0) {
				// Fast contiguous read for standard 2048-byte ISO images
				int64 physOffset = (int64)curExt->lba * kLogicalSectorSize + (int64)offsetInExt;
				if (!_hostStream->seek(physOffset, SEEK_SET)) {
					break;
				}
				uint32 got = _hostStream->read(dst + totalRead, chunk);
				totalRead += got;
				_pos += got;
				if (got < chunk) {
					_eos = true;
					break;
				}
			} else {
				// Sector-by-sector read for raw CD images (2352 / 2336 bytes per sector)
				uint32 secIndex = (uint32)(offsetInExt / kLogicalSectorSize);
				uint32 secOff = (uint32)(offsetInExt % kLogicalSectorSize);
				uint32 secChunk = kLogicalSectorSize - secOff;
				if (secChunk > chunk)
					secChunk = chunk;

				int64 physOffset = (int64)(curExt->lba + secIndex) * _sectorSize + _dataOffset + secOff;
				if (!_hostStream->seek(physOffset, SEEK_SET)) {
					break;
				}
				uint32 got = _hostStream->read(dst + totalRead, secChunk);
				totalRead += got;
				_pos += got;
				if (got < secChunk) {
					_eos = true;
					break;
				}
			}
		}

		if (totalRead < dataSize)
			_eos = true;

		return totalRead;
	}

private:
	SeekableReadStream *_hostStream;
	uint32 _sectorSize;
	uint32 _dataOffset;
	Array<Iso9660Extent> _extents;
	uint64 _totalSize;
	uint64 _pos;
	bool _eos;
};

struct IsoCacheState {
	Mutex mutex;
	HashMap<String, SharedPtr<Iso9660Image> > images;
};

static IsoCacheState *g_isoCache = nullptr;

static IsoCacheState &getIsoCache() {
	if (!g_isoCache)
		g_isoCache = new IsoCacheState();
	return *g_isoCache;
}

} // End of anonymous namespace

Iso9660Entry::~Iso9660Entry() {
	for (uint i = 0; i < children.size(); ++i)
		delete children[i];
}

Iso9660Image::Iso9660Image(const String &hostPath)
	: _hostPath(hostPath),
	  _sectorSize(kLogicalSectorSize),
	  _dataOffset(0),
	  _isJoliet(false),
	  _root(nullptr) {
}

Iso9660Image::~Iso9660Image() {
	delete _root;
}

SeekableReadStream *Iso9660Image::openHostStream() const {
	if (!g_system || !g_system->getFilesystemFactory())
		return nullptr;

	ScopedPtr<AbstractFSNode> hostNode(g_system->getFilesystemFactory()->makeFileNodePath(_hostPath));
	if (!hostNode || !hostNode->exists() || hostNode->isDirectory())
		return nullptr;

	return hostNode->createReadStream();
}

bool Iso9660Image::detectGeometry(SeekableReadStream &stream) {
	int64 fileSize = stream.size();
	byte header[6];

	for (uint i = 0; i < ARRAYSIZE(kGeometryCandidates); ++i) {
		uint32 secSize = kGeometryCandidates[i].sectorSize;
		uint32 dataOff = kGeometryCandidates[i].dataOffset;
		int64 pvdOffset = (int64)kFirstVolumeDescSector * secSize + dataOff;

		if (pvdOffset + kLogicalSectorSize > fileSize)
			continue;

		if (!stream.seek(pvdOffset, SEEK_SET))
			continue;

		if (stream.read(header, sizeof(header)) != sizeof(header))
			continue;

		if (memcmp(header + 1, "CD001", 5) == 0 && (header[0] == 1 || header[0] == 2 || header[0] == 0)) {
			_sectorSize = secSize;
			_dataOffset = dataOff;
			return true;
		}
	}

	return false;
}

bool Iso9660Image::readLogicalSector(SeekableReadStream &stream, uint32 lba, byte *buf) const {
	int64 offset = (int64)lba * _sectorSize + _dataOffset;
	if (!stream.seek(offset, SEEK_SET))
		return false;
	return stream.read(buf, kLogicalSectorSize) == kLogicalSectorSize;
}

bool Iso9660Image::load() {
	ScopedPtr<SeekableReadStream> stream(openHostStream());
	if (!stream)
		return false;

	if (!detectGeometry(*stream))
		return false;

	byte sector[kLogicalSectorSize];
	bool hasPvd = false;
	bool hasJoliet = false;
	uint32 pvdRootLba = 0;
	uint32 pvdRootSize = 0;
	uint32 jolietRootLba = 0;
	uint32 jolietRootSize = 0;

	for (uint32 lba = kFirstVolumeDescSector; lba < kFirstVolumeDescSector + 64; ++lba) {
		if (!readLogicalSector(*stream, lba, sector))
			break;

		if (memcmp(sector + 1, "CD001", 5) != 0)
			break;

		byte vdType = sector[0];
		if (vdType == 255)
			break;

		if (vdType == 1 && !hasPvd) {
			hasPvd = true;
			pvdRootLba = READ_LE_UINT32(sector + 156 + 2);
			pvdRootSize = READ_LE_UINT32(sector + 156 + 10);

			String vol((const char *)(sector + 40), 32);
			vol.trim();
			_volumeId = vol;
		} else if (vdType == 2 && !hasJoliet && isJolietEscape(sector + 88)) {
			hasJoliet = true;
			jolietRootLba = READ_LE_UINT32(sector + 156 + 2);
			jolietRootSize = READ_LE_UINT32(sector + 156 + 10);
		}
	}

	if (!hasPvd && !hasJoliet)
		return false;

	_isJoliet = hasJoliet;
	uint32 rootLba = hasJoliet ? jolietRootLba : pvdRootLba;
	uint32 rootSize = hasJoliet ? jolietRootSize : pvdRootSize;

	_root = new Iso9660Entry();
	_root->isDirectory = true;

	if (!parseDirectory(*stream, rootLba, rootSize, _root, _isJoliet, 0)) {
		// Fall back to PVD if Joliet tree failed to parse
		if (hasJoliet && hasPvd) {
			delete _root;
			_root = new Iso9660Entry();
			_root->isDirectory = true;
			_isJoliet = false;
			if (!parseDirectory(*stream, pvdRootLba, pvdRootSize, _root, false, 0)) {
				delete _root;
				_root = nullptr;
				return false;
			}
		} else {
			delete _root;
			_root = nullptr;
			return false;
		}
	}

	debug(2, "Iso9660Image: Loaded '%s' (volume='%s', sector=%u+%u, joliet=%d, root entries=%u)",
		_hostPath.c_str(), _volumeId.c_str(), _sectorSize, _dataOffset, _isJoliet ? 1 : 0, _root->children.size());

	return true;
}

bool Iso9660Image::parseDirectory(SeekableReadStream &stream, uint32 lba, uint32 size,
                                  Iso9660Entry *dir, bool joliet, int depth) {
	if (depth > kMaxDirectoryDepth || size == 0)
		return true;

	uint32 numSectors = (size + kLogicalSectorSize - 1) / kLogicalSectorSize;
	Array<byte> dirBuf;
	dirBuf.resize(numSectors * kLogicalSectorSize);

	for (uint32 s = 0; s < numSectors; ++s) {
		if (!readLogicalSector(stream, lba + s, &dirBuf[s * kLogicalSectorSize]))
			return false;
	}

	uint32 pos = 0;
	while (pos < size) {
		byte recLen = dirBuf[pos];
		if (recLen == 0) {
			uint32 nextSector = ((pos / kLogicalSectorSize) + 1) * kLogicalSectorSize;
			if (nextSector <= pos)
				break;
			pos = nextSector;
			continue;
		}

		if (pos + recLen > dirBuf.size() || recLen < 34)
			break;

		const byte *rec = &dirBuf[pos];
		uint32 entryLba = READ_LE_UINT32(rec + 2);
		uint32 entrySize = READ_LE_UINT32(rec + 10);
		byte flags = rec[25];
		byte nameLen = rec[32];

		pos += recLen;

		if (33 + nameLen > recLen)
			continue;

		// Skip "." (0x00) and ".." (0x01)
		if (nameLen == 1 && (rec[33] == 0x00 || rec[33] == 0x01))
			continue;

		bool isDir = (flags & 0x02) != 0;
		String name = decodeIsoName(rec + 33, nameLen, joliet, isDir);
		if (name.empty())
			continue;

		Iso9660Entry *existing = dir->childMap.getValOrDefault(name, nullptr);
		if (existing && !isDir && !existing->isDirectory) {
			// Multi-extent file continuation
			Iso9660Extent ext;
			ext.lba = entryLba;
			ext.size = entrySize;
			existing->extents.push_back(ext);
			existing->totalSize += entrySize;
			continue;
		}

		if (existing)
			continue;

		Iso9660Entry *entry = new Iso9660Entry();
		entry->name = name;
		entry->lowerName = name;
		entry->lowerName.toLowercase();
		entry->relativePath = dir->relativePath.empty()
			? Path(name, Path::kNoSeparator)
			: dir->relativePath.appendComponent(name);
		entry->isDirectory = isDir;

		if (!isDir) {
			Iso9660Extent ext;
			ext.lba = entryLba;
			ext.size = entrySize;
			entry->extents.push_back(ext);
			entry->totalSize = entrySize;
		}

		dir->children.push_back(entry);
		dir->childMap[entry->name] = entry;

		if (isDir && entryLba != lba) {
			parseDirectory(stream, entryLba, entrySize, entry, joliet, depth + 1);
		}
	}

	return true;
}

const Iso9660Entry *Iso9660Image::findChild(const Iso9660Entry *dir, const String &name) const {
	if (!dir || !dir->isDirectory)
		return nullptr;
	return dir->childMap.getValOrDefault(name, nullptr);
}

const Iso9660Entry *Iso9660Image::findEntry(const String &internalPath) const {
	if (!_root)
		return nullptr;

	const Iso9660Entry *cur = _root;
	uint32 start = 0;
	uint32 len = internalPath.size();

	while (start < len && (internalPath[start] == '/' || internalPath[start] == '\\'))
		++start;

	while (start < len && cur) {
		uint32 end = start;
		while (end < len && internalPath[end] != '/' && internalPath[end] != '\\')
			++end;

		if (end > start) {
			String component(internalPath.c_str() + start, end - start);
			if (component != ".") {
				cur = findChild(cur, component);
			}
		}

		start = end + 1;
	}

	return cur;
}

SeekableReadStream *Iso9660Image::createReadStream(const Iso9660Entry *entry) const {
	if (!entry || entry->isDirectory)
		return nullptr;

	SeekableReadStream *hostStream = openHostStream();
	if (!hostStream)
		return nullptr;

	return new Iso9660FileStream(hostStream, _sectorSize, _dataOffset, entry->extents, entry->totalSize);
}

SharedPtr<Iso9660Image> Iso9660Image::open(const String &hostPath) {
	SharedPtr<Iso9660Image> img(new Iso9660Image(hostPath));
	if (!img->load())
		return SharedPtr<Iso9660Image>();
	return img;
}

SharedPtr<Iso9660Image> Iso9660Image::openCached(const String &hostPath) {
	IsoCacheState &cache = getIsoCache();
	String key = normalizeCacheKey(hostPath);

	{
		StackLock lock(cache.mutex);
		HashMap<String, SharedPtr<Iso9660Image> >::iterator it = cache.images.find(key);
		if (it != cache.images.end())
			return it->_value;
	}

	SharedPtr<Iso9660Image> img = open(hostPath);
	if (!img)
		return img;

	{
		StackLock lock(cache.mutex);
		cache.images[key] = img;
	}
	return img;
}

void Iso9660Image::evictCached(const String &hostPath) {
	if (!g_isoCache)
		return;
	StackLock lock(g_isoCache->mutex);
	g_isoCache->images.erase(normalizeCacheKey(hostPath));
}

void Iso9660Image::clearCache() {
	if (!g_isoCache)
		return;
	{
		StackLock lock(g_isoCache->mutex);
		g_isoCache->images.clear();
	}
	delete g_isoCache;
	g_isoCache = nullptr;
}

Iso9660Archive::Iso9660Archive(const SharedPtr<Iso9660Image> &image)
	: _image(image) {
}

Iso9660Archive *Iso9660Archive::create(const String &hostPath) {
	SharedPtr<Iso9660Image> img = Iso9660Image::openCached(hostPath);
	if (!img)
		return nullptr;
	return new Iso9660Archive(img);
}

bool Iso9660Archive::hasFile(const Path &path) const {
	if (!_image)
		return false;
	const Iso9660Entry *entry = _image->findEntry(path.toString('/'));
	return entry != nullptr && !entry->isDirectory;
}

void Iso9660Archive::collectMembers(const Iso9660Entry *dir, ArchiveMemberList &list, int &count) const {
	if (!dir)
		return;
	for (uint i = 0; i < dir->children.size(); ++i) {
		const Iso9660Entry *child = dir->children[i];
		if (child->isDirectory) {
			collectMembers(child, list, count);
		} else {
			list.push_back(ArchiveMemberPtr(new GenericArchiveMember(child->relativePath, *this)));
			++count;
		}
	}
}

int Iso9660Archive::listMembers(ArchiveMemberList &list) const {
	if (!_image || !_image->getRoot())
		return 0;
	int count = 0;
	collectMembers(_image->getRoot(), list, count);
	return count;
}

const ArchiveMemberPtr Iso9660Archive::getMember(const Path &path) const {
	if (!_image)
		return ArchiveMemberPtr();
	const Iso9660Entry *entry = _image->findEntry(path.toString('/'));
	if (!entry || entry->isDirectory)
		return ArchiveMemberPtr();
	return ArchiveMemberPtr(new GenericArchiveMember(entry->relativePath, *this));
}

SeekableReadStream *Iso9660Archive::createReadStreamForMember(const Path &path) const {
	if (!_image)
		return nullptr;
	const Iso9660Entry *entry = _image->findEntry(path.toString('/'));
	if (!entry || entry->isDirectory)
		return nullptr;
	return _image->createReadStream(entry);
}

} // End of namespace Common
