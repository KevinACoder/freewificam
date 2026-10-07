# tests/host - naming convention

Every host test project lives in a directory named

    <component-or-kernel>-<test-point>-<kind>

where

* **component-or-kernel** is what the project exercises: an OS kernel
  (`threadx`, `freertos`), a component of this repository (`net80211`,
  `linuxkpi-usb`, `skbuff`), or `container` for the mock container's own proof;
* **test-point** is the single thing the project is about - one point per
  project, so a failure names its area at a glance (`cmsis2`, `port`, `frame`,
  `allocation-free`, `urb`);
* **kind** is the weight of the project:
  * `smoke` - a small run that proves the thing is up and wired;
  * `test` - a contract or behaviour assertion suite;
  * `stress` - a combination/injection run with a judge and a repro path.

A test project keeps the name of the container suite that runs it: the suite
registry (`tools/mock/suites.mk`) and this directory use one vocabulary, so a
case line such as `CASE net80211-frame-test/...` leads straight to the project.

| Project | Suite | Point under test |
|---|---|---|
| `threadx-cmsis2-smoke` | `threadx-cmsis2-smoke` | The CMSIS-RTOS2 API over the ThreadX host port |
| `freertos-cmsis2-smoke` | `freertos-cmsis2-smoke` | The CMSIS-RTOS2 API over the FreeRTOS host port |
| `net80211-port-test` | `net80211-port-test` | The net80211 port layer: stack compile, include discipline, ThreadX run, isolation gates |
| `net80211-frame-test` | `net80211-frame-test` | 802.11 frame formats and the wlan protocol path |
| `linuxkpi-usb-test` | `linuxkpi-usb-test` | The linuxkpi USB API face over the backend seam (synthetic controller + scripted device) |
| `skbuff-allocation-free-stress` | `skbuff-allocation-free-stress` | The skbuff seam under allocation/free combination stress |
| `linuxkpi-usb-urb-stress` | `linuxkpi-usb-urb-stress` | The urb engine under injection stress |

Rules:

* One project, one point: do not grow a second unrelated point inside an
  existing project - a new mock point gets its own project and suite.
* A suite declaration points at exactly one project directory; the container
  never carries a second copy of a project's build (see `tools/mock/README.md`).
* Projects that only prove a compile-time contract live in the project that
  consumes them (for example the include-closure checks run in
  `linuxkpi-usb-test`'s Makefile), not in a directory of their own.
