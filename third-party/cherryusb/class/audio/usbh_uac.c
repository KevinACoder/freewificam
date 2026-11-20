/*
 * Copyright (c) 2026, zhugengyu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side USB Audio Class 1.0 microphone: enumeration, descriptor census and
 * the control requests of one capture stream.
 *
 * The class deliberately copies every descriptor field it needs while
 * `hport->raw_config_desc` is alive: the core frees that buffer at the end of
 * enumeration, so a census that reads it later would race the hub thread.
 * The camera's AudioControl interface is claimed first (it names the
 * AudioStreaming interface in its header); the AudioStreaming class-info row
 * exists so the core claims that interface too, and its connect is a no-op
 * because the AC pass already parsed both blocks.
 */
#include <stdio.h>

#include "usbh_core.h"
#include "usbh_uac.h"

#undef USB_DBG_TAG
#define USB_DBG_TAG "usbh_uac"
#include "usb_log.h"

#define DEV_FORMAT "/dev/audio%d"

/* General descriptor field offsets (the same style class/video uses: the
 * descriptors are variable length, so fields are read by offset). */
#define DESC_bLength             0
#define DESC_bDescriptorType      1
#define DESC_bDescriptorSubType   2
#define INTF_DESC_bInterfaceNumber   2
#define INTF_DESC_bAlternateSetting  3

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t g_uac_buf[USB_ALIGN_UP(64, CONFIG_USB_ALIGN_SIZE)];

static struct usbh_uac g_uac_class[CONFIG_USBHOST_MAX_AUDIO_CLASS];
static uint32_t g_devinuse;

static struct usbh_uac *usbh_uac_class_alloc(void)
{
    uint8_t devno;

    for (devno = 0; devno < CONFIG_USBHOST_MAX_AUDIO_CLASS; devno++) {
        if ((g_devinuse & (1U << devno)) == 0) {
            g_devinuse |= (1U << devno);
            memset(&g_uac_class[devno], 0, sizeof(struct usbh_uac));
            g_uac_class[devno].minor = devno;
            g_uac_class[devno].stream_intf = 0xff;
            g_uac_class[devno].fu_mute_ch = 0xff;
            g_uac_class[devno].fu_volume_ch = 0xff;
            return &g_uac_class[devno];
        }
    }
    return NULL;
}

static void usbh_uac_class_free(struct usbh_uac *uac)
{
    uint8_t devno = uac->minor;

    if (devno < 32) {
        g_devinuse &= ~(1U << devno);
    }
    memset(uac, 0, sizeof(struct usbh_uac));
}

static void uac_fail(struct usbh_uac *uac, const char *reason)
{
    snprintf(uac->reason, sizeof(uac->reason), "%s", reason);
    uac->error = -USB_ERR_NOTSUPP;
}

static void uac_raw_copy(uint8_t *dst, uint8_t *lenp, const uint8_t *p, uint8_t len)
{
    if ((uint32_t)*lenp + (uint32_t)len <= (uint32_t)CONFIG_USBHOST_UAC_RAW_BYTES) {
        memcpy(dst + *lenp, p, len);
        *lenp = (uint8_t)(*lenp + len);
    }
}

static bool uac_terminal_is_mic(uint16_t type)
{
    /* Microphone, desktop microphone, personal microphone, omni microphone,
     * microphone array: all share the 0x02xx terminal class. */
    return (type & 0xff00U) == 0x0200U;
}

