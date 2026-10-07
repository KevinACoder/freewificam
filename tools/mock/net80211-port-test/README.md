# net80211-port-contract

The Gate 1 suite over the net80211 port layer: the imported stack translation
units and the project's port layer (`ports/net80211/`) are compiled, linked and
run on the host runtime.

## Cases

| Case | What it proves |
|---|---|
| `stack-compile` | The seventeen imported stack translation units compile with zero warnings under the port's include discipline |
| `headers` | The include discipline is checked mechanically: shape names come from the component's compat tree, machinery from the port's compat tree (the port test directory's `headers` target) |
| `link-threadx` | Stack + port + smoke + stub driver link and run on the ThreadX host runtime (the smoke's own anchors must pass) |
| `gates` | The kernel-isolation gates pass over the port sources (no kernel primitive reachable from component code) |

## Run shape

The wrapper `check.sh` calls the port test directory's Makefile targets one by
one and maps each into a case line; a failing target keeps its log under the
container's build directory and the suite exits non-zero.

The suite declares its 802.11 truth routing in `suite.mk` (the container
refuses a wireless suite without a route annotation).

## Scope

This is the executable first slice.  The complete clause inventory of the port
contract - one case per declared clause, with the deferred clauses named as
deferred rather than silently absent - belongs to the port-contract round that
follows the port's own specification revision; until then this suite keeps the
build, link and isolation contract green on every container run.
