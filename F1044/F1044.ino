/**
 * Large 7-Segment display controller firmware
 * jb, 2025-07-05
 *
 * Some examples used for this application
 * - https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/WiFiTelnetToSerial/WiFiTelnetToSerial.ino
 *
 */
#include <string.h>
#include <SPI.h>
#include <WiFiClient.h>
#include <ETH.h>
#include <WiFi.h>
#include <EEPROM.h>

#include "common.h"
#include "comms.h"
#include "wifiDefault.h"
#include "micro_tz_d/zones.h"

#define UPDATE_NOCRYPT
#include <Update.h>

char wifiSsid[32];
char wifiPassword[32];

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 3600;  // Set your timezone offset (e.g., -5*3600 for EST)
const int daylightOffset_sec = 3600;   // Daylight savings (if applicable)
struct tm currTime;

static const int spiClk = 1000000;
SPIClass vspi = SPIClass(VSPI);

hw_timer_t* mainTimer = timerBegin(1000000);

EEPROMClass nvm("main");

static bool eth_connected = false;

// an array of what displays to enable per segment
uint8_t dispPerSeg[SEG_PER_DISPLAY] = { 0 };

NetworkServer server(NETWORK_PORT);

uint currDisplayedN = 0;  // the current number being displayed. Only to be updated in displayNumber

mode_e currMode;
timeFormat_e timeFormat;

TimerHandle_t updateTimeT;

bool isWifiEnabled = false;

NetworkClient ethClient;  // for now only allow one client

// parser struct for our custom parser
ParserHandler serialParser;
ParserHandler networkParser;

void setup(void) {
	Serial.begin(115200);
	DEBUG("begin");

	digitalWrite(IO_SHIFT_OE_L, HIGH);
	digitalWrite(IO_SHIFT_OE_H, HIGH);

	pinMode(IO_SHIFT_OE_L, OUTPUT);
	pinMode(IO_SHIFT_OE_H, OUTPUT);
	pinMode(IO_SHIFT_LDR, OUTPUT);
	pinMode(IO_SHIFT_RST, OUTPUT);
	pinMode(IO_SHIFT_CLK, OUTPUT);  // todo: does SPI need it? should it be set on it's own
	pinMode(IO_SHIFT_DAT, OUTPUT);
	pinMode(IO_ETH_EN_CLK, OUTPUT);
	pinMode(IO_ETH_RST, OUTPUT);
	pinMode(IO_DEBUG_LED, OUTPUT);

	// todo: for some reason digitalWrite before pinMode doesn't set the IO state. Look into
	digitalWrite(IO_SHIFT_OE_L, HIGH);
	digitalWrite(IO_SHIFT_OE_H, HIGH);
	digitalWrite(IO_ETH_RST, LOW);

	vspi.begin(IO_SHIFT_CLK, -1, IO_SHIFT_DAT, -1);
	vspi.setHwCs(false);
	vspi.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));

	// reset LED segments on bootup
	digitalWrite(IO_SHIFT_RST, HIGH);
	digitalWrite(IO_SHIFT_LDR, LOW);
	vspi.transfer(0x00);
	vspi.transfer(0x00);
	digitalWrite(IO_SHIFT_LDR, HIGH);
	digitalWrite(IO_SHIFT_OE_L, LOW);
	digitalWrite(IO_SHIFT_OE_H, LOW);

	// startup NVM read
	nvmInit();
	nvmLoad();

	updateTimeT = xTimerCreate("updateTimeT", pdMS_TO_TICKS(500), true, NULL, updateTimeCallback);

	timeFormat = TIME_FORMAT_24HR;
	// displayNumber(0);
	setDisplayMode(DISPLAY_MODE_TIME);

	// todo: rtos task for this?
	timerAttachInterrupt(mainTimer, &realTimeIsr);
	timerAlarm(mainTimer, 1000, true, 0);  // every 500uS call the real-time interrupt

	delay(1000);
	DEBUG("begin ether");
	digitalWrite(IO_ETH_EN_CLK, HIGH);
	delay(50);
	digitalWrite(IO_ETH_RST, HIGH);
	delay(10);

	Network.onEvent(onNetworkEvent);
	WiFi.setHostname(NETWORK_HOSTNAME);
	WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
	ETH.setHostname(NETWORK_HOSTNAME);
	ETH.setAutoNegotiation(false);
	ETH.setLinkSpeed(10);
	ETH.setFullDuplex(true);