/* Pass 1: the AudioControl header names the AudioStreaming interface. */
static void uac_find_stream_interface(struct usbh_uac *uac)
{
    const uint8_t *p = uac->hport->raw_config_desc;
    uint32_t total = uac->hport->config.config_desc.wTotalLength;
    uint32_t off = 0;
    uint8_t cur_iface = 0xff;

    if (p == NULL) {
        return;
    }
    while ((off + 2U) <= total) {
        uint8_t len = p[DESC_bLength];

        if ((len < 2U) || ((off + len) > total)) {
            break;
        }
        if (p[DESC_bDescriptorType] == USB_DESCRIPTOR_TYPE_INTERFACE) {
            cur_iface = p[INTF_DESC_bInterfaceNumber];
        } else if ((p[DESC_bDescriptorType] == AUDIO_CS_INTERFACE_DESCRIPTOR_TYPE) &&
                   (cur_iface == uac->ctrl_intf) &&
                   (p[DESC_bDescriptorSubType] == AUDIO_AC_HEADER_DESCRIPTOR) &&
                   (len >= 8U)) {
            uac->bcdADC = (uint16_t)((uint16_t)p[3] | ((uint16_t)p[4] << 8));
            if ((p[7] > 0U) && (len >= 9U)) {
                uac->stream_intf = p[8];
            }
            return;
        }
        off += len;
        p += len;
    }
}

/* Pass 2: AudioControl fields (input/output terminal, feature unit). */
static void uac_parse_ac(struct usbh_uac *uac)
{
    const uint8_t *p = uac->hport->raw_config_desc;
    uint32_t total = uac->hport->config.config_desc.wTotalLength;
    uint32_t off = 0;
    uint8_t cur_iface = 0xff;
    bool mic_terminal_seen = false;
    bool any_input_seen = false;

    if (p == NULL) {
        return;
    }
    while ((off + 2U) <= total) {
        uint8_t len = p[DESC_bLength];

        if ((len < 2U) || ((off + len) > total)) {
            break;
        }
        if (p[DESC_bDescriptorType] == USB_DESCRIPTOR_TYPE_INTERFACE) {
            cur_iface = p[INTF_DESC_bInterfaceNumber];
        } else if ((p[DESC_bDescriptorType] == AUDIO_CS_INTERFACE_DESCRIPTOR_TYPE) &&
                   (cur_iface == uac->ctrl_intf)) {
            uac_raw_copy(uac->raw_ac, &uac->raw_ac_len, p, len);

            switch (p[DESC_bDescriptorSubType]) {
                case AUDIO_AC_INPUT_TERMINAL:
                    if (len >= 8U) {
                        uint8_t id = p[3];
                        uint16_t type = (uint16_t)((uint16_t)p[4] | ((uint16_t)p[5] << 8));

                        if (!mic_terminal_seen &&
                            (uac_terminal_is_mic(type) || !any_input_seen)) {
                            /* Prefer the microphone terminal; a first non-mic
                             * terminal is kept only until a mic appears. */
                            uac->ac_input_terminal_id = id;
                            uac->ac_input_terminal_type = type;
                            uac->channels = p[7];
                            if (len >= 10U) {
                                uac->channel_config = (uint16_t)((uint16_t)p[8] | ((uint16_t)p[9] << 8));
                            }
                            any_input_seen = true;
                            mic_terminal_seen = uac_terminal_is_mic(type);
                        }
                    }
                    break;

                case AUDIO_AC_OUTPUT_TERMINAL:
                    if (len >= 9U) {
                        uac->ac_output_terminal_id = p[3];
                        uac->ac_output_terminal_source = p[7];
                    }
                    break;

                case AUDIO_AC_FEATURE_UNIT:
                    if ((len >= 7U) && (p[5] != 0U)) {
                        uint8_t csize = p[5];
                        uint8_t entries = (uint8_t)((len - 6U) / csize);
                        uint8_t i, b;

                        uac->fu_id = p[3];
                        uac->fu_bcontrolsize = csize;
                        for (i = 0; i < entries; i++) {
                            uint32_t bits = 0;

                            for (b = 0; (b < csize) && (b < 4U); b++) {
                                if ((6U + (uint32_t)i * csize + b) < len) {
                                    bits |= (uint32_t)p[6U + (uint32_t)i * csize + b] << (8U * b);
                                }
                            }
                            if ((bits & 0x0001U) != 0U) {
                                if (i == 0U) {
                                    uac->fu_mute_ch = 0;
                                } else if (uac->fu_mute_ch == 0xffU) {
                                    uac->fu_mute_ch = (uint8_t)(i - 1U);
                                }
                            }
                            if ((bits & 0x0002U) != 0U) {
                                if (i == 0U) {
                                    uac->fu_volume_ch = 0;
                                } else if (uac->fu_volume_ch == 0xffU) {
                                    uac->fu_volume_ch = (uint8_t)(i - 1U);
                                }
                            }
                        }
                    }
                    break;

                default:
                    break;
            }
        }
        off += len;
        p += len;
    }
}

