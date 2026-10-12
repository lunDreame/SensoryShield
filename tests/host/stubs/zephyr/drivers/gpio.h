#pragma once
#include "zephyr/device.h"
#include <stdint.h>
using gpio_port_pins_t = uint32_t;
struct gpio_callback {};
struct gpio_dt_spec { const device* port; uint32_t pin; uint32_t flags; };
#define GPIO_DT_SPEC_GET(node, prop) {nullptr,0,0}
#define GPIO_INPUT 0
#define GPIO_INT_EDGE_TO_ACTIVE 0
inline bool gpio_is_ready_dt(const gpio_dt_spec*) {return true;}
inline int gpio_pin_configure_dt(const gpio_dt_spec*,int) {return 0;}
inline int gpio_pin_interrupt_configure_dt(const gpio_dt_spec*,int) {return 0;}
inline void gpio_init_callback(gpio_callback*,void (*)(const device*,gpio_callback*,gpio_port_pins_t),uint32_t) {}
inline int gpio_add_callback(const device*,gpio_callback*) {return 0;}
inline int gpio_pin_get_dt(const gpio_dt_spec*) {return 1;}
