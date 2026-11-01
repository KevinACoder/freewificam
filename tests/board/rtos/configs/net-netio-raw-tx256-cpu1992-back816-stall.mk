# K2-16 A arm: OPP procedure vs OPP value.
#
# K2-14 found the defect appears when the case itself pins the clock to the
# 1.992 GHz OPP; nobody separated "the higher clock breaks the transmit path"
# from "the frequency change leaves the machine in a state that breaks it".
# Here the case asks for 1992 at start-up (as the failing arm does) and then
# asks for 816 again after the line is up but before the cell. The cell runs
# at the boot clock.
#
# If it still stalls, the value was never the variable and H5's frequency
# story is wrong; if it runs clean, the damage is in what the procedure does
# (SCMI/PLL state), not in how fast the core runs.
include $(LINE)/configs/net-netio-raw-tx256-stall.mk

CONFIG_LEVERS += -DCONFIG_NETIO_CPU_KHZ=1992000 \
	-DCONFIG_NETIO_CPU_KHZ_LATE=816000
