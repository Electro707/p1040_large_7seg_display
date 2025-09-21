#ifndef COMMON_H
#define COMMON_H

// comment the below define out to disable debug print through the serial port
#define ENABLE_DEBUG
// comment below out to enable wifi, otherwise uses ethernet
#define CONNECT_WIFI
#define CONNECT_ETHERNET

#define FW_VERSION "F1044 Rev 1-dev2"
#define NETWORK_HOSTNAME "esp32-f1044"

#define NETWORK_PORT        23      // for compatiblity with telnet

#define N_DISPLAYS          4       // number of 7-segment displays
#define SEG_PER_DISPLAY     8       // number of segments per display (with dot)

#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR  0
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER -1        // reset pin
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN

#define IO_SHIFT_OE_L       14
#define IO_SHIFT_OE_H       15
#define IO_SHIFT_LDR        13
#define IO_SHIFT_RST        5
#define IO_SHIFT_CLK        12
#define IO_SHIFT_DAT        2
#define IO_DEBUG_LED        33
#define IO_ETH_EN_CLK       32
#define IO_ETH_RST          9

#define MAX_FW_BUFFER   8192            // enough buffer for firmware updates as well as main comms

/********** Macros **********/

#ifndef ENABLE_DEBUG
#define DEBUG(_X, ...)
#else
#define DEBUG(_X, ...)       Serial.printf((_X "\n"), ##__VA_ARGS__)
#endif

/********** Enums and Structs **********/
typedef enum{
    DISPLAY_MODE_OFF = 0,
    DISPLAY_MODE_NUMB,
    DISPLAY_MODE_TIME
}mode_e;

typedef enum{
    TIME_FORMAT_24HR,
    TIME_FORMAT_12HR,
    TIME_FORMAT_METRIC
}timeFormat_e;

#if N_DISPLAYS >= 4 
    #define DISPLAY_MODE_TIME_EN
#endif

extern mode_e currMode;
extern timeFormat_e timeFormat;
extern uint currDisplayedN;
extern NetworkClient ethClient;

extern void displayNumber(int n, uint dotBitMap);
extern void setDisplayMode(mode_e newMode);

#endif
