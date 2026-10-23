#include "../code/log/logger.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

int main()
{
    FILE* output = std::tmpfile();
    assert(output);
    const int savedStderr = dup(STDERR_FILENO);
    assert(savedStderr >= 0);
    assert(std::fflush(stderr) == 0);
    assert(dup2(fileno(output), STDERR_FILENO) == STDERR_FILENO);

    errno = EAGAIN;
    assert(Log(LogLevel::ERROR, "sample", 8, 0x100000001ULL, EPIPE));
    const int actualErrno = errno;
    if (actualErrno != EAGAIN) {
        dup2(savedStderr, STDERR_FILENO);
        std::fprintf(stderr, "Log changed errno: expected=%d actual=%d\n", EAGAIN, actualErrno);
    }
    assert(actualErrno == EAGAIN); // error 是记录内容，不是应该恢复的 errno。
    errno = EINTR;
    assert(Log(LogLevel::WARN, nullptr));
    assert(errno == EINTR);

    std::atomic<bool> success{true};
    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([&, worker] {
            for (int i = 0; i < 100; ++i) {
                errno = ERANGE;
                const auto id = static_cast<std::uint64_t>(worker * 100 + i);
                if (!Log(LogLevel::INFO, "parallel", worker, id, 0) || errno != ERANGE)
                    success = false;
            }
        });
    }
    for (auto& writer : writers) writer.join();
    assert(success.load());
    assert(std::fflush(stderr) == 0);
    assert(dup2(savedStderr, STDERR_FILENO) == STDERR_FILENO);
    close(savedStderr);

    std::rewind(output);
    std::string text;
    char buffer[4096];
    while (const auto count = std::fread(buffer, 1, sizeof(buffer), output))
        text.append(buffer, count);
    assert(!std::ferror(output));
    std::fclose(output);

    const std::regex linePattern(
        R"(^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] \[(INFO|WARN|ERROR)\] event=([a-z]+) fd=(-?[0-9]+) id=([0-9]+) errno=([0-9]+)$)");
    std::set<unsigned long long> ids;
    std::size_t position = 0;
    int lines = 0;
    while (position < text.size()) {
        const auto end = text.find('\n', position);
        assert(end != std::string::npos);
        const auto line = text.substr(position, end - position);
        std::smatch match;
        assert(std::regex_match(line, match, linePattern));
        if (match[2] == "parallel") assert(ids.insert(std::stoull(match[4])).second);
        if (match[2] == "sample") {
            assert(match[1] == "ERROR");
            assert(std::stoull(match[4]) == 0x100000001ULL);
            assert(std::stoi(match[5]) == EPIPE);
        }
        ++lines;
        position = end + 1;
    }
    assert(lines == 402 && ids.size() == 400);
    std::puts("PASS 日期/级别/64位id/错误号、空事件、errno 保留、四线程400行完整且唯一");

    // Linux /dev/full 对写入返回 ENOSPC，验证失败被报告且不覆盖调用者 errno。
    const int full = open("/dev/full", O_WRONLY);
    assert(full >= 0);
    const int backup = dup(STDERR_FILENO);
    assert(backup >= 0);
    assert(dup2(full, STDERR_FILENO) == STDERR_FILENO);
    close(full);
    errno = EAGAIN;
    const bool failed = !Log(LogLevel::ERROR, "writefailure", 9, 2, EPIPE);
    const bool preserved = errno == EAGAIN;
    assert(dup2(backup, STDERR_FILENO) == STDERR_FILENO);
    close(backup);
    std::clearerr(stderr);
    assert(failed && preserved);
    std::puts("PASS 日志写入失败返回 false，并保留调用者 errno");
}
