/*
 * @file   scmi_clk.h
 * @brief  SCMI clock probe (BL31 SMC carrier) - boot-time entry (N7 R5b).
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#ifndef BOARD_RK3568_ITX_SCMI_CLK_H
#define BOARD_RK3568_ITX_SCMI_CLK_H

/* Read-only protocol probes + freq decode (+ optional boot rate set), all
 * reported through board_log. Safe from any task context; never call from
 * an ISR (the SMC can take milliseconds inside BL31). */
void scmi_boot_probe(void);

#endif /* BOARD_RK3568_ITX_SCMI_CLK_H */
