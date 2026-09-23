#include "pty.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace terminal {
Pty::~Pty() { stop(); }
void Pty::fail(const char *operation) { error_ = std::string(operation) + ": " + std::strerror(errno); }
bool Pty::start(const std::vector<std::string> &argv, int rows, int cols,
                const std::string &directory, const std::vector<std::string> &environment) {
    stop(); error_.clear(); eof_ = false; reaped_ = false; status_ = 0;
    if (argv.empty() || argv[0].empty() || argv[0][0] != '/' || rows < 1 || cols < 1 || rows > 200 || cols > 400) {
        error_ = "Invalid shell or terminal size"; return false;
    }
    master_ = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master_ < 0) { fail("Open PTY (/dev/ptmx and mounted /dev/pts required)"); return false; }
    if (grantpt(master_) || unlockpt(master_)) { fail("Prepare PTY"); stop(); return false; }
    const char *name = ptsname(master_);
    if (!name) { fail("Resolve PTY slave"); stop(); return false; }
    int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) { fail("Open PTY slave"); stop(); return false; }
    termios settings{};
    if (tcgetattr(slave, &settings) != 0) {
        fail("Read PTY termios"); close(slave); stop(); return false;
    }
    settings.c_iflag = BRKINT | ICRNL | IXON;
#ifdef IUTF8
    settings.c_iflag |= IUTF8;
#endif
    settings.c_oflag = OPOST | ONLCR;
    settings.c_cflag = CS8 | CREAD;
    settings.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    settings.c_cc[VINTR] = 3; settings.c_cc[VQUIT] = 28;
    settings.c_cc[VERASE] = 127; settings.c_cc[VKILL] = 21;
    settings.c_cc[VEOF] = 4; settings.c_cc[VSTART] = 17; settings.c_cc[VSTOP] = 19;
    settings.c_cc[VSUSP] = 26; settings.c_cc[VMIN] = 1; settings.c_cc[VTIME] = 0;
    winsize size{}; size.ws_row = static_cast<unsigned short>(rows); size.ws_col = static_cast<unsigned short>(cols);
    size.ws_xpixel = 800; size.ws_ypixel = 308;
    if (tcsetattr(slave, TCSANOW, &settings) || ioctl(slave, TIOCSWINSZ, &size)) {
        fail("Configure PTY"); close(slave); stop(); return false;
    }
    std::vector<char *> args;
    for (const auto &arg : argv) args.push_back(const_cast<char *>(arg.c_str()));
    args.push_back(nullptr);
    child_ = fork();
    if (child_ < 0) { fail("Fork shell"); close(slave); stop(); return false; }
    if (child_ == 0) {
        close(master_);
        // This app has no worker threads at fork. Restore shell signal defaults.
        for (int sig : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU})
            signal(sig, SIG_DFL);
        sigset_t mask; sigemptyset(&mask); sigprocmask(SIG_SETMASK, &mask, nullptr);
        if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0) _exit(125);
        if (dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 || dup2(slave, STDERR_FILENO) < 0) _exit(125);
        if (slave > STDERR_FILENO) close(slave);
        // The framebuffer/input/launcher locks must never leak into shell jobs.
        long maximum = sysconf(_SC_OPEN_MAX);
        if (maximum < 0 || maximum > 65536) maximum = 65536;
        for (int fd = 3; fd < maximum; ++fd) close(fd);
        for (const auto &entry : environment) {
            auto pos = entry.find('=');
            if (pos != std::string::npos) setenv(entry.substr(0, pos).c_str(), entry.c_str() + pos + 1, 1);
        }
        if (!directory.empty() && chdir(directory.c_str()) != 0) {
            std::fprintf(stderr, "terminal: cannot cd to %s: %s\r\n", directory.c_str(), std::strerror(errno));
        }
        execv(args[0], args.data());
        std::fprintf(stderr, "terminal: cannot execute %s: %s\r\n", args[0], std::strerror(errno));
        _exit(127);
    }
    session_ = child_;
    close(slave);
    int flags = fcntl(master_, F_GETFL);
    if (flags < 0 || fcntl(master_, F_SETFL, flags | O_NONBLOCK) < 0) { fail("Set nonblocking PTY"); stop(); return false; }
    return true;
}
std::string Pty::read(size_t budget) {
    std::string output;
    if (master_ < 0 || eof_) return output;
    budget = std::min<size_t>(budget, 65536);
    char buffer[4096];
    while (output.size() < budget) {
        auto count = ::read(master_, buffer, std::min(sizeof(buffer), budget - output.size()));
        if (count > 0) { output.append(buffer, size_t(count)); continue; }
        if (count == 0 || (count < 0 && errno == EIO)) { eof_ = true; break; }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) { fail("Read PTY"); eof_ = true; }
        break;
    }
    return output;
}
bool Pty::send(const std::string &bytes) {
    if (master_ < 0 || eof_) return false;
    if (bytes.size() > 65536 - pending_.size()) { error_ = "Shell input queue is full"; return false; }
    pending_ += bytes; pump(); return true;
}
void Pty::pump() {
    if (master_ < 0 || pending_.empty() || eof_) return;
    auto count = write(master_, pending_.data(), pending_.size());
    if (count > 0) pending_.erase(0, size_t(count));
    else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        fail("Write PTY"); pending_.clear(); eof_ = true;
    }
}
bool Pty::running() {
    if (child_ <= 0 || reaped_) return false;
    auto result = waitpid(child_, &status_, WNOHANG);
    if (result == child_ || (result < 0 && errno == ECHILD)) reaped_ = true;
    return !reaped_;
}
void Pty::signal_session(int sig) {
    if (session_ <= 0) return;
    // Interactive job control creates separate process groups. Address both
    // foreground and background jobs, but only inside this terminal's session.
    if (master_ >= 0) {
        pid_t foreground = tcgetpgrp(master_);
        if (foreground > 0 && getsid(foreground) == session_) kill(-foreground, sig);
    }
    if (getsid(session_) == session_) kill(-session_, sig);
#ifdef __linux__
    if (DIR *dir = opendir("/proc")) {
        while (auto *entry = readdir(dir)) {
            char *end = nullptr;
            long candidate = std::strtol(entry->d_name, &end, 10);
            if (end != entry->d_name && !*end && candidate > 0 && candidate <= 0x7fffffff &&
                getsid(pid_t(candidate)) == session_) kill(pid_t(candidate), sig);
        }
        closedir(dir);
    }
#endif
}
void Pty::stop() {
    // Keep session id while escalating, even after the shell is reaped: its
    // foreground/background children may still be alive. Never signal our app.
    if (session_ > 0) signal_session(SIGHUP);
    if (master_ >= 0) { close(master_); master_ = -1; }
    if (session_ > 0) {
        for (int i = 0; i < 10; ++i) { running(); usleep(10000); }
        signal_session(SIGTERM);
        for (int i = 0; i < 10; ++i) { running(); usleep(10000); }
        signal_session(SIGKILL);
        if (child_ > 0 && !reaped_) {
            while (waitpid(child_, &status_, 0) < 0 && errno == EINTR) {}
        }
    }
    child_ = session_ = -1; reaped_ = true; pending_.clear();
}
}