/* Pass 2: AudioStreaming general/format blocks and the endpoint companion. */
static void uac_parse_as(struct usbh_uac *uac)
{
    const uint8_t *p = uac->hport->raw_config_desc;
    uint32_t total = uac->hport->config.config_desc.wTotalLength;
    uint32_t off = 0;
    uint8_t cur_iface = 0xff;
    uint8_t cur_alt = 0xff;
    uint8_t ep_attrs[CONFIG_USBHOST_MAX_INTF_ALTSETTINGS];
    bool have_attrs = false;

    memset(ep_attrs, 0, sizeof(ep_attrs));
    if (p == NULL) {
        return;
    }
    while ((off + 2U) <= total) {
        uint8_t len = p[DESC_bLength];

        if ((len < 2U) || ((off + len) > total)) {
            break;
        }
        if (p[DESC_bDescriptorType] == USB_DESCRIPTOR_TYPE_INTERFACE) {
            cur_iface = p[INTF_DESC_bInterfaceNumber];
            cur_alt = p[INTF_DESC_bAlternateSetting];
        } else if ((p[DESC_bDescriptorType] == AUDIO_CS_INTERFACE_DESCRIPTOR_TYPE) &&
                   (cur_iface == uac->stream_intf)) {
            uac_raw_copy(uac->raw_as, &uac->raw_as_len, p, len);

            switch (p[DESC_bDescriptorSubType]) {
                case AUDIO_AS_GENERAL_DESCRIPTOR:
                    if (len >= 7U) {
                        uac->as_terminal_link = p[3];
                        uac->as_format_tag = (uint16_t)((uint16_t)p[5] | ((uint16_t)p[6] << 8));
                    }
                    break;

                case AUDIO_AS_FORMAT_TYPE_DESCRIPTOR:
                    if (len >= 8U) {
                        uint8_t n = p[7];

                        uac->as_format_type = p[3];
                        uac->as_subframe_size = p[5];
                        uac->as_bit_resolution = p[6];
                        uac->as_rate_type = n;
                        if (uac->channels == 0U) {
                            uac->channels = p[4];
                        }
                        if (n == 0U) {
                            if (len >= 14U) {
                                uac->as_rate_min = (uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16);
                                uac->as_rate_max = (uint32_t)p[11] | ((uint32_t)p[12] << 8) | ((uint32_t)p[13] << 16);
                            }
                        } else {
                            for (uint8_t i = 0; (i < n) && (i < CONFIG_USBHOST_UAC_MAX_RATES) &&
                                                ((8U + 3U * (i + 1U)) <= len); i++) {
                                uac->as_rates[i] = (uint32_t)p[8U + 3U * i] |
                                                   ((uint32_t)p[9U + 3U * i] << 8) |
                                                   ((uint32_t)p[10U + 3U * i] << 16);
                                uac->as_num_rates++;
                            }
                        }
                    }
                    break;

                default:
                    break;
            }
        } else if ((p[DESC_bDescriptorType] == AUDIO_CS_ENDPOINT_DESCRIPTOR_TYPE) &&
                   (cur_iface == uac->stream_intf)) {
            uac_raw_copy(uac->raw_as, &uac->raw_as_len, p, len);
            if ((len >= 4U) && (p[DESC_bDescriptorSubType] == AUDIO_EP_GENERAL_DESCRIPTOR) &&
                (cur_alt < CONFIG_USBHOST_MAX_INTF_ALTSETTINGS)) {
                ep_attrs[cur_alt] = p[3];
                have_attrs = true;
            }
        }
        off += len;
        p += len;
    }

    /* The endpoint is the first AudioStreaming alternate setting that carries
     * an isochronous IN endpoint; its companion's attributes say whether the
     * sampling frequency is software-selectable. */
    {
        const struct usbh_interface *intf = &uac->hport->config.intf[uac->stream_intf];

        for (uint8_t alt = 1; alt < intf->altsetting_num; alt++) {
            const struct usb_endpoint_descriptor *ep = &intf->altsetting[alt].ep[0].ep_desc;

            if (ep->bLength == 0U) {
                continue;
            }
            if (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_ISOCHRONOUS) {
                continue;
            }
            if ((ep->bEndpointAddress & 0x80U) == 0U) {
                continue;
            }
            uac->as_alt = alt;
            uac->isoin = (struct usb_endpoint_descriptor *)ep;
            uac->as_mps = (uint16_t)(ep->wMaxPacketSize & USB_MAXPACKETSIZE_MASK);
            uac->as_interval = ep->bInterval;
            uac->as_ep_addr = ep->bEndpointAddress;
            uac->as_ep_in = true;
            if (have_attrs && (alt < CONFIG_USBHOST_MAX_INTF_ALTSETTINGS)) {
                uac->as_sfctrl = (uint8_t)(ep_attrs[alt] & AUDIO_EP_ATTR_SAMPLING_FREQ);
            }
            break;
        }
    }
}

