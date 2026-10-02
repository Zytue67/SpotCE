#include "usb_link.h"

#include <stddef.h>
#include <string.h>

/* Use the same CDC serial-device transport as ForumCE's Mac bridge. */
#define usb_callback_data_t usb_device_t
#include <usbdrvce.h>
#include <srldrvce.h>

#define RX_BUFFER_SIZE 256
#define SERIAL_BUFFER_SIZE 1024

static spotce_state_t *g_state;
static usb_device_t g_usb_device;
static srl_device_t g_serial;
static uint8_t g_serial_buffer[SERIAL_BUFFER_SIZE];
static uint8_t g_rx_buffer[RX_BUFFER_SIZE];
static uint8_t g_read_chunk[128];
static uint16_t g_rx_length;
static uint16_t g_tx_sequence;
static uint16_t g_palette_received;
static uint16_t g_pixels_received;
static uint8_t g_tx_packet[SPOTCE_PACKET_SIZE];
static bool g_usb_started;
static bool g_usb_configured;
static bool g_serial_open;
static uint16_t g_hello_ticks;

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

void spotce_state_init(spotce_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->volume = 0;
    state->player_state = PLAYER_STOPPED;
    strcpy(state->title, "Waiting for Spotify");
    strcpy(state->artist, "Start the Mac bridge");
    state->art_sprite[0] = SPOTCE_ART_W;
    state->art_sprite[1] = SPOTCE_ART_H;
    state->dirty = true;
}

static bool packet_valid(const uint8_t *packet, uint16_t *payload_len) {
    uint16_t len;
    if (packet[0] != 'S' || packet[1] != 'C' || packet[2] != SPOTCE_PROTOCOL_VERSION) {
        return false;
    }
    len = read_u16(packet + 6);
    if (len > SPOTCE_PAYLOAD_SIZE) {
        return false;
    }
    *payload_len = len;
    return true;
}

