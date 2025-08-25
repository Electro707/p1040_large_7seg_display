/**
 * Large 7-Segment display controller firmware
 * jb, 2025-07-05
 *
 * Some examples used for this application
 * - https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/WiFiTelnetToSerial/WiFiTelnetToSerial.ino
 *
 */

#include "common.h"
#include <string.h>
#include <SPI.h>
#include <WiFiClient.h>
#include <ETH.h>

#define UPDATE_NOCRYPT
#include <Update.h>

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -5*3600;      // Set your timezone offset (e.g., -5*3600 for EST)
const int   daylightOffset_sec = 3600; // Daylight savings (if applicable)
struct tm currTime;

static const int spiClk = 1000000;
SPIClass vspi = SPIClass(VSPI);

hw_timer_t *mainTimer = timerBegin(1000000);

static bool eth_connected = false;

// an array of what displays to enable per segment
uint8_t dispPerSeg[SEG_PER_DISPLAY] = { 0 };

NetworkServer server(NETWORK_PORT);

uint currDisplayedN = 0;  // the current number being displayed. Only to be updated in displayNumber

mode_e currMode;
timeFormat_e timeFormat;

uint8_t fwBuffer[MAX_FW_BUFFER];
uint fwBufferIdx = 0;
uint expectedFwBytes = 0;

TimerHandle_t updateTimeT;

void setup(void) {
    Serial.begin(115200);
    Serial.println("begin");

    digitalWrite(IO_SHIFT_OE_L, HIGH);
    digitalWrite(IO_SHIFT_OE_H, HIGH);
    // digitalWrite(IO_ETH_RST, LOW);

    pinMode(IO_SHIFT_OE_L, OUTPUT);
    pinMode(IO_SHIFT_OE_H, OUTPUT);
    pinMode(IO_SHIFT_LDR, OUTPUT);
    pinMode(IO_SHIFT_RST, OUTPUT);
    pinMode(IO_SHIFT_CLK, OUTPUT);  // todo: does SPI need it? should it be set on it's own
    pinMode(IO_SHIFT_DAT, OUTPUT);
    pinMode(IO_ETH_EN_CLK, OUTPUT);
    pinMode(IO_ETH_RST, OUTPUT);

    digitalWrite(IO_SHIFT_OE_L, HIGH);
    digitalWrite(IO_SHIFT_OE_H, HIGH);
    digitalWrite(IO_ETH_RST, LOW);

    pinMode(IO_DEBUG_LED, OUTPUT);

    vspi.begin(IO_SHIFT_CLK, -1, IO_SHIFT_DAT, -1);
    vspi.setHwCs(false);
    vspi.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));

    // test the LED segment by putting on one on

    digitalWrite(IO_SHIFT_RST, HIGH);
    digitalWrite(IO_SHIFT_LDR, LOW);
    vspi.transfer(0x00);
    vspi.transfer(0x00);
    digitalWrite(IO_SHIFT_LDR, HIGH);

    digitalWrite(IO_SHIFT_OE_L, LOW);
    digitalWrite(IO_SHIFT_OE_H, LOW);

    updateTimeT = xTimerCreate("updateTimeT", pdMS_TO_TICKS(500), true, NULL, updateTimeCallback);

    timeFormat = TIME_FORMAT_24HR;
    setDisplayMode(DISPLAY_MODE_NUMB);
    displayNumber(0);

    // todo: rtos task for this?
    timerAttachInterrupt(mainTimer, &realTimeIsr);
    timerAlarm(mainTimer, 500, true, 0);  // every 500uS call the real-time interrupt

    delay(1000);
    Serial.println("begin ether");
    digitalWrite(IO_ETH_EN_CLK, HIGH);
    delay(50);
    digitalWrite(IO_ETH_RST, HIGH);
    delay(10);

    Network.onEvent(onEthernetEvent);
    ETH.setHostname(NETWORK_HOSTNAME);
    ETH.setAutoNegotiation(false);
    ETH.setLinkSpeed(10);
    ETH.setFullDuplex(true);
    ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);

    DEBUG("postBegin");
}

NetworkClient c;  // for now only allow one client

#define MAX_MSG_LEN 64
char networkMsg[MAX_MSG_LEN];
uint networkMsgIdx = 0;

