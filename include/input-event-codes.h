#pragma once
#include <zephyr/dt-bindings/input/input-event-codes.h>

#define INPUT_EV_HE INPUT_EV_VENDOR_START+00

#define INPUT_HE_RC(row,col) (((row) << 8) + (col))
#define INV_INPUT_HE_ROW(item) (item >> 8)
#define INV_INPUT_HE_COL(item) (item & 0xFF)