static int uac_parse(struct usbh_uac *uac)
{
    uac_find_stream_interface(uac);

    if (uac->bcdADC != 0x0100U) {
        snprintf(uac->reason, sizeof(uac->reason), "bcdADC=0x%04x is not UAC1",
                 (unsigned)uac->bcdADC);
        return -USB_ERR_NOTSUPP;
    }
    if (uac->protocol != AUDIO_PROTOCOL_UNDEFINED) {
        snprintf(uac->reason, sizeof(uac->reason), "protocol=0x%02x is not UAC1",
                 (unsigned)uac->protocol);
        return -USB_ERR_NOTSUPP;
    }
    if ((uac->stream_intf == 0xffU) ||
        (uac->stream_intf >= CONFIG_USBHOST_MAX_INTERFACES) ||
        (uac->stream_intf == uac->ctrl_intf) ||
        (uac->hport->config.intf[uac->stream_intf].altsetting_num == 0U)) {
        snprintf(uac->reason, sizeof(uac->reason), "no AudioStreaming interface (ac=%u)",
                 (unsigned)uac->ctrl_intf);
        return -USB_ERR_NOTSUPP;
    }

    uac_parse_ac(uac);
    uac_parse_as(uac);

    if (!uac->as_ep_in || (uac->isoin == NULL)) {
        uac_fail(uac, "no isochronous IN endpoint");
        return uac->error;
    }
    if (uac->as_interval != 1U) {
        snprintf(uac->reason, sizeof(uac->reason), "bInterval=%u (full speed needs 1)",
                 (unsigned)uac->as_interval);
        return -USB_ERR_NOTSUPP;
    }
    if ((uac->as_mps == 0U) || (uac->as_mps > 1023U)) {
        snprintf(uac->reason, sizeof(uac->reason), "mps=%u outside 1..1023",
                 (unsigned)uac->as_mps);
        return -USB_ERR_NOTSUPP;
    }
    if (uac->as_format_type != AUDIO_FORMAT_TYPE_I) {
        snprintf(uac->reason, sizeof(uac->reason), "format type %u is not type I",
                 (unsigned)uac->as_format_type);
        return -USB_ERR_NOTSUPP;
    }
    if (uac->as_format_tag != AUDIO_FORMAT_TAG_PCM) {
        snprintf(uac->reason, sizeof(uac->reason), "format tag 0x%04x is not PCM",
                 (unsigned)uac->as_format_tag);
        return -USB_ERR_NOTSUPP;
    }
    if ((uac->as_subframe_size != 2U) || (uac->as_bit_resolution != 16U)) {
        snprintf(uac->reason, sizeof(uac->reason), "subframe=%u bits=%u (this line does 16-bit)",
                 (unsigned)uac->as_subframe_size, (unsigned)uac->as_bit_resolution);
        return -USB_ERR_NOTSUPP;
    }
    if (uac->channels == 0U) {
        uac_fail(uac, "no channel count");
        return uac->error;
    }
    if ((uac->as_num_rates == 0U) && (uac->as_rate_min == 0U)) {
        uac_fail(uac, "no sample rate");
        return uac->error;
    }
    if (!uac_terminal_is_mic(uac->ac_input_terminal_type)) {
        snprintf(uac->reason, sizeof(uac->reason),
                 "input terminal 0x%04x is not a microphone",
                 (unsigned)uac->ac_input_terminal_type);
        return -USB_ERR_NOTSUPP;
    }

    uac->reason[0] = '\0';
    return 0;
}

