#include "shutter_sound.hpp"
#include <tinyalsa/mixer.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstdio>

namespace camera {
void ShutterSound::poll(uint32_t now) {
    if (pid_ <= 0) return;
    int status = 0;
    if (waitpid(pid_, &status, WNOHANG) == pid_) {
        fprintf(stderr, "[camera] shutter sound status=%d\n", status);
        pid_ = -1;
    } else if (uint32_t(now-started_) > 1500) {
        fprintf(stderr, "[camera] shutter sound timed out\n"); stop();
    }
}
void ShutterSound::stop() {
    if (pid_ <= 0) return;
    kill(pid_, SIGKILL); // Only the short-lived helper owned by this instance.
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
}
void ShutterSound::play(const std::string& path, uint32_t now) {
    poll(now); if (pid_ > 0 || access(path.c_str(), R_OK) != 0) return;
    auto* mixer = mixer_open(0); if (!mixer) return;
    auto* volume = mixer_get_ctl_by_name(mixer, "softvolume");
    long values[2]{};
    if (!volume || mixer_ctl_get_array(volume, values, 2) || (values[0] <= 0 && values[1] <= 0)) {
        mixer_close(mixer); return; // Never raise volume or override system mute.
    }
    // Existing device route used by Piano; do not alter softvolume.
    for (auto name : {"aw87xxx_profile_switch_0", "SPKPA_L", "SPKPA_R"}) {
        auto* ctl = mixer_get_ctl_by_name(mixer, name);
        if (ctl) mixer_ctl_set_value(ctl, 0, name[0] == 'a' ? 0 : 1);
    }
    mixer_close(mixer);
    pid_t parent = getpid();
    pid_ = fork();
    if (pid_ == 0) {
        // Camera/fb/input/launcher-lock descriptors must not survive exec.
        signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) _exit(1);
        long limit = sysconf(_SC_OPEN_MAX);
        for (int fd = 3; fd < (limit > 0 ? limit : 1024); ++fd) close(fd);
        execl("/usr/bin/aplay", "aplay", "-q", "-D", "hw:0,0", "--period-size=1280", "--buffer-size=10240", path.c_str(), (char*)nullptr);
        _exit(127);
    }
    if (pid_ > 0) { started_ = now; fprintf(stderr, "[camera] shutter sound started\n"); }
}
}
