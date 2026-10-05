/*
 * @file   cherrysh_adapter.h
 * @brief  Board-side entry point for the CherrySH adapter.
 *
 * The shell is not a CMSIS component, so it has no standard header to be
 * reached through; this is the one declaration the application needs.
 *
 * cherrysh_init() MUST be called after the scheduler is running, from a
 * task. Two reasons, both about the console interrupt:
 *
 *  - The adapter's RX path ends in osThreadFlagsSetFromISR, which requires
 *    a running scheduler.
 *  - The adapter arms the console receiver as its last act, after which a
 *    byte can raise that interrupt at any moment.
 *
 * Calling it before osKernelStart() would arm the receiver with no
 * scheduler behind it. A task that calls this and then sleeps forever is
 * the correct pattern (app/hello_world/main.c's shellboot task).
 *
 * Returns 0 on success, non-zero if the shell could not be created - in
 * which case the console is left as a plain output path and the system
 * still runs.
 */
#ifndef FREEWEBCAM_CHERRYSH_ADAPTER_H
#define FREEWEBCAM_CHERRYSH_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

int cherrysh_init(void);

/*
 * CherrySH hands a command its shell handle as argv[argc + 1] instead of a
 * first argument (see the vendored builtin/help.c). Command implementations
 * that live outside this file - other adapters export their own commands
 * into the same FSymTab - need that detail too, so it lives here rather
 * than being copied into each one.
 */
#define CSH_FROM_ARGV(argc, argv) ((chry_shell_t *)(void *)(argv)[(argc) + 1])

#ifdef __cplusplus
}
#endif

#endif /* FREEWEBCAM_CHERRYSH_ADAPTER_H */
