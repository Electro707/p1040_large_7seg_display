/**
 * Large 7-Segment display controller firmware
 * jb, 2025-06-25
 *
 *
 *
 */

// Important to be defined BEFORE including ETH.h for ETH.begin() to work.
// Example RMII LAN8720 (Olimex, etc.)
// #ifndef ETH_PHY_MDC
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR  0
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER -1        // reset pin
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN
// #endif

#include <SPI.h>
#include <WiFiClient.h>
#include <ETH.h>

#define IO_SHIFT_OE_L       14
#define IO_SHIFT_OE_H       15
#define IO_SHIFT_LDR        13
#define IO_SHIFT_RST        5
#define IO_SHIFT_CLK        12
#define IO_SHIFT_DAT        2
#define IO_DEBUG_LED        33
#define IO_ETH_EN_CLK       32
#define IO_ETH_RST          9

#define N_DISPLAYS          2       // number of 7-segment displays
#define SEG_PER_DISPLAY     8       // number of segments per display (with dot)

static const int spiClk = 1000000;
SPIClass vspi = SPIClass(VSPI);

hw_timer_t *mainTimer = timerBegin(1000000);

static bool eth_connected = false;

// an array of what displays to enable per segment
uint8_t dispPerSeg[SEG_PER_DISPLAY] = {0};

NetworkServer server(23);

void setup(void){
    Serial.begin(115200);
    Serial.println("begin");

    digitalWrite(IO_SHIFT_OE_L, HIGH);
    digitalWrite(IO_SHIFT_OE_H, HIGH);
    // digitalWrite(IO_ETH_RST, LOW);

    pinMode(IO_SHIFT_OE_L, OUTPUT);
    pinMode(IO_SHIFT_OE_H, OUTPUT);
    pinMode(IO_SHIFT_LDR, OUTPUT);
    pinMode(IO_SHIFT_RST, OUTPUT);
    pinMode(IO_SHIFT_CLK, OUTPUT);      // todo: does SPI need it? should it be set on it's own
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

    displayNumber(0);

    // todo: rtos task for this?
    timerAttachInterrupt(mainTimer, &realTimeIsr);
    // timerAlarm(mainTimer, 500, true, 0);                // every 500uS call the real-time interrupt

    // Set your Static IP address
    IPAddress local_IP(192, 168, 20, 184);
    // Set your Gateway IP address
    IPAddress gateway(192, 168, 20, 1);

    IPAddress subnet(255, 255, 255, 0);

    delay(1000);
    Serial.println("begin ether");
    digitalWrite(IO_ETH_EN_CLK, HIGH);
    delay(50);
    digitalWrite(IO_ETH_RST, HIGH);
    delay(10);

    Network.onEvent(onEthernetEvent);
    ETH.setAutoNegotiation(false);
    ETH.setLinkSpeed(10);
    ETH.setFullDuplex(true);
    ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);
    // ETH.config(local_IP, gateway, subnet, INADDR_NONE);
}

NetworkClient c;
void loop(void){
    static uint8_t n = 0;
    digitalWrite(IO_DEBUG_LED, !digitalRead(IO_DEBUG_LED));

    if (server.hasClient()) {
        c = server.accept();
         
        Serial.print("New client: ");
        Serial.println(c.remoteIP());
    }

    if(c && c.connected()){
        if(c.available()){
            Serial.print("-> ");
            while(c.available()){
                Serial.write(c.read());
            }
            Serial.write('\n');
        } 
    }
    else {
        if(c){
            Serial.print("Client no longer");
            c.stop();
        }
    }
    // if(eth_connected){
        // client.println("ABC123");
    // if (localClient.connect(gateway, 23)) {      
    //     while(localClient.connected()) {
    //         localClient.println("A");
    //         Serial.println("Start reading");
    //         if (localClient.available() > 0) {
    //             char c = localClient.read();
    //             Serial.print(c);
    //         }
    //         Serial.println("Stop reading");
    //     }
    // }
    // }
    delay(1000);

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
            Serial.println(ETH);
            eth_connected = true;
            server.begin();
            server.setNoDelay(true);
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
            Serial.println("ETH Lost IP");
            eth_connected = false;
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
}

void showcaseLedCurrent(void){
    digitalWrite(IO_SHIFT_LDR, LOW);
    vspi.transfer(0xFF);
    vspi.transfer(0x01);
    digitalWrite(IO_SHIFT_LDR, HIGH);

    delay(1000);

    digitalWrite(IO_SHIFT_LDR, LOW);
    vspi.transfer(0x03);
    vspi.transfer(0x01);
    digitalWrite(IO_SHIFT_LDR, HIGH);

    delay(1000);
}

void displayNumber(int n){
    // an array of what is displayed. Each value is the enabled segments for that display
    static uint8_t segDat[N_DISPLAYS] = {0};
    // todo: below is copied from another project, do for this one
    const uint8_t numberToSeg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    const int power10[3] = {1, 10, 100};

    // todo: handle converting number to bcd

    unsigned int toS;

    for(int currDisp=0;currDisp<N_DISPLAYS;currDisp++){
        toS = n / power10[currDisp];
        toS %= 10;
        segDat[currDisp] = numberToSeg[toS];           // test, segment A only
    }
    
    // segDat[0] = 0x03;           // test, segment A only

    for(int i=0;i<SEG_PER_DISPLAY;i++){
        dispPerSeg[i] = 0;
        for(int d=0;d<N_DISPLAYS;d++){
            if(segDat[d] & (1 << i)){
                dispPerSeg[i] |= 1 << d;
            }
        }
    }
    
}

uint8_t currentSegment = 0;             // the current segment to be displayed
void realTimeIsr(void){

    digitalWrite(IO_SHIFT_LDR, LOW);
    vspi.transfer(1 << currentSegment);
    vspi.transfer(dispPerSeg[currentSegment]);
    digitalWrite(IO_SHIFT_LDR, HIGH);

    if(++currentSegment >= SEG_PER_DISPLAY){
        currentSegment = 0;
    }
}