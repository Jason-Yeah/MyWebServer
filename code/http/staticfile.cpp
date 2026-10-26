#include "staticfile.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace  
{
// 8MB
constexpr size_t MAXFILEBYTES = 8u * 1024u * 1024u; 

class FileDescriptor
{
public:
    explicit FileDescriptor(int fd = -1): fd_(fd) {}
    ~FileDescriptor() 
    {
        if (fd_ >= 0) close(fd_);
    }

    int Get() const { return fd_; }
    void Reset(int fd) 
    {
        if (fd_ >= 0) close(fd_);
        fd_ = fd;
    }
private:
    int fd_;

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
};

StaticFileResult Error(const char* status, const char* message)
{
    return {
        status, "text/plain; charset=utf-8", message
    };
}

StaticFileResult OpenError(int error)
{
    if (error == ENOENT || error == ENOTDIR)
        return Error("404 Not Found", "Static file not found.\n");

    if (error == EACCES || error == EPERM || error == ELOOP)
        return Error("403 Forbidden", "Static file access denied.\n");

    if (error == ENAMETOOLONG)
        return Error("414 URI Too Long", "Static path is too long.\n");

    return Error(
        "500 Internal Server Error",
        "Static file operation failed.\n"
    );
}

bool AllowedName(const std::string& name)
{
    if (name.empty() || name.front() == '.')
        return false;

    for (char c : name)
    {
        const bool allowed =
            (c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || c == '_'
            || c == '-'
            || c == '.';

        if (!allowed)
            return false;
    }
    return true;
}

std::string MimeType(const std::string& filename)
{
    const auto dot = filename.find_last_of('.');

    if (dot == std::string::npos)
        return {};

    std::string extension = filename.substr(dot);

    for (char& c : extension)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');

    if (extension == ".css")
        return "text/css; charset=utf-8";

    if (extension == ".js")
        return "text/javascript; charset=utf-8";

    if (extension == ".txt")
        return "text/plain; charset=utf-8";

    if (extension == ".png")
        return "image/png";

    if (extension == ".jpg" || extension == ".jpeg")
        return "image/jpeg";

    if (extension == ".gif")
        return "image/gif";

    if (extension == ".webp")
        return "image/webp";

    if (extension == ".ico")
        return "image/x-icon";

    return {};
}

}

StaticFileResult LoadStaticFile(const std::string& target)
{
    const std::string path = target.substr(0, target.find('?'));
    
    if (path.size() > 1024)
        return Error("414 URI Too Long", "Static path is too long.\n");

    const std::string prefix = "/static/";

    // Confirm the the prefix of path is `/static/`
    if (path.compare(0, prefix.size(), prefix) != 0)
        return Error("404 Not Found", "Static file not found.\n");

    std::vector<std::string> parts;
    size_t begin = prefix.size();

    while (true)
    {
        // Skip prefix, check other content.
        const auto slash = path.find('/', begin);

        const std::string part =
            path.substr(begin, slash - begin);

        if (!AllowedName(part))
            return Error("400 Bad Request", "Invalid static path.\n");

        parts.push_back(part);

        if (slash == std::string::npos)
            break;

        begin = slash + 1;
    }
    // file name == parts.back(), and others just middum path.
    const std::string contentType = MimeType(parts.back());

    if (contentType.empty())
        return Error("403 Forbidden", "Static file type is not enabled.\n");

    FileDescriptor directory(
        open(
            "resources/static",
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
            )
    );

    if (directory.Get() < 0)
    {
        return Error(
            "500 Internal Server Error",
            "Static directory is unavailable.\n"
        );
    }

    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        const int next = openat(
            directory.Get(),
            parts[i].c_str(),
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
        );

        if (next < 0)
            return OpenError(errno);

        directory.Reset(next);
    }

    FileDescriptor file(
        openat(
            directory.Get(),
            parts.back().c_str(),
            O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK
        )
    );

    if (file.Get() < 0) return OpenError(errno);

    struct stat info{};

    // View open objects
    if (fstat(file.Get(), &info) < 0)
        return Error(
            "500 Internal Server Error",
            "Cannot inspect static file.\n"
        );
    if (!S_ISREG(info.st_mode))
        return Error(
            "403 Forbidden",
            "Only regular files are served.\n"
        );

    if (info.st_size < 0 || static_cast<std::uintmax_t>(info.st_size) > MAXFILEBYTES)
        return Error("403 Forbidden", "Static file exceeds the size policy.\n");

    StaticFileResult result {
        "200 OK",
        contentType,
        std::string(static_cast<std::size_t>(info.st_size), '\0')
    };

    size_t received = 0;

    // Read file
    while (received < result.body.size())
    {
        const ssize_t n = read(
            file.Get(),
            &result.body[received],
            result.body.size() - received
        );

        if (n > 0)
        {
            received += static_cast<std::size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) continue;

        // 文件变短或发生读取错误，不返回不完整的成功响应。
        return Error("500 Internal Server Error", "Cannot read complete static file.\n");
    }
    return result;
}