/* ------------------------------------------------------------------------
 * Control requests
 * --------------------------------------------------------------------- */

static int uac_set_rate(struct usbh_uac *uac, uint32_t rate)
{
    struct usb_setup_packet *setup = uac->hport->setup;
    int ret;

    g_uac_buf[0] = (uint8_t)(rate & 0xffU);
    g_uac_buf[1] = (uint8_t)((rate >> 8) & 0xffU);
    g_uac_buf[2] = (uint8_t)((rate >> 16) & 0xffU);

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_ENDPOINT;
    setup->bRequest = AUDIO_REQUEST_SET_CUR;
    setup->wValue = (uint16_t)(AUDIO_EP_SAMPLING_FREQ_CONTROL << 8);
    setup->wIndex = uac->as_ep_addr;
    setup->wLength = 3;

    ret = usbh_control_transfer(uac->hport, setup, g_uac_buf);
    if (ret < 0) {
        return ret;
    }

    memset(g_uac_buf, 0, 4);
    setup->bmRequestType = USB_REQUEST_DIR_IN | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_ENDPOINT;
    setup->bRequest = AUDIO_REQUEST_GET_CUR;
    setup->wValue = (uint16_t)(AUDIO_EP_SAMPLING_FREQ_CONTROL << 8);
    setup->wIndex = uac->as_ep_addr;
    setup->wLength = 3;
    ret = usbh_control_transfer(uac->hport, setup, g_uac_buf);
    if (ret == 3) {
        uac->get_rate = (uint32_t)g_uac_buf[0] | ((uint32_t)g_uac_buf[1] << 8) |
                        ((uint32_t)g_uac_buf[2] << 16);
        if (uac->get_rate != rate) {
            USB_LOG_WRN("sampling frequency readback %u != %u\r\n",
                        (unsigned)uac->get_rate, (unsigned)rate);
        }
    }
    return 0;
}

