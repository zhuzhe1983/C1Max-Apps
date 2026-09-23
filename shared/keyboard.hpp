#pragma once
#include <cstdint>
namespace keyboard {
void open();
void close();
void poll();
uint32_t take();
bool caps_lock();
}
