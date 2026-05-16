##==========================================================
## Cmod A7 - CNN MNIST + W5500 + UART + LED
##==========================================================

##----------------------------
## CLOCK 12 MHz
##----------------------------
set_property -dict { PACKAGE_PIN L17 IOSTANDARD LVCMOS33 } [get_ports { clk12mhz }]
create_clock -name clk12mhz -period 83.33 [get_ports { clk12mhz }]

##----------------------------
## PUSH BUTTON RESET
##----------------------------
set_property -dict { PACKAGE_PIN A18 IOSTANDARD LVCMOS33 } [get_ports { btn_rst }]

##----------------------------
## USER LED
##----------------------------
set_property -dict { PACKAGE_PIN A17 IOSTANDARD LVCMOS33 } [get_ports { led0 }]

##----------------------------
## UART
##----------------------------
set_property -dict { PACKAGE_PIN J18 IOSTANDARD LVCMOS33 } [get_ports { uart_txd }]
set_property -dict { PACKAGE_PIN J17 IOSTANDARD LVCMOS33 } [get_ports { uart_rxd }]

##----------------------------
## PMOD JA -> W5500
## ja0_sclk  -> JA1 pin1  G17
## ja1_cs_n  -> JA2 pin2  G19
## ja2_mosi  -> JA3 pin3  N18
## ja3_miso  -> JA4 pin4  L18
## ja4_int_n -> JA7 pin7  H17
## ja5_rst_n -> JA8 pin8  H19
##----------------------------
set_property -dict { PACKAGE_PIN G17 IOSTANDARD LVCMOS33 } [get_ports { ja0_sclk }]
set_property -dict { PACKAGE_PIN G19 IOSTANDARD LVCMOS33 } [get_ports { ja1_cs_n }]
set_property -dict { PACKAGE_PIN N18 IOSTANDARD LVCMOS33 } [get_ports { ja2_mosi }]
set_property -dict { PACKAGE_PIN L18 IOSTANDARD LVCMOS33 } [get_ports { ja3_miso }]
#set_property -dict { PACKAGE_PIN H17 IOSTANDARD LVCMOS33 } [get_ports { ja4_int_n }]
set_property -dict { PACKAGE_PIN H19 IOSTANDARD LVCMOS33 } [get_ports { ja5_rst_n }]

##----------------------------
## SPI output tuning
##----------------------------
set_property SLEW FAST [get_ports { ja0_sclk ja1_cs_n ja2_mosi ja5_rst_n }]
set_property DRIVE 8   [get_ports { ja0_sclk ja1_cs_n ja2_mosi ja5_rst_n }]