#ifdef CONNECT_ETHERNET
	ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);
#endif
#ifdef CONNECT_WIFI
	WiFi.begin(wifiSsid, wifiPassword);
#endif

	serialParser.setPrintClass(&Serial);

	DEBUG("postBegin");
}

void loop(void) {
	static uint8_t n = 0;
	// digitalWrite(IO_DEBUG_LED, !digitalRead(IO_DEBUG_LED));

	// handle new client connections
	if (server.hasClient()) {
		if (ethClient.connected()) {
			server.accept().stop();
		} else {
			ethClient = server.accept();
			networkParser.setPrintClass(&ethClient);
			DEBUG("New client: %s", ethClient.remoteIP().toString().c_str());
		}
	}

	// client read loop
	if (ethClient.connected()) {
		while (ethClient.available()) {
			networkParser.parse(ethClient.read());
		}
	}

	while (Serial.available()) {
		serialParser.parse(Serial.read());
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

// WARNING: onEvent is called from a separate FreeRTOS task (thread)!
void onNetworkEvent(arduino_event_id_t event) {
	// todo: clean up the case statements as needed
	// todo: handle if we have wifi connected and ethernet at the same time?
	switch (event) {
		case ARDUINO_EVENT_ETH_START:
			DEBUG("ETH Started");
			// The hostname must be set after the interface is started, but needs
			// to be set before DHCP, so set it from the event handler thread.
			ETH.setHostname("esp32-ethernet");
			break;
		case ARDUINO_EVENT_ETH_CONNECTED:
			DEBUG("ETH Connected");
#ifdef CONNECT_WIFI
            DEBUG("Disconnecting WIFI");
            WiFi.disconnect(true);
#endif
			break;
		case ARDUINO_EVENT_ETH_GOT_IP:
			DEBUG("ETH Got IP %s", ETH.localIP().toString().c_str());
			eth_connected = true;
			configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
			server.begin();
			server.setNoDelay(true);
			break;
		case ARDUINO_EVENT_ETH_LOST_IP:
			DEBUG("ETH Lost IP");
			eth_connected = false;
			server.end();
			break;
		case ARDUINO_EVENT_ETH_DISCONNECTED:
			DEBUG("ETH Disconnected");
			eth_connected = false;
            // restart WiFi back up
#ifdef CONNECT_WIFI
            DEBUG("Restarting WIFI");
            WiFi.begin(wifiSsid, wifiPassword);
#endif
			break;
		case ARDUINO_EVENT_ETH_STOP:
			DEBUG("ETH Stopped");
			eth_connected = false;
			break;
		
		case ARDUINO_EVENT_WIFI_OFF:				 DEBUG("Wifi is set to off"); break;
		case ARDUINO_EVENT_WIFI_READY:               DEBUG("WiFi interface ready"); break;
		case ARDUINO_EVENT_WIFI_SCAN_DONE:           DEBUG("Completed scan for access points"); break;
		case ARDUINO_EVENT_WIFI_STA_START:
			DEBUG("WiFi client started");
			isWifiEnabled = true;
			break;
		case ARDUINO_EVENT_WIFI_STA_STOP:
			DEBUG("WiFi client stopped");
			isWifiEnabled = false;
			break;
		case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            DEBUG("Connected to WiFi access point");
            break;
		case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            DEBUG("Disconnected from WiFi access point");
            break;
		case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: DEBUG("Authentication mode of access point has changed"); break;
		case ARDUINO_EVENT_WIFI_STA_GOT_IP:
			DEBUG("Obtained Wifi IP address: %s", WiFi.localIP().toString().c_str());
			configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
			server.begin();
			server.setNoDelay(true);
			break;
		case ARDUINO_EVENT_WIFI_STA_LOST_IP:
			DEBUG("Lost Wifi IP address and IP address is reset to 0");
			server.end();
			break;
		case ARDUINO_EVENT_WPS_ER_SUCCESS:          DEBUG("WiFi Protected Setup (WPS): succeeded in enrollee mode"); break;
		case ARDUINO_EVENT_WPS_ER_FAILED:           DEBUG("WiFi Protected Setup (WPS): failed in enrollee mode"); break;
		case ARDUINO_EVENT_WPS_ER_TIMEOUT:          DEBUG("WiFi Protected Setup (WPS): timeout in enrollee mode"); break;
		case ARDUINO_EVENT_WPS_ER_PIN:              DEBUG("WiFi Protected Setup (WPS): pin code in enrollee mode"); break;

		case ARDUINO_EVENT_WIFI_AP_START:           DEBUG("WiFi access point started"); break;
		case ARDUINO_EVENT_WIFI_AP_STOP:            DEBUG("WiFi access point  stopped"); break;
		case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    DEBUG("Client connected"); break;
		case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: DEBUG("Client disconnected"); break;
		case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   DEBUG("Assigned IP address to client"); break;
		case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:  DEBUG("Received probe request"); break;
		case ARDUINO_EVENT_WIFI_AP_GOT_IP6:         DEBUG("AP IPv6 is preferred"); break;
		case ARDUINO_EVENT_WIFI_STA_GOT_IP6:        DEBUG("STA IPv6 is preferred"); break;
		default:                                    DEBUG("Other eth/wifi event: %d", event); break;
	}

	digitalWrite(IO_DEBUG_LED, eth_connected);
}

void nvmSave(void){
    nvm.writeByte(0, NVM_MAGIC);
    nvm.writeBytes(1, wifiSsid, 32);
    nvm.writeBytes(1+32, wifiPassword, 32);
    nvm.commit();
}

void nvmLoad(void){
    // we have an empty nvm, initialize
	if(nvm.read(0) != NVM_MAGIC){
		strcpy(wifiSsid, DEFAULT_WIFI_SSID);
		strcpy(wifiPassword, DEFAULT_WIFI_PASSWORD);
		return;
	}
	nvm.readBytes(1, wifiSsid, 32);
	nvm.readBytes(1+32, wifiPassword, 32);
}

void nvmInit(){
	if (!nvm.begin(0x200)) {
		// todo: this came from their example. is restarting the best?
		DEBUG("Failed to initialize nvm");
		DEBUG("Restarting...");
		delay(1000);
		ESP.restart();
	}
}

void updateTimeCallback(TimerHandle_t xTimer) {
  uint currTimeN;
  uint dots;
  time_t now;
  float tmp;

  // basically doing the same as getLocalTime in `esp32-hal-time.c`, but no timeout. if it fails it fails
  time(&now);
  localtime_r(&now, &currTime);
  if (currTime.tm_year > (2016 - 1900)) {
	switch (timeFormat) {
	  case TIME_FORMAT_24HR:
		currTimeN = currTime.tm_min + (currTime.tm_hour * 100);
		break;
	  case TIME_FORMAT_12HR:
		currTimeN = currTime.tm_min + ((currTime.tm_hour % 12) * 100);
		break;
	  case TIME_FORMAT_METRIC:
		// get the current time as a proportion of the day
		tmp = currTime.tm_sec + (60.0 * (float)currTime.tm_min) + (3600.0 * (float)currTime.tm_hour);
		tmp /= 86400;
		// now start dividing and filling the hours and minutes spot
		tmp *= 10;
		currTimeN = (uint)floor(tmp);
		tmp -= currTimeN;
		currTimeN *= 100;  // convert the hours to the 100's decimal place
		tmp *= 100;
		currTimeN += (uint)floor(tmp);
		break;
	}

	dots = 0;
	if(currTime.tm_sec & 0b01){
		dots = (1 << 2);
	}

	displayNumber(currTimeN, dots);
  }
}

// handles the transition in the display mode
void setDisplayMode(mode_e newMode) {
  currMode = newMode;

  if (newMode == DISPLAY_MODE_TIME) {
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

void displayNumber(int n, uint dotBitMap) {
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
	if(dotBitMap & (1 << currDisp)){
		segDat[currDisp] |= 1 << 7;
	}
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

	digitalWrite(IO_SHIFT_OE_L, HIGH);

	digitalWrite(IO_SHIFT_LDR, HIGH);
	vspi.transfer(1 << currentSegment);
	vspi.transfer(dispPerSeg[currentSegment]);
	digitalWrite(IO_SHIFT_LDR, LOW);
	digitalWrite(IO_SHIFT_LDR, HIGH);

	delayMicroseconds(10);
	digitalWrite(IO_SHIFT_OE_L, LOW);

	if(++currentSegment >= SEG_PER_DISPLAY){
		currentSegment = 0;
	}
}