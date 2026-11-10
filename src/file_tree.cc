#include "file_tree.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <vector>

namespace fallout {

namespace {

    bool fail(FileTreeError* error, const char* operation, const std::string& path, int code)
    {
        if (error != nullptr) {
            error->code = code;
            error->operation = operation;
            error->path = path;
        }
        return false;
    }

    std::string child(const std::string& folder, const char* name)
    {
        std::string path = folder;
        if (path.empty() || path.back() != '/') {
            path += '/';
        }
        return path + name;
    }

    // The entries of [folder] but "." and "..".
    bool entries(const std::string& folder, std::vector<std::string>* names, FileTreeError* error)
    {
        DIR* dir = opendir(folder.c_str());
        if (dir == nullptr) {
            return fail(error, "list", folder, errno);
        }
        errno = 0;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                names->push_back(entry->d_name);
            }
            errno = 0;
        }
        int code = errno;
        closedir(dir);
        if (code != 0) {
            return fail(error, "list", folder, code);
        }
        return true;
    }

    bool writeAll(int fd, const char* data, size_t length)
    {
        while (length > 0) {
            ssize_t written = write(fd, data, length);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            data += written;
            length -= static_cast<size_t>(written);
        }
        return true;
    }

    bool copyFile(const std::string& from, const std::string& to, const struct stat& info, uint64_t* copied, const std::function<bool(uint64_t)>& progress, std::vector<char>& buffer, FileTreeError* error)
    {
        int in = open(from.c_str(), O_RDONLY | O_CLOEXEC);
        if (in < 0) {
            return fail(error, "open", from, errno);
        }
        int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, info.st_mode & 0777);
        if (out < 0) {
            int code = errno;
            close(in);
            return fail(error, "create", to, code);
        }

        uint64_t length = 0;
        bool ok = true;
        for (;;) {
            ssize_t read = ::read(in, buffer.data(), buffer.size());
            if (read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ok = fail(error, "read", from, errno);
                break;
            }
            if (read == 0) {
                break;
            }
            if (!writeAll(out, buffer.data(), static_cast<size_t>(read))) {
                ok = fail(error, "write", to, errno);
                break;
            }
            length += static_cast<uint64_t>(read);
            *copied += static_cast<uint64_t>(read);
            if (progress && !progress(*copied)) {
                ok = fail(error, "copy", from, ECANCELED);
                break;
            }
        }
        close(in);

        if (ok && fsync(out) != 0) {
            ok = fail(error, "sync", to, errno);
        }
        if (ok) {
            // The times as they were (a move keeps them); not every storage
            // takes them, the data is what matters.
#ifdef __APPLE__
            struct timespec times[2] = { info.st_atimespec, info.st_mtimespec };
#else
            struct timespec times[2] = { info.st_atim, info.st_mtim };
#endif
            futimens(out, times);
        }
        if (close(out) != 0 && ok) {
            ok = fail(error, "close", to, errno);
        }
        if (ok && length != static_cast<uint64_t>(info.st_size)) {
            ok = fail(error, "copy (size)", from, EIO);
        }
        if (ok) {
            struct stat written;
            if (stat(to.c_str(), &written) != 0) {
                ok = fail(error, "check", to, errno);
            } else if (written.st_size != info.st_size) {
                ok = fail(error, "check (size)", to, EIO);
            }
        }
        return ok;
    }

    bool copyTree(const std::string& from, const std::string& to, uint64_t* copied, const std::function<bool(uint64_t)>& progress, std::vector<char>& buffer, FileTreeError* error)
    {
        struct stat info;
        if (lstat(from.c_str(), &info) != 0) {
            return fail(error, "stat", from, errno);
        }
        if (S_ISLNK(info.st_mode)) {
            std::vector<char> target(PATH_MAX + 1);
            ssize_t length = readlink(from.c_str(), target.data(), PATH_MAX);
            if (length < 0) {
                return fail(error, "read link", from, errno);
            }
            target[static_cast<size_t>(length)] = '\0';
            unlink(to.c_str());
            if (symlink(target.data(), to.c_str()) != 0) {
                return fail(error, "link", to, errno);
            }
            return true;
        }
        if (S_ISREG(info.st_mode)) {
            return copyFile(from, to, info, copied, progress, buffer, error);
        }
        if (!S_ISDIR(info.st_mode)) {
            // Not a game's file (a pipe, a device): left out.
            return true;
        }

        if (mkdir(to.c_str(), 0770) != 0 && errno != EEXIST) {
            return fail(error, "make folder", to, errno);
        }
        std::vector<std::string> names;
        if (!entries(from, &names, error)) {
            return false;
        }
        for (const std::string& name : names) {
            if (!copyTree(child(from, name.c_str()), child(to, name.c_str()), copied, progress, buffer, error)) {
                return false;
            }
        }
        return true;
    }

    bool sizeTree(const std::string& path, uint64_t* size, FileTreeError* error)
    {
        struct stat info;
        if (lstat(path.c_str(), &info) != 0) {
            return fail(error, "stat", path, errno);
        }
        if (!S_ISDIR(info.st_mode)) {
            if (S_ISREG(info.st_mode)) {
                *size += static_cast<uint64_t>(info.st_size);
            }
            return true;
        }
        std::vector<std::string> names;
        if (!entries(path, &names, error)) {
            return false;
        }
        for (const std::string& name : names) {
            if (!sizeTree(child(path, name.c_str()), size, error)) {
                return false;
            }
        }
        return true;
    }

    bool deleteTree(const std::string& path, FileTreeError* error)
    {
        struct stat info;
        if (lstat(path.c_str(), &info) != 0) {
            return errno == ENOENT ? true : fail(error, "stat", path, errno);
        }
        if (S_ISDIR(info.st_mode)) {
            std::vector<std::string> names;
            if (!entries(path, &names, error)) {
                // Deleted meanwhile (another deletion of the same trash).
                return error != nullptr && error->code == ENOENT;
            }
            for (const std::string& name : names) {
                if (!deleteTree(child(path, name.c_str()), error)) {
                    return false;
                }
            }
            if (rmdir(path.c_str()) != 0 && errno != ENOENT) {
                return fail(error, "remove folder", path, errno);
            }
            return true;
        }
        if (unlink(path.c_str()) != 0 && errno != ENOENT) {
            return fail(error, "remove", path, errno);
        }
        return true;
    }

} // namespace

std::string FileTreeError::message() const
{
    std::string text = operation + " ";
    for (unsigned char byte : path) {
        if (byte >= 0x20 && byte < 0x7F) {
            text += static_cast<char>(byte);
        } else {
            char escaped[8];
            snprintf(escaped, sizeof(escaped), "\\x%02x", byte);
            text += escaped;
        }
    }
    text += ": ";
    text += strerror(code);
    return text;
}

bool fileTreeSize(const std::string& path, uint64_t* size, FileTreeError* error)
{
    *size = 0;
    return sizeTree(path, size, error);
}

bool fileTreeCopy(const std::string& from, const std::string& to, const std::function<bool(uint64_t)>& progress, FileTreeError* error)
{
    std::vector<char> buffer(1 << 20);
    uint64_t copied = 0;
    return copyTree(from, to, &copied, progress, buffer, error);
}

bool fileTreeDelete(const std::string& path, FileTreeError* error)
{
    return deleteTree(path, error);
}

} // namespace fallout
