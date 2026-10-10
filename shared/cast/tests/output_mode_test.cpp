#include "output_mode.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
using namespace casting;
int main() {
    char temp[] = "/tmp/c1-output-mode-XXXXXX";
    auto dir = mkdtemp(temp); assert(dir);
    setenv("C1_APPS_DATA", dir, 1);
    assert(output_mode("nes") == OutputMode::Local);
    for (auto mode : {OutputMode::Local, OutputMode::Remote, OutputMode::Both}) {
        assert(set_output_mode("nes", mode));
        assert(output_mode("nes") == mode);
        assert(output_mode("bilibili") == OutputMode::Local);
    }
    struct stat st{};
    auto file = std::string(dir) + "/cast/output-nes";
    assert(!stat(file.c_str(), &st) && (st.st_mode & 0777) == 0600);
    assert(!set_output_mode("../escape", OutputMode::Remote));
    assert(!set_output_mode("", OutputMode::Remote));
    assert(!set_output_mode("nes", static_cast<OutputMode>(42)));
    std::ofstream(file) << "remote\nunexpected";
    assert(output_mode("nes") == OutputMode::Local);
    unlink(file.c_str()); assert(!symlink("/dev/zero", file.c_str()));
    assert(output_mode("nes") == OutputMode::Local);
    assert(set_output_mode("nes", OutputMode::Remote));
    std::thread other([] { for (int n=0;n<20;++n) assert(set_output_mode("bilibili", OutputMode::Both)); });
    for (int n=0;n<20;++n) assert(set_output_mode("nes", OutputMode::Local));
    other.join();
    assert(output_mode("nes") == OutputMode::Local && output_mode("bilibili") == OutputMode::Both);
    std::filesystem::remove_all(dir);
}
