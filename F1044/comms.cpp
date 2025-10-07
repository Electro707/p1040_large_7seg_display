#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <ETH.h>
#include "common.h"
#include "comms.h"
#include "zones.h"

#define UPDATE_NOCRYPT
#include <Update.h>

// yes this is shared for the two communication modes, which is OK
// only one should be updating at a time
uint8_t fwBuffer[MAX_FW_BUFFER];
uint fwBufferIdx;


ParserHandler::ParserHandler(void){
    msgIdx = 0;
    prevByteEscape = false;
    prevCmdWill = false;
    prevCmdDo = false;
    lastReceivedInterrupt = false;
    expectedFwBytes = 0;
}

void ParserHandler::setPrintClass(Print *pH){
    printHandler = pH;
}

// handles reception of a byte at a time
void ParserHandler::parse(char b){
    if(expectedFwBytes){
        handleFirmwareUpdate(b);
        return;
    }

    if(prevByteEscape){
        prevByteEscape = false;
        switch(b){
            case 0xFF:
                parseTelnetDetokenized(b);      // double escape 0xFF-0xFF is just 0xFF
                break;
            case 0xFB:
                prevCmdWill = true;
                break;
            case 0xFD:
                prevCmdDo = true;
                break;
            case 0xF4:      // ctrl-c
                lastReceivedInterrupt = true;
                break;
            default:
                DEBUG("unhandled telnet command %d", b);
                break;
        }
    }
    else if(prevCmdWill){
        prevCmdWill = false;
        DEBUG("Received WILL token: %d", b);
    }
    else if(prevCmdDo){
        prevCmdDo = false;
        DEBUG("Received DO token: %d", b);
        if(b == 0x06){
            printHandler->print("\xFF\xFB\x06");
            if(lastReceivedInterrupt){
                lastReceivedInterrupt = false;
                msgIdx = 0;
                printHandler->print("\r\n");
            }
            
        }
    }
    else{
        if(b == 0xFF){
            prevByteEscape = true;
        } else {
            parseTelnetDetokenized(b);
        }
        
    }
}

void ParserHandler::handleFirmwareUpdate(char b){
    fwBuffer[fwBufferIdx++] = b;
    if(--expectedFwBytes == 0){
        DEBUG("received fw bytes");
        // we are done, can ACK and call the update function
        Update.write(fwBuffer, fwBufferIdx);
        txAck();
        fwBufferIdx = 0;        // to prevent accidental future from the consumed buffer
    }
}

// handles receiving bytes after de-tokenized by the parser
void ParserHandler::parseTelnetDetokenized(char b){
    if(b == '\n' || b == '\r'){
        if(msgIdx){
            msg[msgIdx] = 0;
            processCommand();
            msgIdx = 0;
        }
    }
    else{
        if(msgIdx < MAX_MSG_LEN){
            msg[msgIdx++] = b;
        }
    }
}

void ParserHandler::txAck(void){
    printHandler->println("ok");
}

void ParserHandler::txNack(const char *errMsg){
    printHandler->print("error: ");
    printHandler->println(errMsg);
}

// a helper macro to get the next argument, and if empty print error message
// this assumes the char* variable is named `token`
#define MACRO_GET_NEXTARG(_ERR_MSG) \
    token = strtok(NULL, " "); \
    if(token == NULL){ \
            txNack(_ERR_MSG);    \
            return; \
    }

void ParserHandler::processCommand(void){
    DEBUG("<- %s", msg);

    char *token = strtok(msg, " ");

    if(token == NULL){
        txNack("missing command");
        return;
    }

    if(!strcmp(token, "ping")) {
        printHandler->println("pong!");
    }
    else if (!strcmp(token, "exit")) {
        if(ethClient.connected()){
            ethClient.stop();
        }
    }
    else if (!strcmp(token, "get")) {
        subcommandGet(token);
    }
    else if (!strcmp(token, "set")) {
        subcommandSet(token);
    }
    else if (!strcmp(token, "update")) {
        subcommandUpdate(token);
    }
    else if (!strcmp(token, "nvmSave")) {
        nvmSave();
        txAck();
    }
    else if (!strcmp(token, "reboot")) {
        txAck();
        delay(1000);
        ESP.restart();
    }
    else {
        printHandler->println("error: invalid command");
    }
}

