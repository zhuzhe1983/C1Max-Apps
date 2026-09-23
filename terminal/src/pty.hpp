#pragma once
#include <cstddef>
#include <string>
#include <sys/types.h>
#include <vector>

namespace terminal {
class Pty {
public:
    Pty() = default;
    ~Pty();
    Pty(const Pty &) = delete;
    Pty &operator=(const Pty &) = delete;
    bool start(const std::vector<std::string> &argv, int rows, int cols,
               const std::string &directory, const std::vector<std::string> &environment);
    // Nonblocking; output/read work is bounded so keys and shutdown keep running.
    std::string read(size_t budget = 32768);
    bool send(const std::string &bytes);
    void pump();
    bool running();
    bool eof() const { return eof_; }
    int status() const { return status_; }
    pid_t pid() const { return child_; }
    const std::string &error() const { return error_; }
    void stop();
private:
    int master_ = -1, status_ = 0;
    pid_t child_ = -1, session_ = -1;
    bool reaped_ = false, eof_ = false;
    std::string pending_, error_;
    void fail(const char *operation);
    void signal_session(int signal);
};
}
