/**
 * @file    sdio_cases.h
 * @brief   The SDIO chain bodies, shared by the case table and the console
 *          command.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case body plus a re-run path - the same split the driver images use
 *          (tests/board/threadx-rk3568-rtw88/tests/rtw88_cmds.c re-runs the
 *          case bodies its command file declares here).
 *
 * Each body prints its own anchors and calls board_case_fail() on a failure; the
 * case files register them into the image's table and the command file runs
 * them again on request.
 */

#ifndef SDIO_CASES_H
#define SDIO_CASES_H

#include <stdbool.h>

/**
 * Install this port as the SDIO layer's backend (ports/fsl_sdmmc/lkpi_sdio_fsl.c).
 * Declared here rather than by including the port's own header, which carries the
 * imported stack's declarations the cases have no business seeing: the cases
 * reach the card through the seam and nothing else.
 */
void lkpi_sdio_fsl_install( void );

/**
 * Select the data-path flavour the datapath case's ladder runs under: the
 * controller's own (false), or the board-proven reference implementation's exact
 * shape (true). Declared here and defined by the image's wiring (main.c) for the
 * same reason as lkpi_sdio_fsl_install above - the switch lives in the
 * controller driver, which is not on the cases' include roots.
 *
 * It is a bring-up bisection switch and nothing more: a data phase that never
 * starts has to be told apart from one this project's own additions stopped, and
 * running the ladder under both flavours in one boot is what does that.
 */
void sdio_suite_ref_data_path_set( bool on );

/** Bring the card up, print its identity, and exercise the function's
 *  registers through the seam. */
void sdio_enum_chain( void );

/** Run the card's own power sequence over the seam's single-byte path, and take
 *  an extended read afterwards. The one card-specific body in this suite: the
 *  part answers single-byte transfers from enumeration but refuses extended
 *  ones until its internal power sequence has run. */
void sdio_card_enable_chain( void );

/** Read the card's CIS twice, and write the function's block-size register
 *  through the seam's extended path. */
void sdio_datapath_chain( void );

/** Poll the card's pending-interrupt word and confirm the line stays quiet. */
void sdio_pump_chain( void );

#endif /* SDIO_CASES_H */
