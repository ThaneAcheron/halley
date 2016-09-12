#include "halley/file/filesystem.h"
#include <boost/filesystem.hpp>

using namespace Halley;
using namespace filesystem;

bool FileSystem::exists(const Path& p)
{
	return filesystem::exists(p);
}

bool FileSystem::createDir(const Path& p)
{
	if (!exists(p)) {
		try {
			return create_directories(p);
		} catch (...) {
			return false;
		}
	}
	return false;
}

bool FileSystem::createParentDir(const Path& p)
{
	return createDir(p.parent_path());
}

int64_t FileSystem::getLastWriteTime(const Path& p)
{
	return last_write_time(p);
}

bool FileSystem::isFile(const Path& p)
{
	return is_regular_file(p);
}

bool FileSystem::isDirectory(const Path& p)
{
	return is_directory(p);
}

void FileSystem::copyFile(const Path& src, const Path& dst)
{
	createParentDir(dst);
	copy_file(src, dst, filesystem::copy_option::overwrite_if_exists);
}

bool FileSystem::remove(const Path& path)
{
	return filesystem::remove(path);
}

void FileSystem::writeFile(const Path& path, gsl::span<const gsl::byte> data)
{
	createParentDir(path);
	std::ofstream fp(path.string(), std::ios::binary | std::ios::out);
	fp.write(reinterpret_cast<const char*>(data.data()), data.size());
	fp.close();
}

void FileSystem::writeFile(const Path& path, const Bytes& data)
{
	writeFile(path, as_bytes(gsl::span<const Byte>(data)));
}

Bytes FileSystem::readFile(const Path& path)
{
	Bytes result;

	std::ifstream fp(path.string(), std::ios::binary | std::ios::in);
	fp.seekg(0, std::ios::end);
	size_t size = fp.tellg();
	fp.seekg(0, std::ios::beg);
	result.resize(size);

	fp.read(reinterpret_cast<char*>(result.data()), size);
	fp.close();

	return result;
}

std::vector<Path> FileSystem::enumerateDirectory(const Path& path)
{
	std::vector<Path> result;
	if (exists(path)) {
		using RDI = filesystem::recursive_directory_iterator;
		RDI end;
		for (RDI i(path); i != end; ++i) {
			Path fullPath = i->path();
			if (isFile(fullPath)) {
				result.push_back(fullPath.lexically_relative(path));
			}
		}
	}
	return result;
}

Path FileSystem::getRelative(const Path& path, const Path& parentPath)
{
	return relative(path, parentPath);
}

Path FileSystem::getAbsolute(const Path& path)
{
	return path.lexically_normal();
}