void loop(void) {
  static uint8_t n = 0;
  // digitalWrite(IO_DEBUG_LED, !digitalRead(IO_DEBUG_LED));

  // handle new client connections
  if (server.hasClient()) {
    if (c.connected()) {
      server.accept().stop();
    } else {
      c = server.accept();

      Serial.print("New client: ");
      Serial.println(c.remoteIP());
    }
  }

  // client read loop
  if (c.connected()) {
    if (c.available()) {
      parseNetworkData();
    }
  }

  // delay(1000);

  // delay(100);
  // displayNumber(n);
  // if(++n > 99) n = 0;

  // int x = 1;
  // for(int i=0;i<7;i++){
  //     digitalWrite(IO_SHIFT_LDR, LOW);
  //     vspi.transfer(x);
  //     vspi.transfer(0x01);
  //     digitalWrite(IO_SHIFT_LDR, HIGH);

  //     x <<= 1;
  //     delay(500);
  // }

  // showcaseLedCurrent();
}

void parseNetworkData(void) {
  static bool prevByteEscape = false;
  static bool prevCmdWill = false;
  static bool prevCmdDo = false;
  static bool lastReceivedInterrupt = false;

  while (c.available()) {
    char b = c.read();

    if (expectedFwBytes) {
      handleFirmwareUpdate(b);
      return;
    }

    if (prevByteEscape) {
      prevByteEscape = false;
      switch (b) {
        case 0xFF:
          parseTelnetDetokenized(b);  // double escape 0xFF-0xFF is just 0xFF
          break;
        case 0xFB:
          prevCmdWill = true;
          break;
        case 0xFD:
          prevCmdDo = true;
          break;
        case 0xF4:  // ctrl-c
          lastReceivedInterrupt = true;
          break;
        default:
          DEBUG("unhandled telnet command %d", b);
          break;
      }
    } else if (prevCmdWill) {
      prevCmdWill = false;
      DEBUG("Received WILL token: %d", b);
    } else if (prevCmdDo) {
      prevCmdDo = false;
      DEBUG("Received DO token: %d", b);
      if (b == 0x06) {
        c.print("\xFF\xFB\x06");
        if (lastReceivedInterrupt) {
          lastReceivedInterrupt = false;
          networkMsgIdx = 0;
          c.print("\r\n");
        }
      }
    } else {
      if (b == 0xFF) {
        prevByteEscape = true;
      } else {
        parseTelnetDetokenized(b);
      }
    }
  }
}

void handleFirmwareUpdate(char b) {
  fwBuffer[fwBufferIdx++] = b;
  if (--expectedFwBytes == 0) {
    DEBUG("received fw bytes");
    // we are done, can ACK and call the update function
    Update.write(fwBuffer, fwBufferIdx);
    txAck();
    fwBufferIdx = 0;  // to prevent accidental future from the consumed buffer
  }
}

// handles receiving bytes after de-tokenized by the parser
void parseTelnetDetokenized(char b) {
  if (b == '\n' || b == '\r') {
    if (networkMsgIdx) {
      networkMsg[networkMsgIdx] = 0;
      processCommand();
      networkMsgIdx = 0;
    }
  } else {
    if (networkMsgIdx < MAX_MSG_LEN) {
      networkMsg[networkMsgIdx++] = b;
    }
  }
}

#define MACRO_GET_NEXTARG(_ERR_MSG) \
  token = strtok(NULL, " "); \
  if (token == NULL) { \
    txNack(_ERR_MSG); \
    return; \
  }

void txAck(void) {
  c.println("ok");
}

void txNack(const char *errMsg) {
  c.print("error: ");
  c.println(errMsg);
}

void processCommand(void) {
  DEBUG("<- %s", networkMsg);

  char *token = strtok(networkMsg, " ");
  if (token == NULL) {
    txNack("missing command");
    return;
  }

  if (!strcmp(networkMsg, "ping")) {
    c.println("pong!");
  } else if (!strcmp(networkMsg, "exit")) {
    if (c.connected()) {
      c.stop();
    }
  } else if (!strcmp(token, "get")) {
    subcommandGet(token);
  } else if (!strcmp(token, "set")) {
    subcommandSet(token);
  } else if (!strcmp(token, "update")) {
    subcommandUpdate(token);
  } else if (!strcmp(token, "reboot")) {
    txAck();
    delay(1000);
    ESP.restart();
  } else {
    c.println("error: invalid command");
  }
}

