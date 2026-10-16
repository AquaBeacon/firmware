#pragma once
#include <Arduino.h>

struct Rs485State {
    uint32_t tx_ok, rx_ok, crc_fail, malformed, last_rx_ms;
};

void rs485_setup();
void rs485_tick(uint32_t now);
const Rs485State& rs485_state();
