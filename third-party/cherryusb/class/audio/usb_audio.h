/*
 * Copyright (c) 2026, zhugengyu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB Audio Class 1.0 vocabulary used by the host microphone class.  Only the
 * subset this tree's camera needs is defined: the AC/AS interface subclasses,
 * the class-specific descriptor types and subtypes, the class request codes,
 * the terminal types, the feature-unit and endpoint control selectors, and
 * format type I.  Field layouts are read by byte offset in usbh_uac.c, the
 * same way class/video reads its descriptors, because the descriptors are
 * variable length.
 */
#ifndef USB_AUDIO_H
#define USB_AUDIO_H

/* Audio interface subclass codes (Audio 1.0, table A-2). */
#define AUDIO_SUBCLASS_UNDEFINED      0x00U
#define AUDIO_SUBCLASS_AUDIOCONTROL   0x01U
#define AUDIO_SUBCLASS_AUDIOSTREAMING 0x02U
#define AUDIO_SUBCLASS_MIDISTREAMING  0x03U

/* Audio interface protocol codes (Audio 1.0, table A-3; UAC2 uses 0x20). */
#define AUDIO_PROTOCOL_UNDEFINED 0x00U
#define AUDIO_PROTOCOL_V2        0x20U

/* Class-specific descriptor types (Audio 1.0, table A-4). */
#define AUDIO_CS_INTERFACE_DESCRIPTOR_TYPE 0x24U
#define AUDIO_CS_ENDPOINT_DESCRIPTOR_TYPE  0x25U

/* AudioControl interface descriptor subtypes (Audio 1.0, table A-5). */
#define AUDIO_AC_HEADER_DESCRIPTOR     0x01U
#define AUDIO_AC_INPUT_TERMINAL        0x02U
#define AUDIO_AC_OUTPUT_TERMINAL       0x03U
#define AUDIO_AC_MIXER_UNIT            0x04U
#define AUDIO_AC_SELECTOR_UNIT         0x05U
#define AUDIO_AC_FEATURE_UNIT          0x06U

/* AudioStreaming interface descriptor subtypes (Audio 1.0, table A-6). */
#define AUDIO_AS_GENERAL_DESCRIPTOR    0x01U
#define AUDIO_AS_FORMAT_TYPE_DESCRIPTOR 0x02U

/* AudioStreaming endpoint descriptor subtype (Audio 1.0, table A-7). */
#define AUDIO_EP_GENERAL_DESCRIPTOR 0x01U

/* Class-specific request codes (Audio 1.0, table A-9). */
#define AUDIO_REQUEST_SET_CUR 0x01U
#define AUDIO_REQUEST_GET_CUR 0x81U

/* Terminal types (Audio 1.0, table A-17). */
#define AUDIO_TERMINAL_USB_STREAMING    0x0101U
#define AUDIO_TERMINAL_INPUT_MICROPHONE 0x0201U
#define AUDIO_TERMINAL_OUTPUT_SPEAKER   0x0301U

/* Feature unit control selectors (Audio 1.0, table A-10.2). */
#define AUDIO_FU_MUTE_CONTROL   0x01U
#define AUDIO_FU_VOLUME_CONTROL 0x02U

/* Endpoint control selectors (Audio 1.0, table A-10.3). */
#define AUDIO_EP_SAMPLING_FREQ_CONTROL 0x01U

/* Format type codes (Audio 1.0, table A-14). */
#define AUDIO_FORMAT_TYPE_I 0x01U

/* Format tag for PCM (Audio 1.0, table A-15). */
#define AUDIO_FORMAT_TAG_PCM 0x0001U

/* AudioStreaming endpoint companion bmAttributes (Audio 1.0, A.7.1). */
#define AUDIO_EP_ATTR_SAMPLING_FREQ 0x01U
#define AUDIO_EP_ATTR_PITCH         0x02U

#endif /* USB_AUDIO_H */