static void copy_text(char *dst, const uint8_t *src, uint16_t len) {
    if (len > SPOTCE_PAYLOAD_SIZE) len = SPOTCE_PAYLOAD_SIZE;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void process_host_packet(const uint8_t *packet) {
    uint16_t len;
    const uint8_t *payload = packet + 8;
    uint8_t type;

    if (!g_state || !packet_valid(packet, &len)) return;
    type = packet[3];

    switch (type) {
        case PKT_HOST_HELLO:
            g_state->bridge_ready = true;
            g_state->dirty = true;
            break;
        case PKT_HOST_STATUS:
            if (len >= 12) {
                g_state->spotify_running = payload[0] != 0;
                g_state->player_state = payload[1];
                g_state->volume = payload[2] > 100 ? 100 : payload[2];
                g_state->position_ms = read_u32(payload + 4);
                g_state->duration_ms = read_u32(payload + 8);
                if (g_state->duration_ms && g_state->position_ms > g_state->duration_ms)
                    g_state->position_ms = g_state->duration_ms;
                g_state->dirty = true;
            }
            break;
        case PKT_HOST_TITLE:
            copy_text(g_state->title, payload, len);
            g_state->dirty = true;
            break;
        case PKT_HOST_ARTIST:
            copy_text(g_state->artist, payload, len);
            g_state->dirty = true;
            break;
        case PKT_HOST_CLEAR_ART:
            g_state->art_valid = false;
            g_state->dirty = true;
            break;
        case PKT_HOST_ART_BEGIN:
            if (len >= 4 && payload[0] == SPOTCE_ART_W && payload[1] == SPOTCE_ART_H
                    && read_u16(payload + 2) == SPOTCE_ART_COLORS) {
                g_state->art_valid = false;
                g_palette_received = 0;
                g_pixels_received = 0;
                g_state->art_sprite[0] = SPOTCE_ART_W;
                g_state->art_sprite[1] = SPOTCE_ART_H;
            }
            break;
        case PKT_HOST_ART_PALETTE:
            if (len >= 2) {
                uint8_t offset = payload[0];
                uint8_t count = payload[1];
                uint16_t i;
                if ((uint16_t)2 + (uint16_t)count * 2 <= len
                        && (uint16_t)offset + count <= SPOTCE_ART_COLORS) {
                    for (i = 0; i < count; ++i)
                        g_state->art_palette[offset + i] = read_u16(payload + 2 + i * 2);
                    if ((uint16_t)offset + count > g_palette_received)
                        g_palette_received = (uint16_t)offset + count;
                }
            }
            break;
        case PKT_HOST_ART_PIXELS:
            if (len >= 3) {
                uint16_t offset = read_u16(payload);
                uint8_t count = payload[2];
                if ((uint16_t)3 + count <= len
                        && (uint32_t)offset + count <= SPOTCE_ART_PIXELS) {
                    memcpy(g_state->art_sprite + 2 + offset, payload + 3, count);
                    if ((uint16_t)(offset + count) > g_pixels_received)
                        g_pixels_received = (uint16_t)(offset + count);
                }
            }
            break;
        case PKT_HOST_ART_END:
            if (g_palette_received >= SPOTCE_ART_COLORS
                    && g_pixels_received >= SPOTCE_ART_PIXELS) {
                g_state->art_valid = true;
                ++g_state->art_revision;
                g_state->dirty = true;
            }
            break;
        default:
            break;
    }
}

static void consume_serial_bytes(const uint8_t *data, uint16_t length) {
    if (length > RX_BUFFER_SIZE - g_rx_length) {
        g_rx_length = 0;
        if (length > RX_BUFFER_SIZE) {
            data += length - RX_BUFFER_SIZE;
            length = RX_BUFFER_SIZE;
        }
    }
    memcpy(g_rx_buffer + g_rx_length, data, length);
    g_rx_length += length;

    while (g_rx_length >= SPOTCE_PACKET_SIZE) {
        uint16_t payload_len;
        if (packet_valid(g_rx_buffer, &payload_len)) {
            process_host_packet(g_rx_buffer);
            g_rx_length -= SPOTCE_PACKET_SIZE;
            memmove(g_rx_buffer, g_rx_buffer + SPOTCE_PACKET_SIZE, g_rx_length);
        } else {
            --g_rx_length;
            memmove(g_rx_buffer, g_rx_buffer + 1, g_rx_length);
        }
    }
}

static usb_error_t usb_event_handler(usb_event_t event, void *event_data,
                                     usb_device_t *callback_data) {
    /* event_data is a usb_device_t on CONNECTED, but a configuration
       descriptor on HOST_CONFIGURE. Never treat the latter as a device. */
    if (event == USB_DEVICE_CONNECTED_EVENT && callback_data && !*callback_data) {
        *callback_data = (usb_device_t)event_data;
        if (g_state) {
            g_state->usb_connected = true;
            g_state->bridge_ready = false;
            g_state->dirty = true;
        }
    }
    if (event == USB_HOST_CONFIGURE_EVENT) {
        g_usb_configured = true;
    } else if (event == USB_DEVICE_DISCONNECTED_EVENT || event == USB_DEVICE_DISABLED_EVENT) {
        if (g_serial_open) {
            srl_Close(&g_serial);
            g_serial_open = false;
        }
        g_usb_configured = false;
        g_rx_length = 0;
        if (callback_data) *callback_data = NULL;
        if (g_state) {
            g_state->usb_connected = false;
            g_state->bridge_ready = false;
            g_state->dirty = true;
        }
    }
    return srl_UsbEventCallback(event, event_data, callback_data);
}

static bool send_packet(uint8_t type, const void *payload, uint16_t len) {
    int written;
    if (!g_serial_open || len > SPOTCE_PAYLOAD_SIZE) return false;

    g_tx_packet[0] = 'S';
    g_tx_packet[1] = 'C';
    g_tx_packet[2] = SPOTCE_PROTOCOL_VERSION;
    g_tx_packet[3] = type;
    write_u16(g_tx_packet + 4, g_tx_sequence++);
    write_u16(g_tx_packet + 6, len);
    memset(g_tx_packet + 8, 0, SPOTCE_PAYLOAD_SIZE);
    if (payload && len) memcpy(g_tx_packet + 8, payload, len);

    written = srl_Write(&g_serial, g_tx_packet, SPOTCE_PACKET_SIZE);
    return written == SPOTCE_PACKET_SIZE;
}

bool spotce_usb_init(spotce_state_t *state) {
    g_state = state;
    g_usb_device = NULL;
    memset(&g_serial, 0, sizeof(g_serial));
    g_rx_length = 0;
    g_palette_received = 0;
    g_pixels_received = 0;
    g_tx_sequence = 1;
    g_usb_configured = false;
    g_serial_open = false;
    g_hello_ticks = 0;

    g_usb_started = true;
    if (usb_Init(usb_event_handler, &g_usb_device,
                 srl_GetCDCStandardDescriptors(), USB_DEFAULT_INIT_FLAGS) != USB_SUCCESS) {
        usb_Cleanup();
        g_usb_started = false;
        return false;
    }
    return true;
}

void spotce_usb_poll(void) {
    int count;

    if (!g_usb_started) return;
    if (usb_HandleEvents() != USB_SUCCESS) return;

    if (g_usb_configured && g_usb_device && !g_serial_open) {
        if (srl_Open(&g_serial, g_usb_device, g_serial_buffer, sizeof(g_serial_buffer),
                     SRL_INTERFACE_ANY, 115200) == SRL_SUCCESS) {
            g_serial_open = true;
            g_rx_length = 0;
            if (g_state) {
                g_state->usb_connected = true;
                g_state->dirty = true;
            }
        }
    }

    if (!g_serial_open) return;

    do {
        count = srl_Read(&g_serial, g_read_chunk, sizeof(g_read_chunk));
        if (count > 0) consume_serial_bytes(g_read_chunk, (uint16_t)count);
    } while (count > 0);

    if (g_state && g_state->usb_connected && !g_state->bridge_ready) {
        if (++g_hello_ticks >= 50) {
            static const char hello[] = "SpotCE V1";
            if (send_packet(PKT_CALC_HELLO, hello, sizeof(hello) - 1))
                g_hello_ticks = 0;
        }
    }
}

bool spotce_usb_send_command(uint8_t command) {
    return send_packet(PKT_CALC_COMMAND, &command, 1);
}

void spotce_usb_cleanup(void) {
    if (g_serial_open) {
        srl_Close(&g_serial);
        g_serial_open = false;
    }
    if (g_usb_started) {
        usb_Cleanup();
        g_usb_started = false;
    }
    if (g_state) {
        g_state->usb_connected = false;
        g_state->bridge_ready = false;
    }
    g_state = NULL;
}
