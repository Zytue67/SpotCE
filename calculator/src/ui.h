#ifndef SPOTCE_UI_H
#define SPOTCE_UI_H

#include "protocol.h"

enum {
    SPOTCE_CONTROL_NONE = 0,
    SPOTCE_CONTROL_PREVIOUS,
    SPOTCE_CONTROL_PLAY_PAUSE,
    SPOTCE_CONTROL_NEXT,
    SPOTCE_CONTROL_VOLUME
};

void spotce_ui_init(void);
void spotce_ui_draw(const spotce_state_t *state, uint8_t pressed_control);
void spotce_ui_cleanup(void);

#endif