int usbh_uac_open(struct usbh_uac *uac, uint32_t rate, uint8_t alt)
{
    struct usb_setup_packet *setup;
    const struct usbh_interface *intf;
    const struct usb_endpoint_descriptor *ep;
    int ret;

    if ((uac == NULL) || (uac->hport == NULL)) {
        return -USB_ERR_INVAL;
    }
    if (uac->error != 0) {
        return uac->error;
    }
    if (uac->is_opened) {
        return 0;
    }

    intf = &uac->hport->config.intf[uac->stream_intf];
    if ((alt == 0U) || (alt >= intf->altsetting_num)) {
        return -USB_ERR_INVAL;
    }
    ep = &intf->altsetting[alt].ep[0].ep_desc;
    if ((ep->bLength == 0U) ||
        (USB_GET_ENDPOINT_TYPE(ep->bmAttributes) != USB_ENDPOINT_TYPE_ISOCHRONOUS) ||
        ((ep->bEndpointAddress & 0x80U) == 0U) ||
        (ep->bInterval != 1U)) {
        return -USB_ERR_NOTSUPP;
    }

    setup = uac->hport->setup;
    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = USB_REQUEST_SET_INTERFACE;
    setup->wValue = alt;
    setup->wIndex = uac->stream_intf;
    setup->wLength = 0;
    ret = usbh_control_transfer(uac->hport, setup, NULL);
    if (ret < 0) {
        USB_LOG_ERR("SET_INTERFACE alt=%u rc=%d\r\n", (unsigned)alt, ret);
        return ret;
    }
    uac->alt_set = true;

    uac->rate_set = 2; /* no sampling-frequency control on this endpoint */
    if (uac->as_sfctrl) {
        ret = uac_set_rate(uac, rate);
        if (ret < 0) {
            /* The camera family is known to refuse a second SET_CUR after a
             * leave; a refused first one means the device streams at its own
             * default.  The capture window's byte count is the evidence. */
            USB_LOG_WRN("SET_CUR sampling frequency %u refused: %d\r\n",
                        (unsigned)rate, ret);
            uac->rate_set = 3;
        } else {
            uac->rate_set = 1;
        }
    }

    uac->isoin = (struct usb_endpoint_descriptor *)ep;
    uac->as_alt = alt;
    uac->as_mps = (uint16_t)(ep->wMaxPacketSize & USB_MAXPACKETSIZE_MASK);
    uac->as_interval = ep->bInterval;
    uac->as_ep_addr = ep->bEndpointAddress;
    uac->rate = rate;
    uac->is_opened = true;

    USB_LOG_INFO("Open audio alt=%u rate=%u mps=%u sfctrl=%u set=%u\r\n",
                 (unsigned)alt, (unsigned)rate, (unsigned)uac->as_mps,
                 (unsigned)uac->as_sfctrl, (unsigned)uac->rate_set);
    return 0;
}

int usbh_uac_close(struct usbh_uac *uac)
{
    struct usb_setup_packet *setup;
    int ret;

    if ((uac == NULL) || (uac->hport == NULL) || !uac->is_opened) {
        return 0;
    }

    setup = uac->hport->setup;
    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = USB_REQUEST_SET_INTERFACE;
    setup->wValue = 0;
    setup->wIndex = uac->stream_intf;
    setup->wLength = 0;
    ret = usbh_control_transfer(uac->hport, setup, NULL);
    uac->is_opened = false;
    uac->alt_set = false;
    return ret;
}

int usbh_uac_get_mute(struct usbh_uac *uac)
{
    struct usb_setup_packet *setup;
    int ret;

    if ((uac == NULL) || (uac->hport == NULL) || (uac->fu_mute_ch == 0xffU)) {
        return -USB_ERR_NOTSUPP;
    }
    setup = uac->hport->setup;
    setup->bmRequestType = USB_REQUEST_DIR_IN | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = AUDIO_REQUEST_GET_CUR;
    setup->wValue = (uint16_t)((AUDIO_FU_MUTE_CONTROL << 8) | uac->fu_mute_ch);
    setup->wIndex = (uint16_t)(((uint16_t)uac->fu_id << 8) | uac->ctrl_intf);
    setup->wLength = 1;
    memset(g_uac_buf, 0, 4);
    ret = usbh_control_transfer(uac->hport, setup, g_uac_buf);
    if (ret != 1) {
        return -USB_ERR_IO;
    }
    uac->mute_known = true;
    uac->muted = (g_uac_buf[0] & 0x01U) != 0U;
    return uac->muted ? 1 : 0;
}

int usbh_uac_set_mute(struct usbh_uac *uac, bool mute)
{
    struct usb_setup_packet *setup;
    int ret;

    if ((uac == NULL) || (uac->hport == NULL) || (uac->fu_mute_ch == 0xffU)) {
        return -USB_ERR_NOTSUPP;
    }
    g_uac_buf[0] = mute ? 1U : 0U;
    setup = uac->hport->setup;
    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = AUDIO_REQUEST_SET_CUR;
    setup->wValue = (uint16_t)((AUDIO_FU_MUTE_CONTROL << 8) | uac->fu_mute_ch);
    setup->wIndex = (uint16_t)(((uint16_t)uac->fu_id << 8) | uac->ctrl_intf);
    setup->wLength = 1;
    ret = usbh_control_transfer(uac->hport, setup, g_uac_buf);
    if (ret < 0) {
        return ret;
    }
    uac->mute_known = true;
    uac->muted = mute;
    return 0;
}

