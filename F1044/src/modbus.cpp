/**
 * Used to have Modbus over Ethernet
 *
 * NOTE:
 *  - This is only a cpp file as the write function is inside of a class in this code implementation.
 *      As long as this is used where a simple function pointer can be used for write, then it should be ok to
 *      make a C only file instead
 */

#include <Arduino.h>
#include <Network.h>
#include <string.h>
#include "common.h"
#include "modbus.h"

typedef enum{
    MODBUS_FUNCCODE_READ_HOLD_REG = 0x03,
    MODBUS_FUNCCODE_READ_INPUT_REG = 0x04,
}modbusFuncCodes_e;

typedef enum{
    MODBUS_EXCEPTION_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EXCEPTION_ILLEGAL_ADDR = 0x02,
    MODBUS_EXCEPTION_ILLEGAL_DATA = 0x03,
    MODBUS_EXCEPTION_SERVER_FAIL = 0x04,
    MODBUS_EXCEPTION_ACK = 0x05,
    MODBUS_EXCEPTION_BUSY = 0x06,
    MODBUS_EXCEPTION_BAD_FRAME = 0xFE,      // made up, todo: see spec or convension
}modbusExceptions_e;

#define COPY_WORD_TO_BYTES(DST_BYTE, SRC_WORD) \
    *(DST_BYTE) = ((SRC_WORD) >> 8) & 0xFF; \
    *((DST_BYTE)+1) = (SRC_WORD) & 0xFF

#define WORD_FROM_BYTES(SRC)   ((SRC)[0] << 8 | (SRC)[1])


void modbusInit(modbusC_s *m){
    m->state = MODBUS_STATE_START;
    m->msgLen = 0;
    m->transId = 0;
    m->protId = 0;
    m->uId = 0;
    m->wordBuffExp = 2;             // we expect 2 bytes on the get-go
}

// NOTE: len must be less than PDU_MAX_LEN, this is internal so just ensure that
// todo: add non-runtime assert
void modbusSendPDU(modbusC_s *m, uint8_t *pdu, uint8_t pduLen){
    uint8_t resp[PDU_MAX_LEN+7];
    // copy
    memcpy(resp+7, pdu, pduLen);

    pduLen++;          // the length variable also includes the unit id

    COPY_WORD_TO_BYTES(&resp[0], m->transId);
    memset(&resp[2], 0, 2);
    COPY_WORD_TO_BYTES(&resp[4], pduLen);
    resp[6] = m->uId;

    m->client->write(resp, pduLen + 6);
}

void modbusSendError(modbusC_s *m, uint8_t code){
    uint8_t resp[2];
    resp[0] = m->pdu[0] | 0x80;
    resp[1] = code;
    modbusSendPDU(m, resp, 2);
}

void modbusProcessReadReg(modbusC_s *m){
    uint8_t resp[PDU_MAX_LEN];
    uint8_t *respP = resp;
    uint16_t startAddr;
    uint16_t regCount;

    uint16_t addr;
    uint16_t addrByte;      // byte address instead of word address. Used in array access

    startAddr = WORD_FROM_BYTES(&m->pdu[1]);
    regCount = WORD_FROM_BYTES(&m->pdu[3]);

    if(regCount > ((PDU_MAX_LEN >> 2)-2)){
        modbusSendError(m, MODBUS_EXCEPTION_ILLEGAL_DATA);
        return;
    }

    *respP++ = m->pdu[0];
    *respP++ = regCount << 1;        // *2 the register count

    for(uint16_t i=0;i<regCount;i++){
        addr = startAddr + i;

        // 0x00 -> 0x0C: firmware version
        if(addr < (FW_VERSION_STR_LEN >> 1)){
            addrByte = addr << 1;
            *respP++ = fwVersion[addrByte+1];
            *respP++ = fwVersion[addrByte];
            modbusSendPDU(m, resp, 4);
        }
        else{
            *respP++ = 0;
            *respP++ = 0;
        }

    }
    // 0x00 -> 0x0C: firmware version
    if(startAddr < (FW_VERSION_STR_LEN >> 1)){
        startAddr <<= 1;     // as the address is
        resp[2] = fwVersion[startAddr+1];
        resp[3] = fwVersion[startAddr];
        modbusSendPDU(m, resp, 4);
    }
    else{
        modbusSendError(m, MODBUS_EXCEPTION_ILLEGAL_ADDR);
    }
}

void modbusProcessPDU(modbusC_s *m){
// todo: check pduIdx

    switch(m->pdu[0]){
        case MODBUS_FUNCCODE_READ_HOLD_REG:
        case MODBUS_FUNCCODE_READ_INPUT_REG:
            modbusProcessReadReg(m);
            break;
        default:
            modbusSendError(m, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
    }

}

void modbusProcessByte(modbusC_s *m, uint8_t readByte){
    switch(m->state){
        case MODBUS_STATE_START:
            m->transId <<= 8;
            m->transId |= readByte;
            if(--m->wordBuffExp == 0){
                m->wordBuffExp = 2;
                m->state = MODBUS_STATE_PID;
            }
            break;
        case MODBUS_STATE_PID:
            m->protId <<= 8;
            m->protId |= readByte;
            if(--m->wordBuffExp == 0){
                m->wordBuffExp = 2;
                m->state = MODBUS_STATE_LEN;
            }
            break;
        case MODBUS_STATE_LEN:
            m->msgLen <<= 8;
            m->msgLen |= readByte;
            if(--m->wordBuffExp == 0){
                if(m->msgLen == 0){
                    // immediatly send out error
                    modbusSendError(m, MODBUS_EXCEPTION_BAD_FRAME);
                    modbusInit(m);
                }
                else if(m->msgLen < 2){
                    // go to error state
                    m->state = MODBUS_STATE_ERR;
                }
                else{
                    m->state = MODBUS_STATE_UID;
                }
            }
            break;
        case MODBUS_STATE_UID:
            m->uId = readByte;
            m->msgLen--;
            m->state = MODBUS_STATE_PDU;
            m->pduIdx = 0;
            break;
        case MODBUS_STATE_PDU:
            m->pdu[m->pduIdx++] = readByte;
            if(--m->msgLen == 0){
                // we received the PDU, process it
                modbusProcessPDU(m);
                modbusInit(m);
            }
            break;
        case MODBUS_STATE_ERR:
            if(--m->msgLen == 0){
                // send out error
                modbusSendError(m, MODBUS_EXCEPTION_BAD_FRAME);
                modbusInit(m);
            }
            break;
    }
}