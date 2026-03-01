#include "dw8051_sfr.h"

#define INDACC_CMD_READ_REG   1
#define INDACC_CMD_WRITE_REG  3
#define INDACC_CMD_READ_SDS   5
#define INDACC_CMD_WRITE_SDS  7
#define INDACC_CMD_READ_SMI   9
#define INDACC_CMD_WRITE_SMI 11

__sfr   __at(0x96)   CODE_BANK;
__sfr   __at(0x97)   DATA_BANK;
__sfr   __at(0x9a)   SPI_IO_CONF;
__sfr   __at(0x9b)   SPI_RI_CONF;
__sfr   __at(0x9c)   SPI_RI_CMD;
__sfr   __at(0x9d)   SPI_S_CONF;
/* Probably 000adsss, where a=1 if no address sent, d=1 if write, sss=bytes to read (0..4) */
__sfr   __at(0x9e)   SPI_S_ACCESS;
__sfr   __at(0xa0)   INDACC_CMD;
__sfr   __at(0xa1)   INDACC_STATUS;
__sfr   __at(0xa2)   INDACC_ADDR_H;
__sfr   __at(0xa3)   INDACC_ADDR_L;
__sfr16 __at(0xa2a3) INDACC_ADDR;
__sfr   __at(0xa4)   INDACC_WDATA_H;
__sfr   __at(0xa5)   INDACC_WDATA_L;
__sfr16 __at(0xa4a5) INDACC_WDATA;
__sfr   __at(0xa6)   INDACC_RDATA_H;
__sfr   __at(0xa7)   INDACC_RDATA_L;
__sfr16 __at(0xa6a7) INDACC_RDATA;
__sfr   __at(0xb1)   SPI_S_RCMD;
__sfr   __at(0xb2)   SPI_S_WCMD;
/* 8051 XRAM address */
__sfr   __at(0xb3)   PEDMA_E_ADDR_L;
__sfr   __at(0xb4)   PEDMA_E_ADDR_H;
__sfr16 __at(0xb4b3) PEDMA_E_ADDR;
/* NIC peripheral address */
__sfr   __at(0xb5)   PEDMA_P_ADDR_L;
__sfr   __at(0xb6)   PEDMA_P_ADDR_H;
__sfr16 __at(0xb6b5) PEDMA_P_ADDR;
__sfr   __at(0xb7)   PEDMA_LEN;
__sfr   __at(0xb9)   SSPL;
__sfr   __at(0xba)   SSPH;
__sfr16 __at(0xbab9) SSP;
__sfr   __at(0xbb)   SSIO;
__sfr   __at(0xbc)   SPI_DSEL_CYCLE;
