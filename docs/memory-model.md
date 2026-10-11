# The memory model

Memory is where this kind of port usually breaks: a shim that allocates on one
side and frees on the other, with no shared allocator, produces the classic
use-after-free and double-free failures that only surface under load. The model
here is explicit about who owns what.

## The heap

The system heap is TLSF (a vendored two-level segregated-fit allocator) over a
fixed region. A fixed region rather than a growing heap keeps the failure mode
predictable: exhaustion is a named failure, not a corrupt neighbouring object.
The allocator is reached through the standard allocation names, bound in the
`os/` layer, so nothing above it knows which allocator is in use.

## Packet buffers

Two packet types meet at the driver/stack boundary, and the bridge owns the
conversion:

- the Linux drivers allocate **skbs** (the Linux socket-buffer shape), from a
  first-party pool (`components/linuxkpi/skbuff/`);
- the 802.11 core uses **mbufs** (the BSD shape), from the port layer's
  presentation.

The bridge converts between them at the boundary. Both pools carry **named
counters** (allocations, frees, and the misuse categories: double free, stray
free, write through a released pointer). The counters exist so a leak or a
misuse is a number a test can assert on, not a mystery that appears as a hang
three minutes later.

## Ownership rules

- **Allocate and free on the same side.** A buffer allocated by the driver is
  freed by the driver, and a buffer allocated by the stack is freed by the
  stack. The bridge moves ownership explicitly at the boundary, once.
- **A completion callback runs before the buffer is released.** The interface
  layer unhooks a buffer from its owner before handing it to a callback, so a
  callback that submits the buffer again cannot race the release.
- **A guard uses the space that is actually writable.** When copying into a
  packet buffer, the check is against the space from the write position to the
  end of the buffer, not the buffer's nominal size; an alignment offset in
  between makes those differ, and using the nominal size writes past the end.

## Why this is documented

Every one of these rules is the answer to a failure that was found the hard
way: a pool slot released twice, a completion arriving after the buffer was
returned, a copy that used the wrong bound. They are written down so the next
port does not rediscover them. The pools' counters and a host stress suite
(`skbuff-allocation-free-stress`) are the mechanical guard.
