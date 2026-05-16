`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 06.04.2026 13:00:27
// Design Name: 
// Module Name: cnn_bram_portb32_bridge
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////

module cnn_bram_portb32_bridge (
    input  wire        clk,
    input  wire        rst_n,

    // lato CNN HLS
    input  wire [5:0]  img_words_address0,
    input  wire        img_words_ce0,
    output wire [31:0] img_words_q0,

    // lato BRAM PORTB
    output wire [31:0] addrb,
    output wire        clkb,
    output wire        enb,
    output wire [3:0]  web,
    output wire [31:0] dinb,
    input  wire [31:0] doutb,
    output wire        rstb
);

    // HLS fornisce indice parola, BRAM vuole indirizzo byte
    assign addrb        = {24'd0, img_words_address0, 2'b00};

    assign clkb         = clk;
    assign enb          = img_words_ce0;
    assign web          = 4'b0000;
    assign dinb         = 32'd0;
    assign img_words_q0 = doutb;
    assign rstb         = ~rst_n;

endmodule