__WEAK void usbh_uac_run(struct usbh_uac *uac)
{
    (void)uac;
}

__WEAK void usbh_uac_stop(struct usbh_uac *uac)
{
    (void)uac;
}

static int usbh_uac_ctrl_connect(struct usbh_hubport *hport, uint8_t intf);
static int usbh_uac_ctrl_disconnect(struct usbh_hubport *hport, uint8_t intf);
static int usbh_uac_streaming_connect(struct usbh_hubport *hport, uint8_t intf);
static int usbh_uac_streaming_disconnect(struct usbh_hubport *hport, uint8_t intf);

static const struct usbh_class_driver audio_ctrl_driver = {
    .driver_name = "audio_ctrl",
    .connect = usbh_uac_ctrl_connect,
    .disconnect = usbh_uac_ctrl_disconnect,
};

static const struct usbh_class_driver audio_streaming_driver = {
    .driver_name = "audio_streaming",
    .connect = usbh_uac_streaming_connect,
    .disconnect = usbh_uac_streaming_disconnect,
};

static int usbh_uac_ctrl_connect(struct usbh_hubport *hport, uint8_t intf)
{
    struct usbh_uac *uac = usbh_uac_class_alloc();
    int ret;

    if (uac == NULL) {
        USB_LOG_ERR("Fail to alloc audio_class\r\n");
        return -USB_ERR_NOMEM;
    }

    uac->hport = hport;
    uac->ctrl_intf = intf;
    uac->protocol = hport->config.intf[intf].altsetting[0].intf_desc.bInterfaceProtocol;
    hport->config.intf[intf].priv = uac;

    ret = uac_parse(uac);
    if (ret < 0) {
        uac->error = ret;
        USB_LOG_ERR("Audio descriptor refused: %s\r\n", uac->reason);
    } else {
        uac->error = 0;
    }

    snprintf(hport->config.intf[intf].devname, CONFIG_USBHOST_DEV_NAMELEN,
             DEV_FORMAT, uac->minor);
    if (uac->error == 0) {
        USB_LOG_INFO("Register Audio Class:%s\r\n", hport->config.intf[intf].devname);
    }

    /* Publish even a refused instance: the application's census prints the
     * descriptor facts and the reason, instead of timing out for a class
     * instance that would never become valid. */
    usbh_uac_run(uac);
    return uac->error;
}

static int usbh_uac_ctrl_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    struct usbh_uac *uac = (struct usbh_uac *)hport->config.intf[intf].priv;

    if (uac != NULL) {
        if (hport->config.intf[intf].devname[0] != '\0') {
            usb_osal_thread_schedule_other();
            USB_LOG_INFO("Unregister Audio Class:%s\r\n", hport->config.intf[intf].devname);
            usbh_uac_stop(uac);
        }
        usbh_uac_class_free(uac);
    }
    return 0;
}

static int usbh_uac_streaming_connect(struct usbh_hubport *hport, uint8_t intf)
{
    (void)hport;
    (void)intf;
    return 0;
}

static int usbh_uac_streaming_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    (void)hport;
    (void)intf;
    return 0;
}

CLASS_INFO_DEFINE const struct usbh_class_info audio_ctrl_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS,
    .bInterfaceClass = USB_DEVICE_CLASS_AUDIO,
    .bInterfaceSubClass = AUDIO_SUBCLASS_AUDIOCONTROL,
    .bInterfaceProtocol = AUDIO_PROTOCOL_UNDEFINED,
    .id_table = NULL,
    .class_driver = &audio_ctrl_driver,
};

CLASS_INFO_DEFINE const struct usbh_class_info audio_streaming_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS,
    .bInterfaceClass = USB_DEVICE_CLASS_AUDIO,
    .bInterfaceSubClass = AUDIO_SUBCLASS_AUDIOSTREAMING,
    .bInterfaceProtocol = AUDIO_PROTOCOL_UNDEFINED,
    .id_table = NULL,
    .class_driver = &audio_streaming_driver,
};