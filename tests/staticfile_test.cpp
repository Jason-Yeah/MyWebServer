#include "../code/http/staticfile.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
int interruptedReads = 0;
bool smallReads = false, readError = false, earlyEof = false, statError = false;

int Fds()
{
    DIR* directory = opendir("/proc/self/fd");
    assert(directory);
    int count = 0;
    while (auto* entry = readdir(directory))
        if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, "..")) ++count;
    closedir(directory);
    return count;
}

// 仅在 CTest 的 build 工作目录创建自有临时目录，退出逐项回收，不动项目资源。
struct Fixture {
    std::string original, root;
    std::vector<std::string> files, directories;
    Fixture()
    {
        char cwd[PATH_MAX];
        assert(getcwd(cwd, sizeof(cwd)));
        original = cwd;
        std::string pattern = original + "/staticfile-test-XXXXXX";
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        assert(mkdtemp(name.data()));
        root = name.data();
        assert(chdir(root.c_str()) == 0);
        Directory("resources"); Directory("resources/static");
        Directory("resources/static/sub"); Directory("resources/static/folder.txt");
        Directory("outside");
    }
    void Directory(const std::string& path)
    {
        assert(mkdir(path.c_str(), 0700) == 0);
        directories.push_back(path);
    }
    void File(const std::string& path, const std::string& contents)
    {
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        assert(fd >= 0);
        files.push_back(path);
        std::size_t written = 0;
        while (written < contents.size()) {
            const auto n = write(fd, contents.data() + written, contents.size() - written);
            assert(n > 0);
            written += static_cast<std::size_t>(n);
        }
        assert(close(fd) == 0);
    }
    void Sparse(const std::string& path, off_t size)
    {
        File(path, "");
        const int fd = open(path.c_str(), O_WRONLY);
        assert(fd >= 0 && ftruncate(fd, size) == 0);
        assert(close(fd) == 0);
    }
    void Link(const std::string& path, const char* target)
    {
        assert(symlink(target, path.c_str()) == 0);
        files.push_back(path);
    }
    ~Fixture()
    {
        assert(chdir(original.c_str()) == 0);
        for (auto it = files.rbegin(); it != files.rend(); ++it)
            assert(unlink((root + "/" + *it).c_str()) == 0);
        for (auto it = directories.rbegin(); it != directories.rend(); ++it)
            assert(rmdir((root + "/" + *it).c_str()) == 0);
        assert(rmdir(root.c_str()) == 0);
    }
};

void Status(const std::string& target, const char* expected)
{
    const auto result = LoadStaticFile(target);
    assert(result.status == expected);
}
}

extern "C" ssize_t __real_read(int, void*, size_t);
extern "C" ssize_t __wrap_read(int fd, void* data, size_t size)
{
    if (interruptedReads > 0) { --interruptedReads; errno = EINTR; return -1; }
    if (readError) { readError = false; errno = EIO; return -1; }
    if (earlyEof) { earlyEof = false; return 0; }
    return __real_read(fd, data, smallReads ? std::min(size, std::size_t{3}) : size);
}
extern "C" int __real_fstat(int, struct stat*);
extern "C" int __wrap_fstat(int fd, struct stat* info)
{
    if (statError) { statError = false; errno = EIO; return -1; }
    return __real_fstat(fd, info);
}