void subcommandSet(char *token) {
  int32_t tmpLong;

  token = strtok(NULL, " ");
  if (token == NULL) {
    txNack("missing sub-arg");
    return;
  }

  if (!strcmp(token, "mode")) {
    MACRO_GET_NEXTARG("missing arg1");

    if (!strcmp(token, "off")) {
      setDisplayMode(DISPLAY_MODE_OFF);
    } 
    else if (!strcmp(token, "numb")) {
      setDisplayMode(DISPLAY_MODE_NUMB);
    }
#ifdef DISPLAY_MODE_TIME_EN
    else if (!strcmp(token, "time")) {
      setDisplayMode(DISPLAY_MODE_TIME);
    }
#endif
    // else if(!strcmp(token, "time")){
    //     setDisplayMode(DISPLAY_MODE_TIME);
    // }
    else {
      txNack("invalid mode");
      return;
    }

    txAck();
  } else if (!strcmp(token, "n")) {
    MACRO_GET_NEXTARG("missing arg1");
    // only allow in NUMB mode
    if (currMode != DISPLAY_MODE_NUMB) {
      txNack("not in 'numb' mode");
      return;
    }

    tmpLong = atol(token);
    if (tmpLong < 0) {
      txNack("number negative");
      return;
    }
    // todo: make this dynamic based off the N_DISPLAY variable
    if (((int)(log10(tmpLong)) + 1) > N_DISPLAYS) {
      txNack("number too big");
      return;
    }
    displayNumber(tmpLong);
  } else {
    txNack("invalid sub-command");
  }
}

void subcommandGet(char *token) {
    token = strtok(NULL, " ");
    if (token == NULL) {
        txNack("missing sub-arg");
        return;
    }

    if (!strcmp(token, "version")) {
        c.println(FW_VERSION);
    } else if (!strcmp(token, "mode")) {
        switch (currMode) {
        case DISPLAY_MODE_OFF:
            c.println("off");
            break;
        case DISPLAY_MODE_NUMB:
            c.println("numb");
            break;
#ifdef DISPLAY_MODE_TIME_EN
        case DISPLAY_MODE_TIME:
            c.println("time");
            break;
#endif
        default:
            c.println("Mode not defined");
            break;
        }
    } else if (!strcmp(token, "n")) {
        c.println(currDisplayedN);
    } else if (!strcmp(token, "ip")) {
        if (ETH.connected()) {
        c.println(ETH.localIP().toString().c_str());
        } else {
        c.println("none");
        }
    } else {
        txNack("invalid sub-command");
    }
}

void subcommandUpdate(char *token) {
  uint32_t tmpLong;
  uint8_t stat;

  token = strtok(NULL, " ");
  if (token == NULL) {
    txNack("missing sub-arg");
    return;
  }

  if (!strcmp(token, "begin")) {
    token = strtok(NULL, " ");
    if (token == NULL) {
      txNack("missing arg1");
      return;
    }
    tmpLong = atol(token);
    if (tmpLong == 0) {
      txNack("zero update size");
      return;
    }
    stat = Update.begin(tmpLong);
    if (!stat) {
      txNack("failed to init update");
      return;
    }
    txAck();
  } else if (!strcmp(token, "cont")) {
    token = strtok(NULL, " ");
    if (token == NULL) {
      txNack("missing arg1");
      return;
    }
    tmpLong = atol(token);
    if (tmpLong == 0) {
      txNack("zero update size");
      return;
    }
    if (tmpLong > MAX_FW_BUFFER) {
      txNack("beyond max size");
      return;
    }
    fwBufferIdx = 0;
    expectedFwBytes = tmpLong;
    txAck();
  } else if (!strcmp(token, "end")) {
    stat = Update.end(true);
    if (stat) {
      txAck();
    } else {
      txNack("error when finishing firmware");
    }
  } else if (!strcmp(token, "cancel")) {
    Update.abort();
    txAck();
    DEBUG("end: %d", expectedFwBytes);
  } else {
    txNack("invalid sub-command");
  }
}

