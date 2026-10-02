#include <tice.h>

#include "protocol.h"
#include "ui.h"
#include "usb_link.h"

/*
 * Keep the large artwork/state buffer out of the calculator's small C stack.
 * This struct includes the 80x80 album image and palette (~7 KB).
 */
static spotce_state_t state;

int main(void) {
    bool running = true;
    uint8_t pressed_control = SPOTCE_CONTROL_NONE;
    uint8_t press_feedback_ticks = 0;

    spotce_state_init(&state);
    spotce_ui_init();
    spotce_ui_draw(&state, pressed_control);
    state.dirty = false;

    /* Draw the UI before starting USB so a USB init error can't blank the app. */
    if (!spotce_usb_init(&state)) {
        state.usb_init_failed = true;
        state.dirty = true;
    }

    while (running) {
        uint8_t key;

        spotce_usb_poll();
        key = os_GetCSC();

        switch (key) {
            case sk_2nd:
                pressed_control = SPOTCE_CONTROL_PLAY_PAUSE;
                press_feedback_ticks = 80;
                state.dirty = true;
                spotce_usb_send_command(CMD_PLAY_PAUSE);
                break;
            case sk_Left:
                pressed_control = SPOTCE_CONTROL_PREVIOUS;
                press_feedback_ticks = 80;
                state.dirty = true;
                spotce_usb_send_command(CMD_PREVIOUS);
                break;
            case sk_Right:
                pressed_control = SPOTCE_CONTROL_NEXT;
                press_feedback_ticks = 80;
                state.dirty = true;
                spotce_usb_send_command(CMD_NEXT);
                break;
            case sk_Up:
                pressed_control = SPOTCE_CONTROL_VOLUME;
                press_feedback_ticks = 80;
                state.dirty = true;
                spotce_usb_send_command(CMD_VOLUME_UP);
                break;
            case sk_Down:
                pressed_control = SPOTCE_CONTROL_VOLUME;
                press_feedback_ticks = 80;
                state.dirty = true;
                spotce_usb_send_command(CMD_VOLUME_DOWN);
                break;
            case sk_Clear:
                running = false;
                break;
            default:
                break;
        }

        if (press_feedback_ticks && --press_feedback_ticks == 0) {
            pressed_control = SPOTCE_CONTROL_NONE;
            state.dirty = true;
        }

        if (state.dirty) {
            spotce_ui_draw(&state, pressed_control);
            state.dirty = false;
        }

        delay(10);
    }

    spotce_usb_cleanup();
    spotce_ui_cleanup();
    return 0;
}
