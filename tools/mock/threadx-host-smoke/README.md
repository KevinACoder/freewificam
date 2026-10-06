# threadx-host-smoke (Gate1 suite)

The ThreadX runtime face on the host: the kernel runs as an ordinary Linux process through
its own userspace port, with the CMSIS-RTOS2 backend in front of it. This suite wraps the
smoke test that lives under `tests/host/threadx-cmsis2-smoke/` - the executable is built by
its own Makefile and is not duplicated here.

Cases: the process exit code plus the six anchors the smoke prints (kernel running, the
three queue messages in order, a tick that advanced by at least five, and its PASS line).

What it proves: the scheduler runs, threads are created and switched, a message queue
carries data between them, and the tick advances - without a board.

What it does not cover: the kernel API semantics themselves (queues under every option,
mutex inheritance, pool behaviour). That is the regression suite next to it, which runs
the upstream tests over the same port.