// WARNING: onEvent is called from a separate FreeRTOS task (thread)!
void onEthernetEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      // The hostname must be set after the interface is started, but needs
      // to be set before DHCP, so set it from the event handler thread.
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("ETH Got IP");
    //   Serial.println(ETH);
      eth_connected = true;
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      server.begin();
      server.setNoDelay(true);
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println("ETH Lost IP");
      eth_connected = false;
      server.end();
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }

  digitalWrite(IO_DEBUG_LED, eth_connected);
}

void updateTimeCallback(TimerHandle_t xTimer){
    uint currTimeN;
    time_t now;
    float tmp;

    // basically doing the same as getLocalTime in `esp32-hal-time.c`, but no timeout. if it fails it fails
    time(&now);
    localtime_r(&now, &currTime);
    if (currTime.tm_year > (2016 - 1900)) {
        switch(timeFormat){
            case TIME_FORMAT_24HR:
                currTimeN = currTime.tm_min + (currTime.tm_hour*100);
                break;
            case TIME_FORMAT_12HR:
                currTimeN = currTime.tm_min + ((currTime.tm_hour % 12)*100);
                break;
            case TIME_FORMAT_METRIC:
                // get the current time as a proportion of the day
                tmp = currTime.tm_sec + (60.0*(float)currTime.tm_min) + (3600.0*(float)currTime.tm_hour);
                tmp /= 86400;
                // now start dividing and filling the hours and minutes spot
                tmp *= 10;
                currTimeN = (uint)floor(tmp);
                tmp -= currTimeN;
                currTimeN *= 100;       // convert the hours to the 100's decimal place
                tmp *= 100;
                currTimeN += (uint)floor(tmp);
                break;
        }
        
        displayNumber(currTimeN);
    }
}

// handles the transition in the display mode
void setDisplayMode(mode_e newMode) {
    currMode = newMode;

    if(newMode == DISPLAY_MODE_TIME){
        xTimerStart(updateTimeT, 0);
    } else {
        xTimerStop(updateTimeT, 0);
    }

    if (newMode == DISPLAY_MODE_OFF) {
        for (int i = 0; i < SEG_PER_DISPLAY; i++) {
        dispPerSeg[i] = 0;
        }
    }
}

// a test I did to demonstrate to myself that the display must be driven one segment at at time due to current limiter per display
// void showcaseLedCurrent(void){
//     digitalWrite(IO_SHIFT_LDR, LOW);
//     vspi.transfer(0xFF);
//     vspi.transfer(0x01);
//     digitalWrite(IO_SHIFT_LDR, HIGH);

//     delay(1000);

//     digitalWrite(IO_SHIFT_LDR, LOW);
//     vspi.transfer(0x03);
//     vspi.transfer(0x01);
//     digitalWrite(IO_SHIFT_LDR, HIGH);

//     delay(1000);
// }

void displayNumber(int n) {
  // an array of what is displayed. Each value is the enabled segments for that display
  static uint8_t segDat[N_DISPLAYS] = { 0 };
  // todo: below is copied from another project, do for this one
  const uint8_t numberToSeg[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
  const int power10[N_DISPLAYS] = { 1, 10, 100, 1000 };

  // todo: handle converting number to bcd

  uint toS;

  for (uint currDisp = 0; currDisp < N_DISPLAYS; currDisp++) {
    toS = n / power10[currDisp];
    toS %= 10;
    segDat[currDisp] = numberToSeg[toS];  // test, segment A only
  }

  // segDat[0] = 0x03;           // test, segment A only

  for (uint i = 0; i < SEG_PER_DISPLAY; i++) {
    dispPerSeg[i] = 0;
    for (int d = 0; d < N_DISPLAYS; d++) {
      if (segDat[d] & (1 << i)) {
        dispPerSeg[i] |= 1 << d;
      }
    }
  }

  currDisplayedN = n;  // update global variable
}

uint8_t currentSegment = 0;  // the current segment to be displayed
void realTimeIsr(void) {

  digitalWrite(IO_SHIFT_LDR, LOW);
  vspi.transfer(1 << currentSegment);
  vspi.transfer(dispPerSeg[currentSegment]);
  digitalWrite(IO_SHIFT_LDR, HIGH);

  if (++currentSegment >= SEG_PER_DISPLAY) {
    currentSegment = 0;
  }
}