void ParserHandler::subcommandSet(char *token){
    int32_t tmpLong;
    
    token = strtok(NULL, " ");
    if(token == NULL){
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
        else {
            txNack("invalid mode");
            return;
        }

        txAck();
    }
    else if(!strcmp(token, "timeFormat")){
        MACRO_GET_NEXTARG("missing arg1");

        if(!strcmp(token, "24hr")){
            timeFormat = TIME_FORMAT_24HR;
        }
        else if(!strcmp(token, "12hr")){
            timeFormat = TIME_FORMAT_12HR;
        }
        else if(!strcmp(token, "metric")){
            timeFormat = TIME_FORMAT_METRIC;
        }
        else{
            txNack("invalid mode");
            return;
        }
        txAck();
    }
    else if (!strcmp(token, "n")) {
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
        displayNumber(tmpLong, 0);
        txAck();
    }
    else if (!strcmp(token, "wifiSSID")) {
        token = strtok(NULL, "");
        if(token == NULL){
            txNack("SSID not given");
            return; \
        }
        if(strlen(token) > 31){
            txNack("SSID too long, must be <31 characters");
            return;
        }
        strcpy(wifiSsid, token);
        txAck();
        // if(WiFi.status() == WL_IDLE_STATUS){
        //     printHandler->println("restarting wifi");
        //     WiFi.disconnect(true);
        //     WiFi.begin(wifiSsid, wifiPassword);
        // }
    }
    else if (!strcmp(token, "wifiPass")) {
        token = strtok(NULL, "");
        if(token == NULL){
            txNack("Password not given");
            return; \
        }
        if(strlen(token) > 31){
            txNack("Password too long, must be <31 characters");
            return;
        }
        strcpy(wifiPassword, token);
        txAck();
        // if(WiFi.status() == WL_IDLE_STATUS){
        //     printHandler->println("restarting wifi");
        //     WiFi.disconnect(true);
        //     WiFi.begin(wifiSsid, wifiPassword);
        // }
    }
    else if (!strcmp(token, "wifiOff")) {
        if(isWifiEnabled){
            printHandler->println("stopping wifi");
            WiFi.disconnect(true);
        }else{
            printHandler->println("wifi already off");
        }
    }
    else if (!strcmp(token, "wifiOn")) {
        if(!isWifiEnabled){
            printHandler->println("starting wifi");
            WiFi.begin(wifiSsid, wifiPassword);
        }else{
            printHandler->println("wifi already on");
        }
    }
    else if(!strcmp(token, "timeZone")){
        MACRO_GET_NEXTARG("missing arg1");
        const char *zone = micro_tz_db_get_posix_str(token);
        if(zone == NULL){
            txNack("Invalid zone");
            return;
        }
        setenv("TZ", zone, 1);
        tzset();
        txAck();
    }
    else {
        txNack("invalid sub-command");
    }
}

void ParserHandler::subcommandGet(char *token){
    token = strtok(NULL, " ");
    if(token == NULL){
        txNack("missing sub-arg");
        return;
    }

    if(!strcmp(token, "version")){
        printHandler->println(FW_VERSION);
    }
    else if(!strcmp(token, "mode")){
        switch(currMode){
            case DISPLAY_MODE_OFF:
                printHandler->println("off");
                break;
            case DISPLAY_MODE_NUMB:
                printHandler->println("numb");
                break;
#ifdef DISPLAY_MODE_TIME_EN
            case DISPLAY_MODE_TIME:
                printHandler->println("time");
                break;
#endif
            default:
                printHandler->println("Mode not defined");
                break;
        }
    }
    else if(!strcmp(token, "timeFormat")){
        switch(timeFormat){
            case TIME_FORMAT_24HR:
                printHandler->println("24hr");
                break;
            case TIME_FORMAT_12HR:
                printHandler->println("12hr");
                break;
            case TIME_FORMAT_METRIC:
                printHandler->println("metric");
                break;
            default:
                printHandler->println("Mode not defined");
                break;
        }
    }
    else if(!strcmp(token, "n")){
        printHandler->println(currDisplayedN);
    }
    else if(!strcmp(token, "ip")){
        bool conn = false;
        if(WiFi.isConnected()){
            conn = true;
            printHandler->print("Wifi: ");
            printHandler->println(WiFi.localIP().toString().c_str());
        }
        if(ETH.connected()){
            conn = true;
            printHandler->print("Eth: ");
            printHandler->println(ETH.localIP().toString().c_str());
        }
        if(!conn){
            printHandler->println("none");
        }
    }
    else if(!strcmp(token, "wifiInfo")){
        printHandler->print("SSID: ");
        printHandler->println(wifiSsid);
        printHandler->print("Password: ");
        printHandler->println(wifiPassword);
    }
    else if(!strcmp(token, "allTimeZones")){
        for(int i=0;i<N_TIME_ZONES;i++){
            printHandler->println(micro_tz_db_tzs[i].name);
        }
        printHandler->println("---");
    }
    // else if(!strcmp(token, "timeZone")){
    //     txNack("TODO: THIS");
    // }
    else{
        txNack("invalid sub-command");
    }
}

void ParserHandler::subcommandUpdate(char *token){
    uint32_t tmpLong;
    uint8_t stat;

    token = strtok(NULL, " ");
    if(token == NULL){
        txNack("missing sub-arg");
        return;
    }

    if(!strcmp(token, "begin")){
        token = strtok(NULL, " ");
        if(token == NULL){
            txNack("missing arg1");
            return;
        }
        tmpLong = atol(token);
        if(tmpLong == 0){
            txNack("zero update size");
            return;
        }
        stat = Update.begin(tmpLong);
        if(!stat){
            txNack("failed to init update");
            return;
        }
        txAck();
    }
    else if(!strcmp(token, "cont")){
        token = strtok(NULL, " ");
        if(token == NULL){
            txNack("missing arg1");
            return;
        }
        tmpLong = atol(token);
        if(tmpLong == 0){
            txNack("zero update size");
            return;
        }
        if(tmpLong > MAX_FW_BUFFER){
            txNack("beyond max size");
            return;
        }
        fwBufferIdx = 0;
        expectedFwBytes = tmpLong;
        txAck();
    }
    else if(!strcmp(token, "end")){
        stat = Update.end(true);
        if(stat){
            txAck();
        } else {
            printHandler->println("error when finishing firmware");
        }
    }
    else if(!strcmp(token, "cancel")){
        Update.abort();
        txAck();
        DEBUG("end: %d", expectedFwBytes);
    }
    else{
        txNack("invalid sub-command");
    }
}