int main()
{
    const int baseline = Fds();
    {
        Fixture fixture;
        const std::string binary("a\0b\xff\r\n", 6);
        fixture.File("resources/static/sub/data.PNG", binary);
        fixture.File("resources/static/style.css", "body{color:blue;}\n");
        fixture.File("resources/static/app.js", "const n = 1;\n");
        fixture.File("resources/static/empty.txt", "");
        fixture.File("resources/static/private.txt", "private fixture");
        fixture.File("resources/static/config.env", "not public");
        fixture.File("secret.txt", "outside document root");
        fixture.File("outside/secret.txt", "outside directory");
        fixture.Link("resources/static/link.txt", "../../secret.txt");
        fixture.Link("resources/static/linked", "../../outside");
        assert(mkfifo("resources/static/pipe.txt", 0600) == 0);
        fixture.files.push_back("resources/static/pipe.txt");
        fixture.Sparse("resources/static/limit.txt", 8 * 1024 * 1024);
        fixture.Sparse("resources/static/large.txt", 8 * 1024 * 1024 + 1);

        auto result = LoadStaticFile("/static/sub/data.PNG?version=1");
        assert(result.status == "200 OK" && result.contentType == "image/png" && result.body == binary);
        result = LoadStaticFile("/static/style.css");
        assert(result.contentType == "text/css; charset=utf-8" && result.body == "body{color:blue;}\n");
        assert(LoadStaticFile("/static/app.js").contentType == "text/javascript; charset=utf-8");
        result = LoadStaticFile("/static/empty.txt");
        assert(result.status == "200 OK" && result.body.empty());
        assert(LoadStaticFile("/static/limit.txt").body.size() == 8 * 1024 * 1024);
        Status("/static/large.txt", "403 Forbidden");
        std::puts("PASS 二进制/子目录/查询参数/大小写 MIME/空文件/8MiB 边界");

        for (const auto& target : {"/static/../secret.txt", "/static/sub/../../secret.txt",
                "/static/%2e%2e/secret.txt", "/static/%252e%252e/secret.txt",
                "/static/.hidden.txt", "/static/a\\b.txt", "/static//style.css", "/static/"})
            Status(target, "400 Bad Request");
        Status("/static/missing.css", "404 Not Found");
        Status("/static/config.env", "403 Forbidden");
        Status("/static/link.txt", "403 Forbidden");
        Status("/static/linked/secret.txt", "404 Not Found"); // Linux O_DIRECTORY|O_NOFOLLOW
        Status("/static/folder.txt", "403 Forbidden");
        Status("/static/pipe.txt", "403 Forbidden");
        Status("/static/" + std::string(1013, 'a') + ".txt", "414 URI Too Long");
        Status("/static/" + std::string(260, 'a') + ".txt", "414 URI Too Long");
        if (geteuid() != 0) {
            assert(chmod("resources/static/private.txt", 0) == 0);
            Status("/static/private.txt", "403 Forbidden");
        } else {
            std::puts("SKIP root 下不可验证普通账号 EACCES");
        }
        std::puts("PASS 目录穿越/编码路径/符号链接/特殊文件/扩展名及长度限制");

        interruptedReads = 2;
        smallReads = true; // 每次最多真实读取 3 字节，强制覆盖累计读取分支。
        result = LoadStaticFile("/static/sub/data.PNG");
        smallReads = false;
        assert(interruptedReads == 0 && result.status == "200 OK" && result.body == binary);
        readError = true;
        Status("/static/style.css", "500 Internal Server Error");
        earlyEof = true;
        Status("/static/style.css", "500 Internal Server Error");
        statError = true;
        Status("/static/style.css", "500 Internal Server Error");
        std::puts("PASS 测试包装注入 EINTR/小块读取/EIO/提前 EOF/fstat 失败");

        assert(rename("resources/static", "resources/moved") == 0);
        const auto missingRoot = LoadStaticFile("/static/style.css");
        assert(symlink("moved", "resources/static") == 0);
        const auto symlinkRoot = LoadStaticFile("/static/style.css");
        assert(unlink("resources/static") == 0);
        assert(rename("resources/moved", "resources/static") == 0);
        assert(missingRoot.status == "500 Internal Server Error");
        assert(symlinkRoot.status == "500 Internal Server Error");
        for (int i = 0; i < 100; ++i) {
            Status("/static/style.css", "200 OK");
            Status("/static/link.txt", "403 Forbidden");
            Status("/static/linked/secret.txt", "404 Not Found");
            Status("/static/missing.css", "404 Not Found");
        }
        assert(Fds() == baseline);
    }
    assert(Fds() == baseline);
    std::puts("PASS 静态根目录错误路径、反复成功/失败无 fd 泄漏，临时文件全部回收");
}
