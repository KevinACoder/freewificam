/*
 * @file    wpa_port_glue.h
 * @brief   Internal glue between the wpa port pieces: the event queue lives in
 *          supp_main.c and the eloop only wakes on it.
 *
 * @design producer-consumer queue, port side (DESIGN.md section 1, "event
 *         face" row): producers only enqueue; the eloop thread drains and
 *         hands events to wpa_supplicant_event(), then frees the deep copy.
 */

#ifndef WPA_PORT_GLUE_H_
#define WPA_PORT_GLUE_H_

/* one queued driver event; the producer copies the pointer payloads
 * (supp_main.c), the eloop thread hands it to
 * wpa_supplicant_event() and frees the copy */
struct wpa_supplicant_event_msg {
	void *ctx;
	int event; /* enum wpa_event_type; -1 = wake-up only */
	void *data;
};

/* driver events are queued for the eloop thread */
int wpa_send_event(void *ctx, int event, const void *data);
int wpa_send_dummy_event(void);
void wpa_process_events(void);
void wpa_process_jobs(void);

/* the queue wakeup used by eloop_register_timeout */
void wpa_wake_loop(void);

#endif /* WPA_PORT_GLUE_H_ */
