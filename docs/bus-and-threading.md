# Bus layers and the threading model

## Bus interface layers

The Linux drivers reach their hardware through the kernel's bus APIs. This
project provides a first-party face for each bus, with the actual controller
behind a narrow backend seam, so the same driver bytes compile against a small
in-repo interface instead of the kernel's.

| Bus | Interface face | Backend |
|---|---|---|
| USB | `components/linuxkpi/usb/` (the urb engine and the host-controller interface) | `ports/cherryusb/` over the vendored CherryUSB host stack (EHCI / xHCI) |
| PCIe | `components/linuxkpi/pcie/` (in progress) | a first-party controller driver |
| SDIO | `components/linuxkpi/sdio/` (the byte and block transfer path, card identity and the interrupt machinery) | `ports/fsl_sdmmc/` |

USB and SDIO are each exercised by a board line today. The PCIe interface layer
is designed and drafted but not yet brought up on hardware; see
[known-limitations.md](known-limitations.md).

The shape of each layer is the same: a facade that presents the kernel API, an
engine that implements the parts that are the same for every controller (the
urb model, the descriptor walking, the error mapping), and a narrow seam
between them that a backend implements. A backend that needs something extra
(staged DMA buffers, a specific alignment) installs it behind the seam rather
than changing the engine.

## The threading model

The kernel is reached only through CMSIS-RTOS2, and only from the `os/` port
layer. Everything above it -- the components, the ports, the application, the
tests -- calls the kernel exclusively through `cmsis_os2.h` plus the project's
extension header. This is enforced, not advised: the components must build and
link on both kernels, and a gate checks the compiled objects for foreign kernel
symbols. A direct kernel call is not a shortcut, it is a portability defect.

Concretely:

- Threads are created through CMSIS-RTOS2 and given priorities in the project's
  own scale. The board glue maps those onto the underlying kernel's priorities.
- The drivers run on their own threads. A driver's work that was a workqueue or
  a threaded interrupt in Linux becomes a thread here, and the interface layer
  serialises it the way the interface promises.
- Completion and notification use CMSIS-RTOS2 primitives (semaphores, message
  queues, event flags), never the kernel's own.
- Critical sections are expressed through the project's CMSIS-RTOS2 extension
  header, which maps onto the right primitive for the kernel in use. Interrupt
  state is saved and restored around a critical section rather than assumed.
- The receive path hands packets to the network layer on the driver's thread;
  the presentation layer is called from there, and the interface's own
  serialisation rules apply.

The reason for a single abstraction rather than one port per kernel: the port
layer itself becomes a reusable, upstreamable backend, and the portability
requirement becomes a build gate rather than a promise.
