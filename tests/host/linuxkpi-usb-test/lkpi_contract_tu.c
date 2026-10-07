/*
 * Import build contract: the linuxkpi contract-header closure must compile on the
 * host and stay closed over the repository (checked with -H by the Makefile).
 *
 * Compile-only by design. It includes <linux/usb.h> alone, instantiates the API
 * shapes the host mock suites and the later linuxkpi body consume, and takes the
 * address of the two lifecycle functions so their declarations are proven usable.
 * Nothing is linked or run.
 */
#include <linux/usb.h>

struct urb		contract_urb;
struct usb_device	contract_usb_device;
struct usb_interface	contract_usb_interface;

void *const contract_usb_alloc_urb = (void *)usb_alloc_urb;
void *const contract_usb_free_urb  = (void *)usb_free_urb;
