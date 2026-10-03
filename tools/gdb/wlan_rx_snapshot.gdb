# One-shot RTL8821CU RX-stall snapshot. Run only after live shell dumps.
# The current stub cannot reliably detach/resume; cold-boot after capture.
set confirm off
set pagination off
set print pretty on
set architecture aarch64
file build/baremetal-lwip-wlan-gdb/baremetal.elf
set remotetimeout 20
target remote 192.168.0.18:18010
info registers pc sp
p/x rtw_rxdbg_last_comp_ms
p/x rtw_rxdbg_last_ok_ms
p/x rtw_rxdbg_last_frame_ms
p/x rtw_rxdbg_last_ind_ms
p/x g_wlan_last_rx_ms
p/x lkpi_usb_ctrl_stage
p/x lkpi_usb_done_head
p/x lkpi_rx_eps
p/x usbkpi_seq
