#pragma once
#include <cstdint>

// CPU cycles since the most recent audio frame. Unlike g_wPassedClocks,
// this counter is not reduced at the end of every K6502_Step/scanline.
extern uint32_t c1_nes_apu_cycles;
uint8_t c1_nes_apu_status();
void c1_nes_apu_frame_counter(uint8_t value);
bool c1_nes_apu_irq_pending();
