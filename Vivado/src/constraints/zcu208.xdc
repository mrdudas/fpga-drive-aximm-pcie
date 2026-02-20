#----------------------------------------------------------------------------------------
# Constraints for Opsero FPGA Drive FMC Gen4 ref design for ZCU208 using 2x SSDs
#----------------------------------------------------------------------------------------

# SSD1 PCI Express reset (perst_0)
set_property PACKAGE_PIN F21 [get_ports {perst_0[0]}]; # LA00_CC_P
set_property IOSTANDARD LVCMOS18 [get_ports {perst_0[0]}]

# SSD2 PCI Express reset (perst_1)
set_property PACKAGE_PIN C20 [get_ports {perst_1[0]}]; # LA04_P
set_property IOSTANDARD LVCMOS18 [get_ports {perst_1[0]}]

# SSD1 PE Detect (pedet_0, not connected in the design)
# set_property PACKAGE_PIN F22 [get_ports {pedet_0[0]}]; # LA00_CC_N
# set_property IOSTANDARD LVCMOS18 [get_ports {pedet_0[0]}]

# SSD2 PE Detect (pedet_1, not connected in the design)
# set_property PACKAGE_PIN B20 [get_ports {pedet_1[0]}]; # LA04_N
# set_property IOSTANDARD LVCMOS18 [get_ports {pedet_1[0]}]

# Disable signal for 3.3V power supply for SSD2 (disable_ssd2_pwr)
set_property PACKAGE_PIN C23 [get_ports disable_ssd2_pwr]; # LA07_P
set_property IOSTANDARD LVCMOS18 [get_ports disable_ssd2_pwr]

##############################
# PCIe reference clock 100MHz
##############################

# SSD1 ref clock
set_property PACKAGE_PIN U33 [get_ports {ref_clk_0_clk_p[0]}]; # GBTCLK0_M2C_P
set_property PACKAGE_PIN U34 [get_ports {ref_clk_0_clk_n[0]}]; # GBTCLK0_M2C_N
create_clock -period 10.000 -name ref_clk_0_clk_p -waveform {0.000 5.000} [get_ports ref_clk_0_clk_p]

# SSD2 ref clock
set_property PACKAGE_PIN P31 [get_ports {ref_clk_1_clk_p[0]}]; # GBTCLK1_M2C_P
set_property PACKAGE_PIN P32 [get_ports {ref_clk_1_clk_n[0]}]; # GBTCLK1_M2C_N
create_clock -period 10.000 -name ref_clk_1_clk_p -waveform {0.000 5.000} [get_ports ref_clk_1_clk_p]

############################
# SSD1 Gigabit transceivers
############################

set_property LOC GTYE4_CHANNEL_X0Y12 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_0.*" && NAME =~ ".*channel_inst\[0\].*" }]
set_property LOC GTYE4_CHANNEL_X0Y13 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_0.*" && NAME =~ ".*channel_inst\[1\].*" }]
set_property LOC GTYE4_CHANNEL_X0Y14 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_0.*" && NAME =~ ".*channel_inst\[2\].*" }]
set_property LOC GTYE4_CHANNEL_X0Y15 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_0.*" && NAME =~ ".*channel_inst\[3\].*" }]

############################
# SSD2 Gigabit transceivers
############################

set_property LOC GTYE4_CHANNEL_X0Y16 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_1.*" && NAME =~ ".*channel_inst\[0\].*" }]
set_property LOC GTYE4_CHANNEL_X0Y17 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_1.*" && NAME =~ ".*channel_inst\[1\].*" }]
set_property LOC GTYE4_CHANNEL_X0Y18 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_1.*" && NAME =~ ".*channel_inst\[2\].*" }]
set_property LOC GTYE4_CHANNEL_X0Y19 [get_cells -hierarchical -regexp -filter { PRIMITIVE_TYPE =~ ADVANCED.GT.GTYE4_CHANNEL && PARENT =~  ".*xdma_1.*" && NAME =~ ".*channel_inst\[3\].*" }]

############################
# SSD1 PCIe block
############################

set_property LOC PCIE4CE4_X0Y0 [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ ADVANCED.PCIE.* && NAME =~ "*xdma_0*" }]

############################
# SSD2 PCIe block
############################

set_property LOC PCIE4CE4_X0Y1 [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ ADVANCED.PCIE.* && NAME =~ "*xdma_1*" }]

# The following LOC and USER_CLOCK_ROOT constraints correct a timing issue that 
# started happening in the ZCU208 dual design with the 2022.1 version of Vivado.
# This forum post led to the solution:
# https://support.xilinx.com/s/question/0D52E00006nAusUSAS/what-can-i-do-to-fix-a-max-skew-violation-on-the-pcie-pipeclk-port-i-consistently-have-pulse-width-violations-that-are-related-to-this-clock-the-rest-of-the-design-meets-timing?language=en_US
# We simply copied the positions of these two BUFG_GTs from the same design in version 2020.2
# and used the USER_CLOCK_ROOT property to assign the output nets to the same CLOCK_ROOT.
set_property LOC BUFG_GT_X0Y161 [get_cells *_i/xdma_0/inst/pcie4c_ip_i/inst/fpgadrv_xdma_0_0_pcie4c_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/bufg_gt_coreclk]
set_property LOC BUFG_GT_X0Y164 [get_cells *_i/xdma_0/inst/pcie4c_ip_i/inst/fpgadrv_xdma_0_0_pcie4c_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/bufg_gt_pclk]
set_property USER_CLOCK_ROOT X0Y5 [get_nets *_i/xdma_0/inst/pcie4c_ip_i/inst/fpgadrv_xdma_0_0_pcie4c_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/CLK_CORECLK]
set_property USER_CLOCK_ROOT X0Y5 [get_nets *_i/xdma_0/inst/pcie4c_ip_i/inst/fpgadrv_xdma_0_0_pcie4c_ip_gt_top_i/diablo_gt.diablo_gt_phy_wrapper/phy_clk_i/CLK_PCLK2_GT]

