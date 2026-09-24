#include "pty.hpp"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using terminal::Pty;
using Clock = std::chrono::steady_clock;
std::string until(Pty &pty, const std::string &marker, int timeout = 4000) {
    std::string output;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
    while (Clock::now() < deadline) {
        pty.pump(); output += pty.read();
        if (output.find(marker) != std::string::npos) return output;
        usleep(10000);
    }
    std::cerr << "PTY expected marker: " << marker << "\nOutput: " << output << "\nError: " << pty.error() << '\n';
    assert(false); return output;
}
void exited(Pty &pty) {
    auto deadline = Clock::now() + std::chrono::seconds(3);
    std::string output;
    while (pty.running() && Clock::now() < deadline) { pty.pump(); output += pty.read(); usleep(10000); }
    if (pty.running()) std::cerr << "Waiting for shell exit; output=" << output << " error=" << pty.error() << '\n';
    assert(!pty.running());
}
int main() {
    {
        Pty pty;
        assert(pty.start({"/bin/sh", "-c", "stty -echo; stty size; printf READY; IFS= read -r value; printf '[%s]' \"$value\""},
                         14, 80, "/", {"LC_ALL=C"}));
        auto output = until(pty, "READY");
        assert(output.find("14 80") != std::string::npos);
        assert(pty.send(std::string("hellx") + char(127) + "o\r"));
        until(pty, "[hello]");
        exited(pty); int status = pty.status(); assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    {
        Pty pty;
        assert(pty.start({"/bin/sh", "-i"}, 14, 80, "/", {"PS1=TEST> ", "TERM=vt100", "ENV=/dev/null"}));
        until(pty, "TEST> ");
        assert(pty.resize(11,66));assert(pty.send("stty size\r"));
        until(pty,"11 66");assert(pty.running());
        assert(pty.send("sleep 20\r")); usleep(120000);
        assert(pty.send(std::string(1, 3))); // Terminal VINTR must reach foreground process group.
        until(pty, "TEST> ");
        assert(pty.running());
        assert(pty.send("printf '\\122\\105\\123\\125\\115\\105\\104'\r"));
        until(pty, "RESUMED");
        assert(pty.send("exit\r")); exited(pty);
    }
    {
        Pty pty;
        assert(pty.start({"/bin/sh", "-c", "trap '' HUP TERM; printf RESIST; while :; do sleep 1; done"},
                         14, 80, "/", {}));
        until(pty, "RESIST");
        const auto pid = pty.pid();
        auto begin = Clock::now(); pty.stop();
        assert(Clock::now() - begin < std::chrono::seconds(2));
        assert(kill(pid, 0) == -1 && errno == ESRCH);
        int status = 0; assert(waitpid(pid, &status, WNOHANG) == -1 && errno == ECHILD);
    }
    {
        Pty pty;
        assert(pty.start({"/no/such/c1max-shell", "-i"}, 14, 80, "/", {}));
        until(pty, "cannot execute"); exited(pty);
        int status = pty.status(); assert(WIFEXITED(status) && WEXITSTATUS(status) == 127);
        pty.stop();
        assert(!pty.start({}, 14, 80, "/", {}) && !pty.error().empty());
    }
    std::cout << "real PTY: shell, canonical backspace, winsize, foreground Ctrl-C, exit, escalation/reap passed\n";
}
