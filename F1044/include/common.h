#ifndef COMMON_H
#define COMMON_H

/**
*  FFFFF  L        A       GGGG   SSSS
*  F      L       A A     G      S
*  FFF    L      AAAAA    G  GG   SSS
*  F      L     A     A   G   G      S
*  F      LLL  A       A   GGG   SSSS
**/
// comment the below define out to disable debug print through the serial port
#define ENABLE_DEBUG
// comment below out to enable wifi, otherwise uses ethernet
// #define CONNECT_WIFI
#define CONNECT_ETHERNET

/**
*  M   M  IIIII   SSSS   CCC
*  MM MM    I    S      C
*  M M M    I     SSS   C
*  M   M    I        S  C
*  M   M  IIIII  SSSS    CCC
**/
#define MAX_FW_BUFFER   8192            // enough buffer for firmware updates as well as main comms

#define FW_VERSION "F1044 Rev 1-dev2"
#define FW_VERSION_STR_LEN 0x18

#define NVM_MAGIC       0x5A

#define N_DISPLAYS          6       // number of 7-segment displays
#define SEG_PER_DISPLAY     8       // number of segments per display (with dot)

#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR  0
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER -1        // reset pin
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN

/**
*  N   N  EEEEE  TTTTT  W       W   OOO   RRRR   K   K
*  NN  N  E        T    W       W  O   O  R   R  K KK
*  N N N  EEE      T     W  W  W   O   O  RRRR   KK
*  N  NN  E        T     W W W W   O   O  R R    K KK
*  N   N  EEEEE    T      W   W     OOO   R  R   K   K
**/
#define NETWORK_HOSTNAME "esp32-f1044"

#define TELNET_PORT         23      // for compatiblity with telnet
#define MODBUS_PORT         502

#define TELNET_MAX_CLIENTS     1
#define MODBUS_MAX_CLIENTS     2

/**
*  IIIII   OOO
*    I    O   O
*    I    O   O
*    I    O   O
*  IIIII   OOO
**/
#define IO_SHIFT_OE_CAT       14        // OE_L on schematic, for cathode side of LEDs (each bit is a display)
#define IO_SHIFT_OE_ANA       15        // OE_H on schematic, for anode side of LEDs (each bit is a segment)
#define IO_SHIFT_LDR        13
#define IO_SHIFT_RST        5
#define IO_SHIFT_CLK        12
#define IO_SHIFT_DAT        2
#define IO_DEBUG_LED        33
#define IO_ETH_EN_CLK       32
#define IO_ETH_RST          9

/********** Macros **********/

#ifndef ENABLE_DEBUG
#define DEBUG(_X, ...)
#else
#define DEBUG(_X, ...)       Serial.print("[DBG] "); Serial.printf((_X "\n"), ##__VA_ARGS__)
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

typedef uint32_t uint32;
typedef int32_t int32;
typedef int8_t u8;

#if N_DISPLAYS >= 4
    #define DISPLAY_MODE_TIME_EN
#endif

extern mode_e currMode;
extern timeFormat_e timeFormat;
extern uint currDisplayedN;
extern NetworkClient telnetClient[TELNET_MAX_CLIENTS];
extern char wifiSsid[32];
extern char wifiPassword[32];
extern bool isWifiEnabled;

extern void displayNumber(int n, uint dotBitMap);
extern void setDisplayMode(mode_e newMode);
extern void nvmSave(void);

// used for modbus
extern char fwVersion[FW_VERSION_STR_LEN];

#endif
