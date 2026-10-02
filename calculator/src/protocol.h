#ifndef SPOTCE_PROTOCOL_H
#define SPOTCE_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define SPOTCE_PACKET_SIZE 64
#define SPOTCE_PAYLOAD_SIZE 56
#define SPOTCE_PROTOCOL_VERSION 1

#define SPOTCE_ART_W 80
#define SPOTCE_ART_H 80
#define SPOTCE_ART_PIXELS (SPOTCE_ART_W * SPOTCE_ART_H)
#define SPOTCE_ART_COLORS 224
#define SPOTCE_ART_PALETTE_OFFSET 32

/* Calculator -> Mac */
#define PKT_CALC_HELLO 0x01
#define PKT_CALC_COMMAND 0x10

/* Mac -> Calculator */
#define PKT_HOST_HELLO 0x80
#define PKT_HOST_STATUS 0x81
#define PKT_HOST_TITLE 0x82
#define PKT_HOST_ARTIST 0x83
#define PKT_HOST_ART_BEGIN 0x90
#define PKT_HOST_ART_PALETTE 0x91
#define PKT_HOST_ART_PIXELS 0x92
#define PKT_HOST_ART_END 0x93
#define PKT_HOST_CLEAR_ART 0x94

/* Calculator commands */
#define CMD_PLAY_PAUSE 1
#define CMD_NEXT 2
#define CMD_PREVIOUS 3
#define CMD_VOLUME_UP 4
#define CMD_VOLUME_DOWN 5

/* Player states */
#define PLAYER_STOPPED 0
#define PLAYER_PAUSED 1
#define PLAYER_PLAYING 2
#define PLAYER_ERROR 3

typedef struct {
    bool usb_connected;
    bool usb_init_failed;
    bool bridge_ready;
    bool spotify_running;
    uint8_t player_state;
    uint8_t volume;
    uint32_t position_ms;
    uint32_t duration_ms;

    char title[SPOTCE_PAYLOAD_SIZE + 1];
    char artist[SPOTCE_PAYLOAD_SIZE + 1];

    bool art_valid;
    uint16_t art_revision;
    uint16_t art_palette[SPOTCE_ART_COLORS];
    uint8_t art_sprite[2 + SPOTCE_ART_PIXELS];

    bool dirty;
} spotce_state_t;

#endif
