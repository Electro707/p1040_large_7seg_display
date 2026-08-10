#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>
#include <Network.h>

#define PDU_MAX_LEN      32

// a state machine to keep track of where we read
typedef enum{
    MODBUS_STATE_START = 0,       // starting point, gets the transaction ID
    MODBUS_STATE_PID,
    MODBUS_STATE_LEN,
    MODBUS_STATE_UID,
    MODBUS_STATE_PDU,           // PDU data
    MODBUS_STATE_ERR,           // error state, hold here until we receive
                                // all bytes to send out error
}modbusState_e;

typedef struct{
    modbusState_e state;
    uint8_t wordBuffExp;        // how many more bytes we expect
    // modbus TCP specific items
    uint16_t transId;
    uint16_t protId;
    uint16_t msgLen;
    uint8_t uId;
    // modbus data
    uint8_t pdu[PDU_MAX_LEN];
    uint8_t pduIdx;
    // modify below as needed. In this case it goes to a CPP function call, so the entire
    //      class is pointer here
    NetworkClient *client;
}modbusC_s;

void modbusInit(modbusC_s *m);
void modbusProcessByte(modbusC_s *m, uint8_t read);

#endif