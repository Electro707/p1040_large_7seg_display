/**
 * F1093 - comms.h
 * electro707, 2025-07-11
 *
 * this file handles everything communication related
 */
#ifndef COMMS_H
#define COMMS_H

#include <stdbool.h>
#include <Print.h>
#include <HardwareSerial.h>

#define MAX_MSG_LEN     64
#define MAX_FW_BUFFER   8192

class ParserHandler{
    public:
        ParserHandler(void);
        void parse(char b);
        void setPrintClass(Print *pH);

    private:
        void handleFirmwareUpdate(char b);
        void parseTelnetDetokenized(char b);
        void processCommand(void);
        void subcommandGet(char *token);
        void subcommandSet(char *token);
        void subcommandUpdate(char *token);

        void txAck(void);
        void txNack(const char *errMsg);

        char msg[MAX_MSG_LEN];
        uint8_t msgIdx;
        bool prevByteEscape;
        bool prevCmdWill;
        bool prevCmdDo;
        bool lastReceivedInterrupt;
        uint expectedFwBytes;
        Print *printHandler;
};

extern HardwareSerial commsSerial;

#endif