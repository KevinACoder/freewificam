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

/**
 * Install this port as the SDIO layer's backend (ports/fsl_sdmmc/lkpi_sdio_fsl.c).
 * Declared here rather than by including the port's own header, which carries the
 * imported stack's declarations the cases have no business seeing: the cases
 * reach the card through the seam and nothing else.
 */
void lkpi_sdio_fsl_install( void );

/** Bring the card up, print its identity, and exercise the function's
 *  registers through the seam. */
void sdio_enum_chain( void );

/** Read the card's CIS twice, and write the function's block-size register
 *  through the seam's extended path. */
void sdio_datapath_chain( void );

/** Poll the card's pending-interrupt word and confirm the line stays quiet. */
void sdio_pump_chain( void );

#endif /* SDIO_CASES_H */
