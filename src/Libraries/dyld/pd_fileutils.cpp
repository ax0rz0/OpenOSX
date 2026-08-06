/*
 * Minimal runtime-side FileUtils for the OpenOSX dyld target.
 *
 * Apple's dyld3/shared-cache/FileUtils.cpp is shared-cache builder tooling: it
 * pulls in iostreams, JSON parsing, order-file processing, and temp-file save
 * machinery. The runtime dyld target only needs read-only file mapping and a
 * few path predicates, so keep this dependency leaf-sized.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <string>

const void* mapFileReadOnly(const char* path, size_t& mappedSize)
{
    mappedSize = 0;

    struct stat statBuf;
    if (::stat(path, &statBuf) != 0)
        return nullptr;

    if (statBuf.st_size <= 0)
        return nullptr;

    int fd = ::open(path, O_RDONLY);
    if (fd == -1)
        return nullptr;

    const size_t fileSize = static_cast<size_t>(statBuf.st_size);
    void* mapping = ::mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);

    if (mapping == MAP_FAILED)
        return nullptr;

    mappedSize = fileSize;
    return mapping;
}

bool fileExists(const std::string& path)
{
    struct stat statBuf;
    return (::stat(path.c_str(), &statBuf) == 0);
}
