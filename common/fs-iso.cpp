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

#include "common/fs-iso.h"

#include "backends/fs/abstract-fs.h"
#include "common/formats/iso9660.h"
#include "common/path.h"
#include "common/ptr.h"

namespace Common {

namespace {

static bool isPathSep(char c) {
	return c == '/' || c == '\\';
}

static char toLowerAscii(char c) {
	return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

static bool splitIsoPath(const String &nativePath, String &isoFilePath, String &internalPath) {
	const uint32 len = nativePath.size();
	if (len < 4)
		return false;

	for (uint32 i = 0; i + 4 <= len; ++i) {
		if (nativePath[i] == '.' &&
		    toLowerAscii(nativePath[i + 1]) == 'i' &&
		    toLowerAscii(nativePath[i + 2]) == 's' &&
		    toLowerAscii(nativePath[i + 3]) == 'o') {
			uint32 end = i + 4;
			if (end == len || isPathSep(nativePath[end])) {
				isoFilePath = String(nativePath.c_str(), end);
				uint32 subStart = end;
				while (subStart < len && isPathSep(nativePath[subStart]))
					++subStart;
				internalPath = (subStart < len) ? String(nativePath.c_str() + subStart) : String();
				while (!internalPath.empty() && isPathSep(internalPath.lastChar()))
					internalPath.deleteLastChar();
				return true;
			}
		}
	}

	return false;
}

static String joinNativePath(const String &isoFilePath, const String &internalPath) {
	if (internalPath.empty())
		return isoFilePath;

	String res = isoFilePath;
	char sep = Path::kNativeSeparator;
	if (!res.empty() && !isPathSep(res.lastChar()))
		res += sep;

	for (uint i = 0; i < internalPath.size(); ++i) {
		char c = internalPath[i];
		res += isPathSep(c) ? sep : c;
	}
	return res;
}

class IsoFilesystemNode final : public AbstractFSNode {
public:
	IsoFilesystemNode(const SharedPtr<Iso9660Image> &image, const String &isoFilePath,
	                  const String &internalPath, const Iso9660Entry *entry)
		: _image(image),
		  _isoFilePath(isoFilePath),
		  _internalPath(internalPath),
		  _entry(entry) {
		_fullPath = joinNativePath(isoFilePath, internalPath);
		if (_entry && !_entry->name.empty()) {
			_name = _entry->name;
		} else if (!internalPath.empty()) {
			const char *last = lastPathComponent(internalPath, '/');
			const char *lastBack = lastPathComponent(internalPath, '\\');
			if (lastBack > last)
				last = lastBack;
			_name = last;
		} else {
			const char *last = lastPathComponent(isoFilePath, Path::kNativeSeparator);
			_name = last;
		}
	}

	bool exists() const override { return _entry != nullptr; }
	bool isDirectory() const override { return _entry != nullptr && _entry->isDirectory; }
	bool isReadable() const override { return _entry != nullptr; }
	bool isWritable() const override { return false; }

	U32String getDisplayName() const override { return U32String(_name, kUtf8); }
	String getName() const override { return _name; }
	String getPath() const override { return _fullPath; }

	AbstractFSNode *getChild(const String &n) const override {
		if (!_entry || !_entry->isDirectory)
			return nullptr;

		const Iso9660Entry *child = _image->findChild(_entry, n);
		String childInternal = _internalPath.empty() ? n : (_internalPath + "/" + n);
		if (child)
			childInternal = _internalPath.empty() ? child->name : (_internalPath + "/" + child->name);
		return new IsoFilesystemNode(_image, _isoFilePath, childInternal, child);
	}

	bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override {
		if (!_entry || !_entry->isDirectory)
			return false;

		for (uint i = 0; i < _entry->children.size(); ++i) {
			const Iso9660Entry *child = _entry->children[i];
			if (child->isDirectory && mode == FSNode::kListFilesOnly)
				continue;
			if (!child->isDirectory && mode == FSNode::kListDirectoriesOnly)
				continue;

			String childInternal = _internalPath.empty()
				? child->name
				: (_internalPath + "/" + child->name);
			list.push_back(new IsoFilesystemNode(_image, _isoFilePath, childInternal, child));
		}

		return true;
	}

	AbstractFSNode *getParent() const override {
		if (_internalPath.empty())
			return new IsoFilesystemNode(_image, _isoFilePath, String(), _image->getRoot());

		size_t slash = _internalPath.findLastOf('/');
		size_t bslash = _internalPath.findLastOf('\\');
		size_t sep = String::npos;
		if (slash != String::npos && bslash != String::npos)
			sep = MAX(slash, bslash);
		else if (slash != String::npos)
			sep = slash;
		else
			sep = bslash;

		String parentInternal = (sep == String::npos) ? String() : String(_internalPath.c_str(), sep);
		const Iso9660Entry *parentEntry = _image->findEntry(parentInternal);
		return new IsoFilesystemNode(_image, _isoFilePath, parentInternal, parentEntry);
	}

	SeekableReadStream *createReadStream() override {
		if (!_entry || _entry->isDirectory)
			return nullptr;
		return _image->createReadStream(_entry);
	}

	SeekableWriteStream *createWriteStream(bool atomic) override {
		return nullptr;
	}

	bool createDirectory() override {
		return false;
	}

private:
	SharedPtr<Iso9660Image> _image;
	String _isoFilePath;
	String _internalPath;
	String _fullPath;
	String _name;
	const Iso9660Entry *_entry;
};

} // End of anonymous namespace

AbstractFSNode *tryCreateIsoFilesystemNode(const String &nativePath) {
	String isoFilePath;
	String internalPath;
	if (!splitIsoPath(nativePath, isoFilePath, internalPath))
		return nullptr;

	SharedPtr<Iso9660Image> image = Iso9660Image::openCached(isoFilePath);
	if (!image)
		return nullptr;

	const Iso9660Entry *entry = image->findEntry(internalPath);
	return new IsoFilesystemNode(image, isoFilePath, internalPath, entry);
}

} // End of namespace Common
