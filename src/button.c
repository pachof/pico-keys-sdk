/*
 * This file is part of the Pico Keys SDK distribution (https://github.com/polhenarejos/pico-keys-sdk).
 * Copyright (c) 2022 Pol Henarejos.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "picokeys.h"
#include "button.h"
#include "led/led.h"
#include "pico_time.h"
#if defined(PICO_PLATFORM)
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/gpio.h"
#elif defined(ESP_PLATFORM)
#include "driver/gpio.h"
#endif
#include "usb.h"

extern void execute_tasks(void);

int (*button_pressed_cb)(uint8_t) = NULL;

static bool req_button_pending = false;

bool is_req_button_pending(void) {
    return req_button_pending;
}

bool cancel_button = false;

#if !defined(ENABLE_EMULATION)
#ifdef ESP_PLATFORM
static bool picok_board_button_read(void) {
    int boot_state = gpio_get_level(BOOT_PIN);
    return boot_state == 0;
}
#elif defined(PICO_PLATFORM)
// GPIO pins para botones (puentes a GND)
#define BUTTON_GPIO_15 15
#define BUTTON_GPIO_0  0

void button_gpio_init(void) {
    printf("[BUTTON] Initializing GPIO 15 and GPIO 0\n");
    // Inicializar GPIO 15
    gpio_init(BUTTON_GPIO_15);
    gpio_set_dir(BUTTON_GPIO_15, GPIO_IN);
    gpio_pull_up(BUTTON_GPIO_15);
    
    // Inicializar GPIO 0
    gpio_init(BUTTON_GPIO_0);
    gpio_set_dir(BUTTON_GPIO_0, GPIO_IN);
    gpio_pull_up(BUTTON_GPIO_0);
    printf("[BUTTON] GPIO initialized successfully\n");
}

static bool picok_board_button_read(void) {
    // Leer GPIO 15 o GPIO 0 (ambos activos en bajo cuando se cierran a GND)
    bool button_15 = !gpio_get(BUTTON_GPIO_15);
    bool button_0 = !gpio_get(BUTTON_GPIO_0);
    bool pressed = button_15 || button_0;
    
    if (pressed) {
        printf("[BUTTON] Button pressed! GPIO15=%d, GPIO0=%d\n", button_15, button_0);
    }
    return pressed;
}
#else
static bool picok_board_button_read(void) {
    return true; // always unpressed
}
#endif
static bool button_pressed_state = false;
static uint32_t button_pressed_time = 0;
static uint8_t button_press = 0;

bool button_wait(void) {
    /* Disabled by default. As LED may not be properly configured,
       it will not be possible to indicate button press unless it
       is commissioned. */
    uint32_t button_timeout = 0;
    if (phy_data.up_btn_present) {
        button_timeout = phy_data.up_btn * 1000;
    }
    if (button_timeout == 0) {
        return false;
    }
    uint32_t start_button = board_millis();
    bool timeout = false;
    cancel_button = false;
    uint32_t led_mode = led_get_mode();
    led_set_mode(MODE_BUTTON);
    req_button_pending = true;
    while (picok_board_button_read() == false && cancel_button == false) {
        execute_tasks();
        //sleep_ms(10);
        if (start_button + button_timeout < board_millis()) { /* timeout */
            timeout = true;
            break;
        }
    }
    if (!timeout) {
        while (picok_board_button_read() == true && cancel_button == false) {
            execute_tasks();
            //sleep_ms(10);
            if (start_button + 15000 < board_millis()) { /* timeout */
                timeout = true;
                break;
            }
        }
    }
    led_set_mode(led_mode);
    req_button_pending = false;
    return timeout || cancel_button;
}
#endif

void button_task(void) {
#ifndef ENABLE_EMULATION
    if (button_pressed_cb && board_millis() > 1000 && !is_busy()) { // wait 1 second to boot up
        bool current_button_state = picok_board_button_read();
        if (current_button_state != button_pressed_state) {
            printf("[BUTTON] Button state changed: %d -> %d\n", button_pressed_state, current_button_state);
            if (current_button_state == false) { // unpressed
                if (button_pressed_time == 0 || button_pressed_time + 1000 > board_millis()) {
                    button_press++;
                    printf("[BUTTON] Press count: %d\n", button_press);
                }
                button_pressed_time = board_millis();
            }
            button_pressed_state = current_button_state;
        }
        if (button_pressed_time > 0 && button_press > 0 && button_pressed_time + 1000 < board_millis() && button_pressed_state == false) {
            if (button_pressed_cb != NULL) {
                printf("[BUTTON] Calling callback with %d presses\n", button_press);
                (*button_pressed_cb)(button_press);
            }
            button_pressed_time = button_press = 0;
        }
    }
    else if (!button_pressed_cb && board_millis() > 1000) {
        static uint32_t last_log = 0;
        if (board_millis() - last_log > 5000) {
            printf("[BUTTON] Callback not registered yet\n");
            last_log = board_millis();
        }
    }
#endif
}
