#pragma once
// ============================================================
// MappedFile.h — RAII Memory-Mapped File Reader (Zero-Copy)
//
// WHY MMAP OVER std::ifstream?
//
//   std::ifstream::read() path:
//     1. read() syscall → kernel reads disk blocks into page cache
//     2. Kernel copies page cache → user-space heap buffer (copy #1)
//     3. Application reads from heap buffer (copy #2)
//     Total: 2 copies + syscall overhead + heap allocation
//
//   mmap() path:
//     1. mmap() syscall → OS maps file's page cache into process space
//     2. Application reads directly from the mapped address (0 copies)
//     Total: 0 copies after the initial mapping
//
//   The OS page cache IS the buffer. When parse_line() accesses
//   mapped memory, the CPU loads directly from page cache via
//   virtual address translation — no memcpy, no heap.
//
//   Additional benefit: FILE_FLAG_SEQUENTIAL_SCAN (Windows) /
//   MADV_SEQUENTIAL (Linux) tells the OS to prefetch pages ahead
//   of the current read position — effectively pipelining disk I/O
//   with CSV parsing.
//
// BENCHMARK TARGET: 3-4x faster than ifstream on 100MB files.
//
// RAII GUARANTEE: destructor always unmaps, even on exceptions.
// ============================================================
#include <string_view>
#include <stdexcept>
#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Aquila {

class MappedFile {
public:
    explicit MappedFile(std::string_view path) {
        const std::string path_str(path);

#ifdef _WIN32
        m_file = CreateFileA(path_str.c_str(),
                             GENERIC_READ,
                             FILE_SHARE_READ,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, // OS prefetch hint
                             nullptr);
        if (m_file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("MappedFile: cannot open: " + path_str);

        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(m_file, &sz))
            throw std::runtime_error("MappedFile: GetFileSizeEx failed");
        m_size = static_cast<std::size_t>(sz.QuadPart);

        if (m_size > 0) {
            m_mapping = CreateFileMappingA(m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (!m_mapping)
                throw std::runtime_error("MappedFile: CreateFileMapping failed");
            m_view = MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, 0);
            if (!m_view)
                throw std::runtime_error("MappedFile: MapViewOfFile failed");
        }
#else
        const int fd = ::open(path_str.c_str(), O_RDONLY);
        if (fd < 0)
            throw std::runtime_error("MappedFile: cannot open: " + path_str);

        struct stat sb{};
        if (::fstat(fd, &sb) < 0) {
            ::close(fd);
            throw std::runtime_error("MappedFile: fstat failed");
        }
        m_size = static_cast<std::size_t>(sb.st_size);

        if (m_size > 0) {
            // MAP_POPULATE: pre-fault all pages before returning from mmap().
            // Eliminates page-fault latency during parsing.
            m_view = ::mmap(nullptr, m_size, PROT_READ,
                            MAP_PRIVATE | MAP_POPULATE, fd, 0);
            if (m_view == MAP_FAILED) {
                m_view = nullptr;
                ::close(fd);
                throw std::runtime_error("MappedFile: mmap failed");
            }
            // Sequential hint: OS increases readahead window
            ::madvise(m_view, m_size, MADV_SEQUENTIAL);
        }
        ::close(fd); // fd can be closed; mapping persists until munmap()
#endif
    }

    ~MappedFile() noexcept {
#ifdef _WIN32
        if (m_view)                    UnmapViewOfFile(m_view);
        if (m_mapping)                 CloseHandle(m_mapping);
        if (m_file != INVALID_HANDLE_VALUE) CloseHandle(m_file);
#else
        if (m_view) ::munmap(m_view, m_size);
#endif
    }

    // Non-copyable (ownership of OS resource)
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // Returns entire file as a zero-copy string_view over mapped memory
    [[nodiscard]] std::string_view view() const noexcept {
        return {static_cast<const char*>(m_view), m_size};
    }

    [[nodiscard]] std::size_t size()  const noexcept { return m_size; }
    [[nodiscard]] bool        empty() const noexcept { return m_size == 0; }

private:
    void*       m_view = nullptr;
    std::size_t m_size = 0;
#ifdef _WIN32
    HANDLE      m_file    = INVALID_HANDLE_VALUE;
    HANDLE      m_mapping = nullptr;
#endif
};

} // namespace Aquila
