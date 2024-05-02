// Copyright 2024 sekigon-gonnoc
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include QMK_KEYBOARD_H
#include "bmp.h"
#include "bmp_custom_keycodes.h"

/////////////////////////////
/// miniZoneの実装 ここから ///
////////////////////////////

enum custom_keycodes {
    KC_MY_BTN1 = SAFE_RANGE,
    KC_MY_BTN2,
    KC_MY_BTN3,
    KC_MY_SCR,
    KC_TO_CLICKABLE_INC,
    KC_TO_CLICKABLE_DEC,
    KC_SCROLL_DIR_V,
    KC_SCROLL_DIR_H,
    DRAG_SCROLL,
};

enum click_state {
    NONE = 0,
    WAITING,
    CLICKABLE,
    CLICKING,
    SCROLLING
};

typedef union {
    uint32_t raw;
    struct {
        int16_t to_clickable_movement;
        bool mouse_scroll_v_reverse;
        bool mouse_scroll_h_reverse;
    };
} user_config_t;

user_config_t user_config;

enum click_state state;
uint16_t click_timer;

uint16_t to_reset_time = 1000;
const uint16_t click_layer = 8;

int16_t scroll_v_mouse_interval_counter;
int16_t scroll_h_mouse_interval_counter;
int16_t scroll_v_threshold = 50;
int16_t scroll_h_threshold = 50;

int16_t after_click_lock_movement = 0;

int16_t mouse_record_threshold = 30;
int16_t mouse_move_count_ratio = 5;

const uint16_t ignore_disable_mouse_layer_keys[] = {KC_LGUI, KC_LCTL};

int16_t mouse_movement;
bool set_scrolling = false;

void eeconfig_init_user(void) {
    user_config.raw = 0;
    user_config.to_clickable_movement = 50;
    user_config.mouse_scroll_v_reverse = false;
    user_config.mouse_scroll_h_reverse = false;
    eeconfig_update_user(user_config.raw);
}

void keyboard_post_init_user(void) {
    user_config.raw = eeconfig_read_user();
}

void enable_click_layer(void) {
    layer_on(click_layer);
    click_timer = timer_read();
    state = CLICKABLE;
}

void disable_click_layer(void) {
    state = NONE;
    layer_off(click_layer);
    scroll_v_mouse_interval_counter = 0;
    scroll_h_mouse_interval_counter = 0;
}

int16_t my_abs(int16_t num) {
    return num < 0 ? -num : num;
}

int16_t mouse_move_y_sign(int16_t num) {
    return num < 0 ? -1 : 1;
}

