# Suite declaration: the UVC host streaming engine against a scripted device.
#
# The suite binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it. It compiles the engine the UVC
# patch adds (class/video/usbh_uvc_stream.c, replayed from patches/cherryusb/
# 0004) with host doubles for the class-facing seams and asserts the payload
# header state machine (frame-id / end-of-frame / error bits), the frame
# boundaries both channels carry, the buffer-pool behaviour and the
# alternate-setting selection against the board camera's descriptor ladder;
# the wrapper adds the red control.

SUITE_NAME := uvc-stream-test
SUITE_TIMEOUT := 120
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/uvc-stream-test
SUITE_RUN := sh tools/mock/uvc-stream-test/check.sh
SUITE_TRUTH_SOURCE := the UVC payload header semantics (UVC 1.0/1.1 section 2.4.3.4: bHeaderLength, the frame-id / end-of-frame / error bits) and the board camera's ground-truth descriptor ladder (alternate settings 1..11 carrying 192/384/512/640/800/944/1280/1600/1984/2688/3060 bytes per frame)
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a
