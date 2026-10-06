# threadx-regression (Gate1 suite)

The kernel API face on the host: Eclipse ThreadX's own regression suite, built from the
vendored tree at the pinned commit and run on its Linux userspace port. The tests come
with the kernel, so the expected values are the upstream framework's own.

## How it runs

`build.sh` configures the upstream CMake entry into a build directory under this suite and
builds it; `run-tests.sh` then executes every test the build registered, one process each,
and reports one case per test.

Two details matter:

* The build directory is outside the vendored tree. The upstream entry derives its paths
  from its own location, so an external `-B` directory leaves the checkout byte-identical -
  the container's zero-pollution check depends on it.
* `-DBUILD_SHARED_LIBS=ON` is part of the upstream invocation and is required here: the
  coverage configurations attach the profiling flags as private link options of the kernel
  library, which a static archive does not carry to the executables.

## Judging

A test passes when it exits 0 and its summary line reports zero failures and zero system
errors; the one test that brings its own `main()` reports with a single `SUCCESS!` line
instead. Every test runs once - there is no repeat and no retry, because washing a red away
is exactly what this gate exists to prevent. A test that fails is a finding: its raw output
is kept under `build/<configuration>/logs/`.

`THREADX_REGRESSION_CONFIG` selects one of the kernel's five test configurations, and
`THREADX_REGRESSION_TEST_TIMEOUT` the per-test time limit. The default configuration is the
one upstream's CI uses for its first pass; the other four follow in a later round.
