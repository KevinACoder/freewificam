/*
 * Copyright (c) 2026, zhugengyu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side USB Audio Class 1.0 microphone: the class driver half.  It claims
 * the AudioControl interface (class 0x01 subclass 0x01) and the
 * AudioStreaming interface (0x01/0x02), copies and parses the class-specific
 * descriptors while the raw configuration descriptor is still alive, and
 * provides the control requests the capture engine needs (alternate setting,
 * sampling frequency, mute).
 *
 * This is deliberately not a general UAC framework: it supports one
 * microphone stream, format type I PCM, 16-bit samples, discrete sample
 * rates, and isochronous IN endpoints at one frame per packet.  Anything
 * outside that set is refused with a named reason by the descriptor census
 * instead of being half-supported.
 */
#ifndef USBH_UAC_H
#define USBH_UAC_H

#include "usb_audio.h"

#ifndef CONFIG_USBHOST_MAX_AUDIO_CLASS
#define CONFIG_USBHOST_MAX_AUDIO_CLASS 1
#endif

/* The camera's UAC1 rate table is one or two entries; eight is the cap a
 * continuous-rate descriptor's two endpoints would also fit inside. */
#ifndef CONFIG_USBHOST_UAC_MAX_RATES
#define CONFIG_USBHOST_UAC_MAX_RATES 8
#endif

/* Raw class-specific descriptor bytes kept for the census: the camera's AC
 * block is under 100 bytes and the AS block under 100; 192 is the same
 * "enough with margin" choice the video line made for its tables. */
#ifndef CONFIG_USBHOST_UAC_RAW_BYTES
#define CONFIG_USBHOST_UAC_RAW_BYTES 192
#endif

struct usbh_uac {
    struct usbh_hubport *hport;
    struct usb_endpoint_descriptor *isoin;
    uint8_t ctrl_intf;
    uint8_t stream_intf;
    uint8_t minor;

    uint16_t bcdADC;
    uint8_t protocol;

    /* 0 = the descriptor set is usable; negative = the census refusal.  The
     * instance is published either way so the application's detect case can
     * print the reason instead of waiting for a class that will never come. */
    int error;
    char reason[48];

    /* AudioControl: the microphone path. */
    uint8_t ac_input_terminal_id;
    uint16_t ac_input_terminal_type;
    uint8_t ac_output_terminal_id;
    uint8_t ac_output_terminal_source;
    uint8_t channels;
    uint16_t channel_config;

    /* Feature unit (0 = absent).  bmaControls entry 0 is the master, entry i
     * is channel i; a control selector is present when its bit is set in an
     * entry.  fu_mute_ch / fu_volume_ch are the channel number to address
     * (0 = master) or 0xff when the unit does not carry the control. */
    uint8_t fu_id;
    uint8_t fu_bcontrolsize;
    uint8_t fu_mute_ch;
    uint8_t fu_volume_ch;

    /* AudioStreaming: format type I and the streaming endpoint. */
    uint8_t as_terminal_link;
    uint8_t as_format_type;
    uint16_t as_format_tag;
    uint8_t as_subframe_size;
    uint8_t as_bit_resolution;
    uint8_t as_num_rates;
    uint32_t as_rates[CONFIG_USBHOST_UAC_MAX_RATES];
    uint8_t as_rate_type; /* 0 = continuous range, else discrete count */
    uint32_t as_rate_min;
    uint32_t as_rate_max;
    uint8_t as_alt;
    uint16_t as_mps;
    uint8_t as_interval;
    uint8_t as_sfctrl;
    uint8_t as_ep_addr;
    bool as_ep_in;

    /* Session state. */
    bool is_opened;
    bool alt_set;
    uint32_t rate;
    uint8_t rate_set; /* 1 = SET_CUR acknowledged, 2 = no control, 3 = stalled */
    uint32_t get_rate;
    bool mute_known;
    bool muted;

    uint8_t raw_ac[CONFIG_USBHOST_UAC_RAW_BYTES];
    uint8_t raw_ac_len;
    uint8_t raw_as[CONFIG_USBHOST_UAC_RAW_BYTES];
    uint8_t raw_as_len;

    void *user_data;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Open the streaming interface at `alt`, set the sampling frequency when the
 * endpoint carries the control, and bind the endpoint.  Idempotent.
 * Returns 0, or a negative USB error. */
int usbh_uac_open(struct usbh_uac *uac, uint32_t rate, uint8_t alt);

/* Leave the streaming interface at alternate setting 0.  The camera family is
 * not closed by the capture engine (a leave can wedge its class requests for
 * the rest of the boot), so this is only for disconnect/cleanup paths. */
int usbh_uac_close(struct usbh_uac *uac);

/* Feature-unit mute: GET_CUR / SET_CUR.  Returns the mute state (0/1) or a
 * negative error when the unit has no mute control. */
int usbh_uac_get_mute(struct usbh_uac *uac);
int usbh_uac_set_mute(struct usbh_uac *uac, bool mute);

/* Class hooks the application overrides (weak in the class). */
void usbh_uac_run(struct usbh_uac *uac);
void usbh_uac_stop(struct usbh_uac *uac);

#ifdef __cplusplus
}
#endif

#endif /* USBH_UAC_H */