bool is_clickable_mode(void) {
    return state == CLICKABLE || state == CLICKING || state == SCROLLING;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case KC_MY_BTN1:
        case KC_MY_BTN2:
        case KC_MY_BTN3:
            report_mouse_t current_report = pointing_device_get_report();

            uint8_t btn = 1 << (keycode - KC_MY_BTN1);

            if (record->event.pressed) {
                current_report.buttons |= btn;
                state = CLICKING;
                after_click_lock_movement = 30;
            } else {
                current_report.buttons &= ~btn;
            }

            enable_click_layer();

            pointing_device_set_report(current_report);
            pointing_device_send();
            return false;

        case KC_MY_SCR:
            if (record->event.pressed) {
                state = SCROLLING;
            } else {
                enable_click_layer();
            }
            return false;

        case KC_TO_CLICKABLE_INC:
            if (record->event.pressed) {
                user_config.to_clickable_movement += 5;
                eeconfig_update_user(user_config.raw);
            }
            return false;

        case KC_TO_CLICKABLE_DEC:
            if (record->event.pressed) {
                user_config.to_clickable_movement -= 5;

                if (user_config.to_clickable_movement < 5) {
                    user_config.to_clickable_movement = 5;
                }

                eeconfig_update_user(user_config.raw);
            }
            return false;

        case KC_SCROLL_DIR_V:
            if (record->event.pressed) {
                user_config.mouse_scroll_v_reverse = !user_config.mouse_scroll_v_reverse;
                eeconfig_update_user(user_config.raw);
            }
            return false;

        case KC_SCROLL_DIR_H:
            if (record->event.pressed) {
                user_config.mouse_scroll_h_reverse = !user_config.mouse_scroll_h_reverse;
                eeconfig_update_user(user_config.raw);
            }
            return false;

        case DRAG_SCROLL:
            set_scrolling = record->event.pressed;
            return false;

        default:
            if (record->event.pressed) {
                if (state == CLICKING || state == SCROLLING) {
                    enable_click_layer();
                    return false;
                }

                for (int i = 0; i < sizeof(ignore_disable_mouse_layer_keys) / sizeof(ignore_disable_mouse_layer_keys[0]); i++) {
                    if (keycode == ignore_disable_mouse_layer_keys[i]) {
                        enable_click_layer();
                        return true;
                    }
                }

                disable_click_layer();
            }
            break;
    }

    return true;
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    int16_t current_x = mouse_report.x;
    int16_t current_y = mouse_report.y;
    int16_t current_h = 0;
    int16_t current_v = 0;

    if (current_x != 0 || current_y != 0) {
        if (set_scrolling) {
            current_h = current_x;
            current_v = -current_y;
            current_x = 0;
            current_y = 0;
        } else {
            switch (state) {
                case CLICKABLE:
                    click_timer = timer_read();
                    break;

                case CLICKING:
                    after_click_lock_movement -= my_abs(current_x) + my_abs(current_y);

                    if (after_click_lock_movement > 0) {
                        current_x = 0;
                        current_y = 0;
                    }
                    break;

                case SCROLLING:
                    int8_t rep_v = 0;
                    int8_t rep_h = 0;

                    if (my_abs(current_y) * 2 > my_abs(current_x)) {
                        scroll_v_mouse_interval_counter += current_y;

                        while (my_abs(scroll_v_mouse_interval_counter) > scroll_v_threshold) {
                            if (scroll_v_mouse_interval_counter < 0) {
                                scroll_v_mouse_interval_counter += scroll_v_threshold;
                                rep_v += scroll_v_threshold;
                            } else {
                                scroll_v_mouse_interval_counter -= scroll_v_threshold;
                                rep_v -= scroll_v_threshold;
                            }
                        }
                    } else {
                        scroll_h_mouse_interval_counter += current_x;

                        while (my_abs(scroll_h_mouse_interval_counter) > scroll_h_threshold) {
                            if (scroll_h_mouse_interval_counter < 0) {
                                scroll_h_mouse_interval_counter += scroll_h_threshold;
                                rep_h += scroll_h_threshold;
                            } else {
                                scroll_h_mouse_interval_counter -= scroll_h_threshold;
                                rep_h -= scroll_h_threshold;
                            }
                        }
                    }

                    current_h = rep_h / scroll_h_threshold * (user_config.mouse_scroll_h_reverse ? -1 : 1);
                    current_v = -rep_v / scroll_v_threshold * (user_config.mouse_scroll_v_reverse ? -1 : 1);
                    current_x = 0;
                    current_y = 0;
                    break;

                case WAITING:
                    mouse_movement += my_abs(current_x) + my_abs(current_y);

                    if (mouse_movement >= user_config.to_clickable_movement) {
                        mouse_movement = 0;
                        enable_click_layer();
                    }
                    break;

                default:
                    click_timer = timer_read();
                    state = WAITING;
                    mouse_movement = 0;
                    break;
            }
        }
    } else {
        switch (state) {
            case CLICKING:
            case SCROLLING:
                break;

            case CLICKABLE:
                if (timer_elapsed(click_timer) > to_reset_time) {
                    disable_click_layer();
                }
                break;

            case WAITING:
                if (timer_elapsed(click_timer) > 50) {
                    mouse_movement = 0;
                    state = NONE;
                }
                break;

            default:
                mouse_movement = 0;
                state = NONE;
                break;
        }
    }

    mouse_report.x = current_x;
    mouse_report.y = current_y;
    mouse_report.h = current_h;
    mouse_report.v = current_v;

    return mouse_report;
}

/////////////////////////////
/// miniZoneの実装 ここまで ///
////////////////////////////

const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(KC_ESC, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_INT3, KC_BSPC,
                KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_ENT,
                KC_LCTL, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_NUHS,
                KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLASH, KC_INT1, KC_UP, KC_RSFT,
                MO(1), KC_GRV, KC_LGUI, KC_LALT, KC_INT5, KC_SPC, KC_SPC, KC_INT4, KC_INT2, KC_RALT, MO(1), KC_LEFT, KC_DOWN, KC_RGHT),
    [1] = LAYOUT(KC_PWR, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_INS, KC_DEL,
                KC_CAPS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_BTN1, KC_BTN2, KC_PSCR, KC_SCRL, KC_PAUS, KC_UP, KC_TRNS, KC_TRNS,
                KC_TRNS, KC_VOLD, KC_VOLU, KC_MUTE, KC_EJCT, KC_TRNS, KC_PAST, KC_PSLS, KC_HOME, KC_PGUP, KC_LEFT, KC_RGHT, KC_TRNS,
                KC_TRNS, AD_WO_L, SEL_BLE, SEL_USB, KC_TRNS, KC_TRNS, KC_PPLS, KC_PMNS, KC_END, KC_PGDN, KC_DOWN, KC_TRNS, KC_RSFT, KC_TRNS,
                KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_DEL, KC_LGUI, KC_RCTL)
};
