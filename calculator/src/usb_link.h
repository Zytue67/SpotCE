#ifndef SPOTCE_USB_LINK_H
#define SPOTCE_USB_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

void spotce_state_init(spotce_state_t *state);
bool spotce_usb_init(spotce_state_t *state);
void spotce_usb_poll(void);
void spotce_usb_cleanup(void);
bool spotce_usb_send_command(uint8_t command);

#endif
