/*
 * Copyright (c) 2018, 2019 Hendrik Hagendorn
 * Copyright (c) 2015, 2016 Ideetron B.V.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http:// www.gnu.org/licenses/>.
 */
#ifdef DEBUG
#include "debug.h"
#endif

#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/atomic.h>
#include <util/delay.h>


#include "config.h"
#include "pins.h"
#include "slimlora.h"
#include "tinyspi.h"

#if OTAA
extern const uint8_t DevEUI[8];
extern const uint8_t JoinEUI[8];
extern const uint8_t NwkKey[16];
extern const uint8_t AppKey[16];
#else
extern const uint8_t NwkSKey[16];
extern const uint8_t AppSKey[16];
extern const uint8_t DevAddr[4];
#endif

extern TinySPI SPI;

/*
*****************************************************************************************
* Description: Frequency band Europe
*****************************************************************************************
*/
const uint8_t PROGMEM SlimLoRa::FrequencyTable[9][3] = {
    { 0xD9, 0x06, 0x8B }, // Channel 0 868.100 MHz / 61.035 Hz = 14222987 = 0xD9068B
    { 0xD9, 0x13, 0x58 }, // Channel 1 868.300 MHz / 61.035 Hz = 14226264 = 0xD91358
    { 0xD9, 0x20, 0x24 }, // Channel 2 868.500 MHz / 61.035 Hz = 14229540 = 0xD92024
    { 0xD8, 0xC6, 0x8B }, // Channel 3 867.100 MHz / 61.035 Hz = 14206603 = 0xD8C68B
    { 0xD8, 0xD3, 0x58 }, // Channel 4 867.300 MHz / 61.035 Hz = 14209880 = 0xD8D358
    { 0xD8, 0xE0, 0x24 }, // Channel 5 867.500 MHz / 61.035 Hz = 14213156 = 0xD8E024
    { 0xD8, 0xEC, 0xF1 }, // Channel 6 867.700 MHz / 61.035 Hz = 14216433 = 0xD8ECF1
    { 0xD8, 0xF9, 0xBE }, // Channel 7 867.900 MHz / 61.035 Hz = 14219710 = 0xD8F9BE
    { 0xD9, 0x61, 0xBE }  // Downlink  869.525 MHz / 61.035 Hz = 14246334 = 0xD961BE
};

/*
*****************************************************************************************
* Description: Data rate
*****************************************************************************************
*/
const uint8_t PROGMEM SlimLoRa::DataRateTable[7][3] = {
    // bw    sf   agc
    { 0x72, 0xC4, 0x0C }, // SF12BW125
    { 0x72, 0xB4, 0x0C }, // SF11BW125
    { 0x72, 0xA4, 0x04 }, // SF10BW125
    { 0x72, 0x94, 0x04 }, // SF9BW125
    { 0x72, 0x84, 0x04 }, // SF8BW125
    { 0x72, 0x74, 0x04 }, // SF7BW125
    { 0x82, 0x74, 0x04 }  // SF7BW250
};

/*
*****************************************************************************************
* Description: Half symbol times
*****************************************************************************************
*/
const uint16_t PROGMEM SlimLoRa::DRTicksPerHalfSymbol[7] = {
    ((128 << 7) * TICKS_PER_SECOND + 500000) / 1000000, // SF12BW125
    ((128 << 6) * TICKS_PER_SECOND + 500000) / 1000000, // SF11BW125
    ((128 << 5) * TICKS_PER_SECOND + 500000) / 1000000, // SF10BW125
    ((128 << 4) * TICKS_PER_SECOND + 500000) / 1000000, // SF9BW125
    ((128 << 3) * TICKS_PER_SECOND + 500000) / 1000000, // SF8BW125
    ((128 << 2) * TICKS_PER_SECOND + 500000) / 1000000, // SF7BW125
    ((128 << 1) * TICKS_PER_SECOND + 500000) / 1000000  // SF7BW250
};

/*
*****************************************************************************************
* Description: S_Table used for AES encription
*****************************************************************************************
*/
const uint8_t PROGMEM SlimLoRa::S_Table[16][16] = {
    {0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76},
    {0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0},
    {0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15},
    {0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75},
    {0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84},
    {0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF},
    {0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8},
    {0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2},
    {0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73},
    {0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB},
    {0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79},
    {0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08},
    {0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A},
    {0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E},
    {0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF},
    {0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16}
};

void SlimLoRa::Init() {
    uint8_t detect_optimize;

    // Sleep
    RfmWrite(RFM_REG_OP_MODE, 0x00);

    // LoRa mode
    RfmWrite(RFM_REG_OP_MODE, 0x80);

    // PA_BOOST pin / +15 dBm output power
    RfmWrite(RFM_REG_PA_CONFIG, 0xFD);

    // Preamble length: 8 symbols
    // 0x0008 + 4 = 12
    RfmWrite(RFM_REG_PREAMBLE_MSB, 0x00);
    RfmWrite(RFM_REG_PREAMBLE_LSB, 0x08);

    // LoRa sync word
    RfmWrite(RFM_REG_SYNC_WORD, 0x34);

    // Errata Note - 2.3 Receiver Spurious Reception
    detect_optimize = RfmRead(RFM_REG_DETECT_OPTIMIZE);
    RfmWrite(RFM_REG_DETECT_OPTIMIZE, (detect_optimize & 0x78) | 0x03);
    RfmWrite(RFM_REG_IF_FREQ_1, 0x00);
    RfmWrite(RFM_REG_IF_FREQ_2, 0x40);

    // FIFO pointers
    RfmWrite(RFM_REG_FIFO_TX_BASE_ADDR, 0x80);
    RfmWrite(RFM_REG_FIFO_RX_BASE_ADDR, 0x00);

    mTxFrameCounter = GetTxFrameCounter();
    mRxFrameCounter = GetRxFrameCounter();
    mRx2DataRate = GetRx2DataRate();
}

/*
*****************************************************************************************
* Description : Function for receiving a packet using the RFM
*
* Arguments   : *packet Pointer to Rx packet array
*               packet_max_length Maximum number of bytes to read from Rx packet
*               channel The FrequencyTable channel index to listen on
*               dri The DataRateTable index to listen on
*               rx_tickstamp Listen until rx_tickstamp elapsed
*****************************************************************************************
*/
int8_t SlimLoRa::RfmReceivePacket(uint8_t *packet, uint8_t packet_max_length, uint8_t channel, uint8_t dri, uint32_t rx_tickstamp) {
    uint8_t modem_config_3, irq_flags, packet_length, read_length;

    // Wait for start time
    wait_until(rx_tickstamp - LORAWAN_RX_SETUP_TICKS);

    // Switch RFM to standby
    RfmWrite(RFM_REG_OP_MODE, 0x81);

    // Invert IQ
    RfmWrite(RFM_REG_INVERT_IQ, 0x66);
    RfmWrite(RFM_REG_INVERT_IQ_2, 0x19);

    // Set SPI pointer to start of Rx part in FiFo
    RfmWrite(RFM_REG_FIFO_ADDR_PTR, 0x00);

    // Channel
    RfmWrite(RFM_REG_FR_MSB, pgm_read_byte(&(FrequencyTable[channel][0])));
    RfmWrite(RFM_REG_FR_MID, pgm_read_byte(&(FrequencyTable[channel][1])));
    RfmWrite(RFM_REG_FR_LSB, pgm_read_byte(&(FrequencyTable[channel][2])));

    // Bandwidth / Coding Rate / Implicit Header Mode
    RfmWrite(RFM_REG_MODEM_CONFIG_1, pgm_read_byte(&(DataRateTable[dri][0])));

    // Spreading Factor / Tx Continuous Mode / Crc
    RfmWrite(RFM_REG_MODEM_CONFIG_2, pgm_read_byte(&(DataRateTable[dri][1])));

    // Automatic Gain Control / Low Data Rate Optimize
    modem_config_3 = pgm_read_byte(&(DataRateTable[dri][2]));
    if (dri == SF12BW125 || dri == SF11BW125) {
        modem_config_3 |= 0x08;
    }
    RfmWrite(RFM_REG_MODEM_CONFIG_3, modem_config_3);

    // Rx timeout
    RfmWrite(RFM_REG_SYMB_TIMEOUT_LSB, mRxSymbols);

    // Clear interrupts
    RfmWrite(RFM_REG_IRQ_FLAGS, 0xFF);

    // Wait for rx time
    wait_until(rx_tickstamp);

    // Switch RFM to Rx
    RfmWrite(RFM_REG_OP_MODE, 0x86);

#ifdef DEBUG
    debug(DSTR_2);
#endif // DEBUG

    // Wait for RxDone or RxTimeout
    do {
        irq_flags = RfmRead(RFM_REG_IRQ_FLAGS);
    } while (!(irq_flags & 0xC0));

    packet_length = RfmRead(RFM_REG_RX_NB_BYTES);
    RfmWrite(RFM_REG_FIFO_ADDR_PTR, RfmRead(RFM_REG_FIFO_RX_CURRENT_ADDR));

    if (packet_max_length < packet_length) {
        read_length = packet_max_length;
    } else {
        read_length = packet_length;
    }
    for (uint8_t i = 0; i < read_length; i++) {
        packet[i] = RfmRead(RFM_REG_FIFO);
    }

    // Clear interrupts
    RfmWrite(RFM_REG_IRQ_FLAGS, 0xFF);

    // Switch RFM to sleep
    RfmWrite(RFM_REG_OP_MODE, 0x00);

    switch (irq_flags & 0xC0) {
        case RFM_STATUS_RX_TIMEOUT:
            return RFM_ERROR_RX_TIMEOUT;
        case RFM_STATUS_RX_DONE_CRC_ERROR:
            return RFM_ERROR_CRC;
        case RFM_STATUS_RX_DONE:
            return packet_length;
    }

    return RFM_ERROR_UNKNOWN;
}

/*
*****************************************************************************************
* Description : Function for sending a packet with the RFM
*
* Arguments   : *packet Pointer to array with data to be send
*               packet_length Length of the packet to send
*               channel The FrequencyTable channel index
*               dri The DataRateTable index
*               start_timer Wheter or not to start a timer for Rx delay
*****************************************************************************************
*/
void SlimLoRa::RfmSendPacket(uint8_t *packet, uint8_t packet_length, uint8_t channel, uint8_t dri, bool start_timer) {
    uint8_t modem_config_3;

    // Switch RFM to standby
    RfmWrite(RFM_REG_OP_MODE, 0x81);

    // Don't invert IQ
    RfmWrite(RFM_REG_INVERT_IQ, 0x27);
    RfmWrite(RFM_REG_INVERT_IQ_2, 0x1D);

    // Channel
    RfmWrite(RFM_REG_FR_MSB, pgm_read_byte(&(FrequencyTable[channel][0])));
    RfmWrite(RFM_REG_FR_MID, pgm_read_byte(&(FrequencyTable[channel][1])));
    RfmWrite(RFM_REG_FR_LSB, pgm_read_byte(&(FrequencyTable[channel][2])));

    // Bandwidth / Coding Rate / Implicit Header Mode
    RfmWrite(RFM_REG_MODEM_CONFIG_1, pgm_read_byte(&(DataRateTable[dri][0])));

    // Spreading Factor / Tx Continuous Mode / Crc
    RfmWrite(RFM_REG_MODEM_CONFIG_2, pgm_read_byte(&(DataRateTable[dri][1])));

    // Automatic Gain Control / Low Data Rate Optimize
    modem_config_3 = pgm_read_byte(&(DataRateTable[dri][2]));
    if (dri == SF12BW125 || dri == SF11BW125) {
        modem_config_3 |= 0x08;
    }
    RfmWrite(RFM_REG_MODEM_CONFIG_3, modem_config_3);

    // Set payload length to the right length
    RfmWrite(RFM_REG_PAYLOAD_LENGTH, packet_length);

    // Set SPI pointer to start of Tx part in FiFo
    RfmWrite(RFM_REG_FIFO_ADDR_PTR, 0x80);

    // Write Payload to FiFo
    for (uint8_t i = 0; i < packet_length; i++) {
        RfmWrite(RFM_REG_FIFO, *packet);
        packet++;
    }

    if (start_timer) {
        init_timer0_16us();
    }

    // Switch RFM to Tx
    RfmWrite(RFM_REG_OP_MODE, 0x83);

    // Wait for TxDone in the RegIrqFlags register
    while ((RfmRead(RFM_REG_IRQ_FLAGS) & RFM_STATUS_TX_DONE) != RFM_STATUS_TX_DONE);

    if (start_timer) {
        ATOMIC_BLOCK(ATOMIC_FORCEON) {
            mTxDoneTickstamp = t0_ticks;
        }

#ifdef DEBUG
        debug(DSTR_1);
#endif // DEBUG
    }

#ifdef DEBUG
    debug(DSTR_RSP_TXDONE);
#endif

    // Clear interrupt
    RfmWrite(RFM_REG_IRQ_FLAGS, 0xFF);

    // Switch RFM to sleep
    RfmWrite(RFM_REG_OP_MODE, 0x00);

    // Saves memory cycles, at worst 10 lost packets
    if (++mTxFrameCounter % 10) {
        SetTxFrameCounter(mTxFrameCounter);
    }
    mAdrAckCounter++;
}

/*
*****************************************************************************************
* Description : Funtion that writes a register from the RFM
*
* Arguments   : address Address of register to be written
*
*               data    Data to be written
*****************************************************************************************
*/
inline void SlimLoRa::RfmWrite(uint8_t address, uint8_t data) {
    // Set NSS pin Low to start communication
    PRT_RFM_NSS &= ~(1 << PB_RFM_NSS);

    // Send addres with MSB 1 to write
    SPI.Transfer(address | 0x80);
    // Send Data
    SPI.Transfer(data);

    // Set NSS pin High to end communication
    PRT_RFM_NSS |= (1 << PB_RFM_NSS);
}

/*
*****************************************************************************************
* Description : Funtion that reads a register from the RFM and returns the value
*
* Arguments   : address Address of register to be read
*
* Returns   : Value of the register
*****************************************************************************************
*/
inline uint8_t SlimLoRa::RfmRead(uint8_t address) {
    uint8_t data;

    // Set NSS pin low to start SPI communication
    PRT_RFM_NSS &= ~(1 << PB_RFM_NSS);

    // Send Address
    SPI.Transfer(address);
    // Send 0x00 to be able to receive the answer from the RFM
    data = SPI.Transfer(0x00);

    // Set NSS high to end communication
    PRT_RFM_NSS |= (1 << PB_RFM_NSS);

    // Return received data
    return data;
}

/*
*****************************************************************************************
* Description : Function calculates the clock drift adjustment (+- 5%)
*****************************************************************************************
*/
inline uint32_t SlimLoRa::CaluclateDriftAdjustment(uint32_t delay, uint16_t ticks_per_half_symbol) {
    // Clock drift
    uint32_t drift = delay * 5 / 100;
    delay -= drift;

    if ((255 - mRxSymbols) * ticks_per_half_symbol < drift) {
        mRxSymbols = 255;
    } else {
        mRxSymbols = 6 + drift / ticks_per_half_symbol;
    }

    return delay;
}

/*
*****************************************************************************************
* Description : Function calculates the centered rx window offset
*****************************************************************************************
*/
inline int32_t SlimLoRa::CalculateRxWindowOffset(int16_t ticks_per_half_symbol) {
    const uint16_t ticks_per_symbol = 2 * ticks_per_half_symbol;

    uint8_t rx_symbols = ((2 * LORAWAN_RX_MIN_SYMBOLS - 8) * ticks_per_symbol + 2 * LORAWAN_RX_ERROR_TICKS + ticks_per_symbol - 1) / ticks_per_symbol;
    if (rx_symbols < LORAWAN_RX_MIN_SYMBOLS) {
        rx_symbols = LORAWAN_RX_MIN_SYMBOLS;
    }
    mRxSymbols = rx_symbols;

    return (8 - rx_symbols) * ticks_per_half_symbol - LORAWAN_RX_MARGIN_TICKS;
}

/*
*****************************************************************************************
* Description : Function calculates the Rx delay for a given data rate
*****************************************************************************************
*/
uint32_t SlimLoRa::CalculateRxDelay(uint8_t data_rate, uint32_t delay) {
    uint16_t ticks_per_half_symbol;
    int32_t offset;

    ticks_per_half_symbol = pgm_read_word(&(DRTicksPerHalfSymbol[data_rate]));
    offset = CalculateRxWindowOffset(ticks_per_half_symbol);

    return CaluclateDriftAdjustment(delay + offset, ticks_per_half_symbol);
}

/*
*****************************************************************************************
* Description : Function enables/disables the ADR mechanism
*****************************************************************************************
*/
void SlimLoRa::SetAdrEnabled(bool enabled) {
    mAdrEnabled = enabled;
}

#if OTAA
/*
*****************************************************************************************
* Description : Function returns if the device joined a LoRaWAN network
*****************************************************************************************
*/
bool SlimLoRa::HasJoined() {
    return mHasJoined;
}

/*
*****************************************************************************************
* Description : Function contstructs a LoRaWAN Join-request packet and sends it
*****************************************************************************************
*/
int8_t SlimLoRa::Join() {
    uint8_t packet[1 + LORAWAN_JOIN_REQUEST_SIZE + 4];
    uint8_t packet_length;

    uint16_t dev_nonce;
    uint8_t mic[4];

    packet[0] = LORAWAN_MTYPE_JOIN_REQUEST;

    packet[1] = JoinEUI[7];
    packet[2] = JoinEUI[6];
    packet[3] = JoinEUI[5];
    packet[4] = JoinEUI[4];
    packet[5] = JoinEUI[3];
    packet[6] = JoinEUI[2];
    packet[7] = JoinEUI[1];
    packet[8] = JoinEUI[0];

    packet[9] = DevEUI[7];
    packet[10] = DevEUI[6];
    packet[11] = DevEUI[5];
    packet[12] = DevEUI[4];
    packet[13] = DevEUI[3];
    packet[14] = DevEUI[2];
    packet[15] = DevEUI[1];
    packet[16] = DevEUI[0];

    dev_nonce = GetDevNonce();
    SetDevNonce(++dev_nonce);

    packet[17] = dev_nonce & 0xFF;
    packet[18] = dev_nonce++ >> 8;

    packet_length = 1 + LORAWAN_JOIN_REQUEST_SIZE;

#if LORAWAN1_1
    CalculateMic(NwkKey, packet, NULL, mic, packet_length);
#else
    CalculateMic(AppKey, packet, NULL, mic, packet_length);
#endif // LORAWAN1_1
    for (uint8_t i = 0; i < 4; i++) {
        packet[i + packet_length] = mic[i];
    }
    packet_length += 4;

    mChannel = mPseudoByte & 0x01;
    RfmSendPacket(packet, packet_length, mChannel, mDataRate, true);

    if (!ProcessJoinAccept(1)) {
        return 0;
    }

    return ProcessJoinAccept(2);
}

/*
*****************************************************************************************
* Description : Function validates the calculated 4-byte MIC against the received 4-byte MIC
*****************************************************************************************
*/
inline bool SlimLoRa::CheckMic(uint8_t *cmic, uint8_t *rmic) {
    return cmic[0] == rmic[0] && cmic[1] == rmic[1]
            && cmic[2] == rmic[2] && cmic[3] == rmic[3];
}

/*
*****************************************************************************************
* Description : Function processes a LoRaWAN 1.0 Join-accept message
*****************************************************************************************
*/
bool SlimLoRa::ProcessJoinAccept1_0(uint8_t *packet, uint8_t packet_length) {
    uint8_t buffer[16], mic[4];
    uint8_t packet_length_no_mic = packet_length - 4;
    uint16_t dev_nonce;

    CalculateMic(AppKey, packet, NULL, mic, packet_length_no_mic);

    if (!CheckMic(mic, packet + packet_length_no_mic)) {
        return false;
    }

    dev_nonce = GetDevNonce();

    // Derive AppSKey, FNwkSIntKey, SNwkSIntKey, NwkSEncKey
    for (uint8_t i = 1; i <= 2; i++) {
        memset(buffer, 0, 16);

        buffer[0] = i;

        // JoinNonce
        buffer[1] = packet[1];
        buffer[2] = packet[2];
        buffer[3] = packet[3];

        // NetID
        buffer[4] = packet[4];
        buffer[5] = packet[5];
        buffer[6] = packet[6];

        // DevNonce
        buffer[7] = dev_nonce & 0xFF;
        buffer[8] = dev_nonce >> 8;

        AesEncrypt(AppKey, buffer);

        if (i == 1) {
            SetFNwkSIntKey(buffer);
            SetSNwkSIntKey(buffer);
            SetNwkSEncKey(buffer);
        } else {
            SetAppSKey(buffer);
        }
    }

    return true;
}

#if LORAWAN1_1
/*
*****************************************************************************************
* Description : Function processes a LoRaWAN 1.1 Join-accept message
*****************************************************************************************
*/
bool SlimLoRa::ProcessJoinAccept1_1(uint8_t *packet, uint8_t packet_length) {
    uint8_t buffer[40] = { 0 }, mic[4];
    uint8_t packet_length_no_mic = packet_length - 4;
    uint16_t dev_nonce;

    // JoinReqType | JoinEUI | DevNonce | MHDR | JoinNonce | NetID | DevAddr | DLSettings | RxDelay | CFList
    buffer[0] = 0xFF; // TODO: JoinReqType

    // JoinEUI
    buffer[1] = JoinEUI[7];
    buffer[2] = JoinEUI[6];
    buffer[3] = JoinEUI[5];
    buffer[4] = JoinEUI[4];
    buffer[5] = JoinEUI[3];
    buffer[6] = JoinEUI[2];
    buffer[7] = JoinEUI[1];
    buffer[8] = JoinEUI[0];

    // DevNonce
    buffer[9] = dev_nonce & 0xFF;
    buffer[10] = dev_nonce >> 8;

    // MHDR
    buffer[11] = packet[0];

    // JoinNonce
    buffer[12] = packet[1];
    buffer[13] = packet[2];
    buffer[14] = packet[3];

    // NetID
    buffer[15] = packet[4];
    buffer[16] = packet[5];
    buffer[17] = packet[6];

    // DevAddr
    buffer[18] = packet[7];
    buffer[19] = packet[8];
    buffer[20] = packet[9];
    buffer[21] = packet[10];

    // DLSettings
    buffer[22] = packet[11];

    // RxDelay
    buffer[23] = packet[12];

    if (packet_length > 17) {
        // CFList
        for (uint8_t i = 0; i < 16; i++) {
            buffer[24 + i] = packet[13 + i];
        }

        //CalculateMic(JSIntKey, buffer, NULL, mic, 40);
    } else {
        //CalculateMic(JSIntKey, buffer, NULL, mic, 24);
    }

    if (!CheckMic(mic, packet + packet_length_no_mic)) {
        return false;
    }

    dev_nonce = GetDevNonce();

    // Derive AppSKey, FNwkSIntKey, SNwkSIntKey and NwkSEncKey
    for (uint8_t i = 1; i <= 4; i++) {
        memset(buffer, 0, 16);

        buffer[0] = i;

        // JoinNonce
        buffer[1] = packet[1];
        buffer[2] = packet[2];
        buffer[3] = packet[3];

        // JoinEUI
        buffer[4] = JoinEUI[7];
        buffer[5] = JoinEUI[6];
        buffer[6] = JoinEUI[5];
        buffer[7] = JoinEUI[4];
        buffer[8] = JoinEUI[3];
        buffer[9] = JoinEUI[2];
        buffer[10] = JoinEUI[1];
        buffer[11] = JoinEUI[0];

        // DevNonce
        buffer[12] = dev_nonce & 0xFF;
        buffer[13] = dev_nonce >> 8;

        if (i == 2) {
            AesEncrypt(AppKey, buffer);
        } else {
            AesEncrypt(NwkKey, buffer);
        }

        switch (i) {
            case 1:
                SetFNwkSIntKey(buffer);
                break;
            case 2:
                SetAppSKey(buffer);
                break;
            case 3:
                SetSNwkSIntKey(buffer);
                break;
            case 4:
                SetNwkSEncKey(buffer);
                break;
        }
    }

    return true;
}
#endif // LORAWAN1_1

/*
*****************************************************************************************
* Description : Function listens and processes a LoRaWAN Join-accept message
*
* Arguments   : window Index of the receive window [1,2]
*****************************************************************************************
*/
int8_t SlimLoRa::ProcessJoinAccept(uint8_t window) {
    int8_t result;
    uint32_t rx_delay;

    uint8_t packet[1 + LORAWAN_JOIN_ACCEPT_MAX_SIZE + 4];
    int8_t packet_length;

    bool mic_valid = false;
    uint8_t dev_addr[4];
    uint32_t join_nonce;

    if (window == 1) {
        rx_delay = CalculateRxDelay(mDataRate, LORAWAN_JOIN_ACCEPT_DELAY1_TICKS);

        packet_length = RfmReceivePacket(packet, sizeof(packet), mChannel, mDataRate, mTxDoneTickstamp + rx_delay);
    } else {
        rx_delay = CalculateRxDelay(mRx2DataRate, LORAWAN_JOIN_ACCEPT_DELAY2_TICKS);

        packet_length = RfmReceivePacket(packet, sizeof(packet), 8, mRx2DataRate, mTxDoneTickstamp + rx_delay);
    }
#ifdef DEBUG
    debug_int16(DSTR_PJA_PLEN, packet_length);
#endif

    if (packet_length <= 0) {
        result = LORAWAN_ERROR_NO_PACKET_RECEIVED;
        goto end;
    }

    if (packet_length > 1 + LORAWAN_JOIN_ACCEPT_MAX_SIZE + 4) {
        result = LORAWAN_ERROR_SIZE_EXCEEDED;
        goto end;
    }

    if (packet[0] != LORAWAN_MTYPE_JOIN_ACCEPT) {
        result = LORAWAN_ERROR_UNEXPECTED_MTYPE;
        goto end;
    }

#if LORAWAN1_1
    AesEncrypt(NwkKey, packet + 1);
#else
    AesEncrypt(AppKey, packet + 1);
#endif // LORAWAN1_1
    if (packet_length > 17) {
#if LORAWAN1_1
        AesEncrypt(NwkKey, packet + 17);
#else
        AesEncrypt(AppKey, packet + 17);
#endif // LORAWAN1_1
    }

    // Check JoinNonce validity
    join_nonce = packet[1] | packet[2] << 8 | (uint32_t) packet[3] << 16;
    if (GetJoinNonce() >= join_nonce) {
        result = LORAWAN_ERROR_INVALID_JOIN_NONCE;
        goto end;
    }

    // Check OptNeg flag
    if (packet[11] & 0x80) {
        // LoRaWAN1.1+

#if LORAWAN1_1
        mic_valid = ProcessJoinAccept1_1(packet, packet_length);
#endif // LORAWAN1_1
    } else {
        // LoRaWAN1.0

        mic_valid = ProcessJoinAccept1_0(packet, packet_length);
    }

    if (!mic_valid) {
        mHasJoined = false;
        result = LORAWAN_ERROR_INVALID_MIC;
        goto end;
    }

    SetJoinNonce(join_nonce);

    dev_addr[0] = packet[10];
    dev_addr[1] = packet[9];
    dev_addr[2] = packet[8];
    dev_addr[3] = packet[7];
    SetDevAddr(dev_addr);

    mRx2DataRate = packet[11] & 0xF;
    SetRx2DataRate(mRx2DataRate);

    mTxFrameCounter = 0;
    SetTxFrameCounter(0);

    mRxFrameCounter = 0;
    SetRxFrameCounter(0);

    mAdrAckCounter = 0;

    mHasJoined = true;

    result = 0;

end:
    if (result == 0 || window == 2) {
        stop_timer0();
    }

#ifdef DEBUG
    debug_int16(DSTR_RESULT, result);
#endif

    return result;
}
#endif // OTAA

/*
*****************************************************************************************
* Description : Function processes frame options of downlink packets
*
* Arguments   : options Pointer to the start of the frame options section
*               f_options_length Length of the frame options section
*****************************************************************************************
*/
void SlimLoRa::ProcessFrameOptions(uint8_t *options, uint8_t f_options_length) {
    uint8_t new_data_rate;

    if (f_options_length == 0) {
        return;
    }

    for (uint8_t i = 0; i < f_options_length; i++) {
        switch (options[i]) {
            case LORAWAN_FOPT_LINK_CHECK_ANS:
                i += 2;
                break;
            case LORAWAN_FOPT_LINK_ADR_REQ:
                new_data_rate = options[i + 1] >> 4;
                if (new_data_rate >= SF12BW125 && new_data_rate <= SF7BW250) { // Reversed table index (SF7B250 higher)
                    mDataRate = new_data_rate;

                    mPendingFopts.fopts[mPendingFopts.length++] = LORAWAN_FOPT_LINK_ADR_ANS;
                    mPendingFopts.fopts[mPendingFopts.length++] = 0x2;
                } else {
                    mPendingFopts.fopts[mPendingFopts.length++] = LORAWAN_FOPT_LINK_ADR_ANS;
                    mPendingFopts.fopts[mPendingFopts.length++] = 0;
                }

                i += 4;
                break;
            case LORAWAN_FOPT_DUTY_CYCLE_REQ:
                i += 1;
                break;
            case LORAWAN_FOPT_RX_PARAM_SETUP_REQ:
                new_data_rate = options[i + 1] & 0xF;
                if (new_data_rate >= SF12BW125 && new_data_rate <= SF7BW250) { // Reversed table index (SF7B250 higher)
                    mRx2DataRate = new_data_rate;
                    SetRx2DataRate(mRx2DataRate);

                    mPendingFopts.fopts[mPendingFopts.length++] = LORAWAN_FOPT_RX_PARAM_SETUP_ANS;
                    mPendingFopts.fopts[mPendingFopts.length++] = 0x2;
                } else {
                    mPendingFopts.fopts[mPendingFopts.length++] = LORAWAN_FOPT_RX_PARAM_SETUP_ANS;
                    mPendingFopts.fopts[mPendingFopts.length++] = 0;
                }

                i += 4;
                break;
            case LORAWAN_FOPT_DEV_STATUS_REQ:
                i += 2;
                break;
            case LORAWAN_FOPT_NEW_CHANNEL_REQ:
                i += 5;
                break;
            case LORAWAN_FOPT_RX_TIMING_SETUP_REQ:
                i += 1;
                break;
            case LORAWAN_FOPT_TX_PARAM_SETUP_REQ:
                i += 1;
                break;
            case LORAWAN_FOPT_DL_CHANNEL_REQ:
                i += 4;
                break;
            case LORAWAN_FOPT_DEVICE_TIME_ANS:
                i += 5;
                break;
            case LORAWAN_FOPT_PROP_DISABLE_ADR:
                mAdrEnabled = false;
                break;
            case LORAWAN_FOPT_PROP_ENABLE_ADR:
                mAdrEnabled = true;
                break;
            default:
                return;
        }
    }
}

/*
*****************************************************************************************
* Description : Function listens for and processes LoRaWAN downlink packets
*
* Arguments   : window Receive window index
*****************************************************************************************
*/
int8_t SlimLoRa::ProcessDownlink(uint8_t window) {
    int8_t result;
    uint32_t rx_delay;

    uint8_t packet[64];
    int8_t packet_length;

    uint8_t f_options_length, port, payload_length;

    uint16_t frame_counter;

    uint8_t mic[4];

#if OTAA
    uint8_t dev_addr[4];
    GetDevAddr(dev_addr);
#endif // OTAA

    if (window == 1) {
        rx_delay = CalculateRxDelay(mDataRate, LORAWAN_RECEIVE_DELAY1_TICKS);

        packet_length = RfmReceivePacket(packet, sizeof(packet), mChannel, mDataRate, mTxDoneTickstamp + rx_delay);
    } else {
        rx_delay = CalculateRxDelay(mRx2DataRate, LORAWAN_RECEIVE_DELAY2_TICKS);

        packet_length = RfmReceivePacket(packet, sizeof(packet), 8, mRx2DataRate, mTxDoneTickstamp + rx_delay);
    }
#ifdef DEBUG
    debug_int16(DSTR_PD_PLEN, packet_length);
#endif

    if (packet_length <= 0) {
        result = LORAWAN_ERROR_NO_PACKET_RECEIVED;
        goto end;
    }

    if (packet_length > sizeof(packet)) {
        result = LORAWAN_ERROR_SIZE_EXCEEDED;
        goto end;
    }

    if (packet[0] != LORAWAN_MTYPE_UNCONFIRMED_DATA_DOWN
            && packet[0] != LORAWAN_MTYPE_CONFIRMED_DATA_DOWN) {
        result = LORAWAN_ERROR_UNEXPECTED_MTYPE;
        goto end;
    }

    frame_counter = packet[7] << 8 | packet[6];
    if (frame_counter < mRxFrameCounter) {
#ifdef DEBUG
        debug_uint16(DSTR_FCNT_L, mRxFrameCounter);
        debug_uint16(DSTR_FCNT_R, frame_counter);
#endif
        result = LORAWAN_ERROR_INVALID_FRAME_COUNTER;
        goto end;
    }

#if OTAA
    if (!(packet[4] == dev_addr[0] && packet[3] == dev_addr[1]
            && packet[2] == dev_addr[2] && packet[1] == dev_addr[3])) {
        result = LORAWAN_ERROR_UNEXPECTED_DEV_ADDR;
        goto end;
    }
#else
    if (!(packet[4] == DevAddr[0] && packet[3] == DevAddr[1]
            && packet[2] == DevAddr[2] && packet[1] == DevAddr[3])) {
        result = LORAWAN_ERROR_UNEXPECTED_DEV_ADDR;
        goto end;
    }
#endif // OTAA

    // Check MIC
    CalculateMessageMic(packet, mic, packet_length - 4, frame_counter, LORAWAN_DIRECTION_DOWN);
    if (!CheckMic(mic, &packet[packet_length - 4])) {
        return LORAWAN_ERROR_INVALID_MIC;
    }

    // Saves memory cycles, we could loose more than 3 packets if we don't receive a packet at all
    mRxFrameCounter = frame_counter + 1;
    if (mRxFrameCounter % 3) {
        SetRxFrameCounter(mRxFrameCounter);
    }

    // Reset ADR acknowledge counter
    mAdrAckCounter = 0;

    // Process MAC commands
    f_options_length = packet[5] & 0xF;
    ProcessFrameOptions(&packet[8], f_options_length);

    // Overwrite MAc commands if packet on port 0xBB
    port = packet[8 + f_options_length];
    if (port == 0 || port == 0xBB) {
        payload_length = packet_length - 8 - f_options_length - 4;
        EncryptPayload(&packet[8 + f_options_length + 1], payload_length, frame_counter, LORAWAN_DIRECTION_DOWN);

        ProcessFrameOptions(&packet[8 + f_options_length + 1], payload_length);
    }

    result = 0;

end:
    if (result == 0 || window == 2) {
        stop_timer0();
    }

#ifdef DEBUG
    debug_int16(DSTR_RESULT, result);
#endif

    return result;
}

/*
*****************************************************************************************
* Description : Function contstructs a LoRaWAN packet and sends it
*
* Arguments   : port FPort of the frame
*               *payload pointer to the array of data that will be transmitted
*               payload_length nuber of bytes to be transmitted
*****************************************************************************************
*/
void SlimLoRa::Transmit(uint8_t fport, uint8_t *payload, uint8_t payload_length) {
    uint8_t packet[64];
    uint8_t packet_length = 0;

    uint8_t mic[4];

#if OTAA
    uint8_t dev_addr[4];
    GetDevAddr(dev_addr);
#endif // OTAA

    // Encrypt the data
    EncryptPayload(payload, payload_length, mTxFrameCounter, LORAWAN_DIRECTION_UP);

    // Spreading factor adjustment
    // TODO: Probably too aggressive
    if (mAdrAckCounter >= LORAWAN_ADR_ACK_LIMIT + LORAWAN_ADR_ACK_DELAY) {
        mDataRate = SF12BW125;
    }

    // Build the packet
    packet[packet_length++] = LORAWAN_MTYPE_UNCONFIRMED_DATA_UP;

#if OTAA
    packet[packet_length++] = dev_addr[3];
    packet[packet_length++] = dev_addr[2];
    packet[packet_length++] = dev_addr[1];
    packet[packet_length++] = dev_addr[0];
#else
    packet[packet_length++] = DevAddr[3];
    packet[packet_length++] = DevAddr[2];
    packet[packet_length++] = DevAddr[1];
    packet[packet_length++] = DevAddr[0];
#endif // OTAA

    // Frame control
    packet[packet_length] = 0;
    if (mAdrEnabled) {
        packet[packet_length] |= LORAWAN_FCTRL_ADR;

        if (mAdrAckCounter >= LORAWAN_ADR_ACK_LIMIT && mDataRate != SF12BW125) {
            packet[packet_length] |= LORAWAN_FCTRL_ADR_ACK_REQ;
        }
    }
    packet[packet_length++] |= mPendingFopts.length;

    // Uplink frame counter
    packet[packet_length++] = mTxFrameCounter & 0xFF;
    packet[packet_length++] = mTxFrameCounter >> 8;

    // Frame options
    for (uint8_t i = 0; i < mPendingFopts.length; i++) {
        packet[packet_length++] = mPendingFopts.fopts[i];
    }

    // Clear fopts
    memset(mPendingFopts.fopts, 0, mPendingFopts.length);
    mPendingFopts.length = 0;

    // Frame port
    packet[packet_length++] = fport;

    // Copy payload
    for (uint8_t i = 0; i < payload_length; i++) {
        packet[packet_length++] = payload[i];
    }

    // Calculate MIC
    CalculateMessageMic(packet, mic, packet_length, mTxFrameCounter, LORAWAN_DIRECTION_UP);
    for (uint8_t i = 0; i < 4; i++) {
        packet[packet_length++] = mic[i];
    }

    mChannel = mPseudoByte & 0x03;
    RfmSendPacket(packet, packet_length, mChannel, mDataRate, true);
}

/*
*****************************************************************************************
* Description : Function handles sending data and downlink windows
*
* Arguments   : port FPort of the frame
*               *payload pointer to the array of data that will be transmitted
*               payload_length nuber of bytes to be transmitted
*****************************************************************************************
*/
void SlimLoRa::SendData(uint8_t fport, uint8_t *payload, uint8_t payload_length) {
    Transmit(fport, payload, payload_length);

    if (ProcessDownlink(1)) {
        ProcessDownlink(2);
    }
}

/*
*****************************************************************************************
* Description : Function used to encrypt and decrypt the data in a LoRaWAN data message
*
* Arguments   : *payload pointer to the data to de/encrypt
*               payload_length number of bytes to process
*               frame_counter Frame counter of upstream frames
*               direction Direction of message
*****************************************************************************************
*/
void SlimLoRa::EncryptPayload(uint8_t *payload, uint8_t payload_length, unsigned int frame_counter, uint8_t direction) {
    uint8_t block_count = 0;
    uint8_t incomplete_block_size = 0;

    uint8_t block_a[16];

#if OTAA
    uint8_t dev_addr[4], app_s_key[16];
    GetDevAddr(dev_addr);
    GetAppSKey(app_s_key);
#endif // OTAA

    // Calculate number of blocks
    block_count = payload_length / 16;
    incomplete_block_size = payload_length % 16;
    if (incomplete_block_size != 0) {
        block_count++;
    }

    for (uint8_t i = 1; i <= block_count; i++) {
        block_a[0] = 0x01;
        block_a[1] = 0x00;
        block_a[2] = 0x00;
        block_a[3] = 0x00;
        block_a[4] = 0x00;

        block_a[5] = direction;

#if OTAA
        block_a[6] = dev_addr[3];
        block_a[7] = dev_addr[2];
        block_a[8] = dev_addr[1];
        block_a[9] = dev_addr[0];
#else
        block_a[6] = DevAddr[3];
        block_a[7] = DevAddr[2];
        block_a[8] = DevAddr[1];
        block_a[9] = DevAddr[0];
#endif // OTAA

        block_a[10] = frame_counter & 0xFF;
        block_a[11] = frame_counter >> 8;

        block_a[12] = 0x00; // Frame counter upper bytes
        block_a[13] = 0x00;

        block_a[14] = 0x00;

        block_a[15] = i;

        // Calculate S
#if OTAA
        AesEncrypt(app_s_key, block_a);
#else
        AesEncrypt(AppSKey, block_a);
#endif // OTAA

        // Check for last block
        if (i != block_count) {
            for (uint8_t j = 0; j < 16; j++) {
                *payload ^= block_a[j];
                payload++;
            }
        } else {
            if (incomplete_block_size == 0) {
                incomplete_block_size = 16;
            }

            for (uint8_t j = 0; j < incomplete_block_size; j++) {
                *payload ^= block_a[j];
                payload++;
            }
        }
    }
}

/*
*****************************************************************************************
* Description : Function used to calculate an AES MIC of given data and key
*
* Arguments   : *key 16 bytes key to use for aes mic
*               *data pointer to the data to process
*               *initial_block pointer to an inital 16 byte block
*               *final_mic 4 byte array for final MIC output
*               data_length number of bytes to process
*****************************************************************************************
*/
void SlimLoRa::CalculateMic(const uint8_t *key, uint8_t *data, uint8_t *initial_block, uint8_t *final_mic, uint8_t data_length) {
    uint8_t key1[16] = {0};
    uint8_t key2[16] = {0};

    uint8_t old_data[16] = {0};
    uint8_t new_data[16] = {0};

    uint8_t block_count = 0;
    uint8_t incomplete_block_size = 0;
    uint8_t block_counter = 1;

    // Calculate number of blocks and blocksize of last block
    block_count = data_length / 16;
    incomplete_block_size = data_length % 16;

    if (incomplete_block_size != 0) {
        block_count++;
    }

    GenerateKeys(key, key1, key2);

    // Copy initial block to old_data if present
    if (initial_block != NULL) {
        for (uint8_t i = 0; i < 16; i++) {
            old_data[i] = *initial_block;
            initial_block++;
        }

        AesEncrypt(key, old_data);
    }

    // Calculate first block_count - 1 blocks
    while (block_counter < block_count) {
        for (uint8_t i = 0; i < 16; i++) {
            new_data[i] = *data;
            data++;
        }

        XorData(new_data, old_data);
        AesEncrypt(key, new_data);

        for (uint8_t i = 0; i < 16; i++) {
            old_data[i] = new_data[i];
        }

        block_counter++;
    }

    // Pad and calculate last block
    if (incomplete_block_size == 0) {
        for (uint8_t i = 0; i < 16; i++) {
            new_data[i] = *data;
            data++;
        }

        XorData(new_data, key1);
        XorData(new_data, old_data);
        AesEncrypt(key, new_data);
    } else {
        for (uint8_t i = 0; i < 16; i++) {
            if (i < incomplete_block_size) {
                new_data[i] = *data;
                data++;
            }

            if (i == incomplete_block_size) {
                new_data[i] = 0x80;
            }

            if (i > incomplete_block_size) {
                new_data[i] = 0x00;
            }
        }

        XorData(new_data, key2);
        XorData(new_data, old_data);
        AesEncrypt(key, new_data);
    }

    final_mic[0] = new_data[0];
    final_mic[1] = new_data[1];
    final_mic[2] = new_data[2];
    final_mic[3] = new_data[3];

    mPseudoByte = final_mic[3];
}

/*
*****************************************************************************************
* Description : Function used to calculate the AES MIC of a LoRaWAN message
*
* Arguments   : *data pointer to the data to process
*               final_mic 4 byte array for final MIC output
*               data_length number of bytes to process
*               frame_counter  Frame counter of upstream frames
*               direction of msg is up
*****************************************************************************************
*/
void SlimLoRa::CalculateMessageMic(uint8_t *data, uint8_t *final_mic, uint8_t data_length, unsigned int frame_counter, uint8_t direction) {
    uint8_t block_b[16];
#if OTAA
    uint8_t dev_addr[4], nwk_s_key[16];

    GetDevAddr(dev_addr);
    GetNwkSEncKey(nwk_s_key);
#endif // OTAA

    block_b[0] = 0x49;
    block_b[1] = 0x00;
    block_b[2] = 0x00;
    block_b[3] = 0x00;
    block_b[4] = 0x00;

    block_b[5] = direction;

#if OTAA
    block_b[6] = dev_addr[3];
    block_b[7] = dev_addr[2];
    block_b[8] = dev_addr[1];
    block_b[9] = dev_addr[0];
#else
    block_b[6] = DevAddr[3];
    block_b[7] = DevAddr[2];
    block_b[8] = DevAddr[1];
    block_b[9] = DevAddr[0];
#endif // OTAA

    block_b[10] = frame_counter & 0xFF;
    block_b[11] = frame_counter >> 8;

    block_b[12] = 0x00; // Frame counter upper bytes
    block_b[13] = 0x00;

    block_b[14] = 0x00;
    block_b[15] = data_length;

#if OTAA
    CalculateMic(nwk_s_key, data, block_b, final_mic, data_length);
#else
    CalculateMic(NwkSKey, data, block_b, final_mic, data_length);
#endif // OTAA
}

/*
*****************************************************************************************
* Description : Function used to generate keys for the MIC calculation
*
* Arguments   : *key1 pointer to Key1
*               *key2 pointer ot Key2
*****************************************************************************************
*/
void SlimLoRa::GenerateKeys(const uint8_t *key, uint8_t *key1, uint8_t *key2) {
    uint8_t msb_key;

    // Encrypt the zeros in key1 with the NwkSkey
    AesEncrypt(key, key1);

    // Create key1
    // Check if MSB is 1
    if ((key1[0] & 0x80) == 0x80) {
        msb_key = 1;
    } else {
        msb_key = 0;
    }

    // Shift key1 one bit left
    ShiftLeftData(key1);

    // if MSB was 1
    if (msb_key == 1) {
        key1[15] = key1[15] ^ 0x87;
    }

    // Copy key1 to key2
    for (uint8_t i = 0; i < 16; i++) {
        key2[i] = key1[i];
    }

    // Check if MSB is 1
    if ((key2[0] & 0x80) == 0x80) {
        msb_key = 1;
    } else {
        msb_key = 0;
    }

    // Shift key2 one bit left
    ShiftLeftData(key2);

    // Check if MSB was 1
    if (msb_key == 1) {
        key2[15] = key2[15] ^ 0x87;
    }
}

void SlimLoRa::ShiftLeftData(uint8_t *data) {
    uint8_t overflow = 0;

    for (uint8_t i = 0; i < 16; i++) {
        // Check for overflow on next byte except for the last byte
        if (i < 15) {
            // Check if upper bit is one
            if ((data[i + 1] & 0x80) == 0x80) {
                overflow = 1;
            } else {
                overflow = 0;
            }
        } else {
            overflow = 0;
        }

        // Shift one left
        data[i] = (data[i] << 1) + overflow;
    }
}

void SlimLoRa::XorData(uint8_t *new_data, uint8_t *old_data) {
    for (uint8_t i = 0; i < 16; i++) {
        new_data[i] = new_data[i] ^ old_data[i];
    }
}

/*
*****************************************************************************************
* Title        : AesEncrypt
* Description  :
*****************************************************************************************
*/
void SlimLoRa::AesEncrypt(const uint8_t *key, uint8_t *data) {
    uint8_t round;
    uint8_t round_key[16];
    uint8_t state[4][4];

    // Copy input to state arry
    for (uint8_t column = 0; column < 4; column++) {
        for (uint8_t row = 0; row < 4; row++) {
            state[row][column] = data[row + (column << 2)];
        }
    }

    // Copy key to round key
    memcpy(&round_key[0], &key[0], 16);

    // Add round key
    AesAddRoundKey(round_key, state);

    // Preform 9 full rounds with mixed collums
    for (round = 1; round < 10; round++) {
        // Perform Byte substitution with S table
        for (uint8_t column = 0; column < 4; column++) {
            for (uint8_t row = 0; row < 4; row++) {
                state[row][column] = AesSubByte(state[row][column]);
            }
        }

        // Perform row Shift
        AesShiftRows(state);

        // Mix Collums
        AesMixCollums(state);

        // Calculate new round key
        AesCalculateRoundKey(round, round_key);

        // Add the round key to the round_key
        AesAddRoundKey(round_key, state);
    }

    // Perform Byte substitution with S table whitout mix collums
    for (uint8_t column = 0; column < 4; column++) {
        for (uint8_t row = 0; row < 4; row++) {
            state[row][column] = AesSubByte(state[row][column]);
        }
    }

    // Shift rows
    AesShiftRows(state);

    // Calculate new round key
    AesCalculateRoundKey(round, round_key);

    // Add round key
    AesAddRoundKey(round_key, state);

    // Copy the state into the data array
    for (uint8_t column = 0; column < 4; column++) {
        for (uint8_t row = 0; row < 4; row++) {
            data[row + (column << 2)] = state[row][column];
        }
    }
}

/*
*****************************************************************************************
* Title       : AesAdd_Round_Key
* Description :
*****************************************************************************************
*/
void SlimLoRa::AesAddRoundKey(uint8_t *round_key, uint8_t (*state)[4]) {
    for (uint8_t column = 0; column < 4; column++) {
        for (uint8_t row = 0; row < 4; row++) {
            state[row][column] ^= round_key[row + (column << 2)];
        }
    }
}

/*
*****************************************************************************************
* Title       : AesSub_Byte
* Description :
*****************************************************************************************
*/
uint8_t SlimLoRa::AesSubByte(uint8_t byte) {
    // uint8_t S_Row, S_Collum;
    // uint8_t S_Byte;

    // S_Row    = ((byte >> 4) & 0x0F);
    // S_Collum = ((byte >> 0) & 0x0F);
    // S_Byte   = S_Table[S_Row][S_Collum];

    // return S_Table[((byte >> 4) & 0x0F)][((byte >> 0) & 0x0F)]; // original
    return pgm_read_byte(&(S_Table[((byte >> 4) & 0x0F)][((byte >> 0) & 0x0F)]));
}

/*
*****************************************************************************************
* Title       : AesShift_Rows
* Description :
*****************************************************************************************
*/
void SlimLoRa::AesShiftRows(uint8_t (*state)[4]) {
    uint8_t buffer;

    // Store firt byte in buffer
    buffer      = state[1][0];
    // Shift all bytes
    state[1][0] = state[1][1];
    state[1][1] = state[1][2];
    state[1][2] = state[1][3];
    state[1][3] = buffer;

    buffer      = state[2][0];
    state[2][0] = state[2][2];
    state[2][2] = buffer;
    buffer      = state[2][1];
    state[2][1] = state[2][3];
    state[2][3] = buffer;

    buffer      = state[3][3];
    state[3][3] = state[3][2];
    state[3][2] = state[3][1];
    state[3][1] = state[3][0];
    state[3][0] = buffer;
}

/*
*****************************************************************************************
* Title       : AesMix_Collums
* Description :
*****************************************************************************************
*/
void SlimLoRa::AesMixCollums(uint8_t (*state)[4]) {
    uint8_t a[4], b[4];

    for (uint8_t column = 0; column < 4; column++) {
        for (uint8_t row = 0; row < 4; row++) {
            a[row] =  state[row][column];
            b[row] = (state[row][column] << 1);

            if ((state[row][column] & 0x80) == 0x80) {
                b[row] ^= 0x1B;
            }
        }

        state[0][column] = b[0] ^ a[1] ^ b[1] ^ a[2] ^ a[3];
        state[1][column] = a[0] ^ b[1] ^ a[2] ^ b[2] ^ a[3];
        state[2][column] = a[0] ^ a[1] ^ b[2] ^ a[3] ^ b[3];
        state[3][column] = a[0] ^ b[0] ^ a[1] ^ a[2] ^ b[3];
    }
}

/*
*****************************************************************************************
* Title       : AesCalculate_Round_Key
* Description :
*****************************************************************************************
*/
void SlimLoRa::AesCalculateRoundKey(uint8_t round, uint8_t *round_key) {
    uint8_t tmp[4];

    // Calculate rcon
    uint8_t rcon = 0x01;
    while (round != 1) {
        uint8_t b = rcon & 0x80;
        rcon = rcon << 1;

        if (b == 0x80) {
            rcon ^= 0x1b;
        }
        round--;
    }

    // Calculate first tmp
    // Copy laste byte from previous key and subsitute the byte, but shift the array contents around by 1.
    tmp[0] = AesSubByte(round_key[12 + 1]);
    tmp[1] = AesSubByte(round_key[12 + 2]);
    tmp[2] = AesSubByte(round_key[12 + 3]);
    tmp[3] = AesSubByte(round_key[12 + 0]);

    // XOR with rcon
    tmp[0] ^= rcon;

    // Calculate new key
    for (uint8_t i = 0; i < 4; i++) {
        for (uint8_t j = 0; j < 4; j++) {
            round_key[j + (i << 2)] ^= tmp[j];
            tmp[j] = round_key[j + (i << 2)];
        }
    }
}

/*
 * EEPROM variables
 */
uint16_t eeprom_lw_tx_frame_counter EEMEM = 0;
uint16_t eeprom_lw_rx_frame_counter EEMEM = 0;
uint8_t eeprom_lw_rx2_data_rate EEMEM = 0;

// TxFrameCounter
inline uint16_t SlimLoRa::GetTxFrameCounter() {
    uint16_t value = eeprom_read_word(&eeprom_lw_tx_frame_counter);

    if (value == 0xFFFF) {
        return 0;
    }

    return value;
}

inline void SlimLoRa::SetTxFrameCounter(uint16_t count) {
    eeprom_write_word(&eeprom_lw_tx_frame_counter, count);
}

// RxFrameCounter
inline uint16_t SlimLoRa::GetRxFrameCounter() {
    uint16_t value = eeprom_read_word(&eeprom_lw_rx_frame_counter);

    if (value == 0xFFFF) {
        return 0;
    }

    return value;
}

inline void SlimLoRa::SetRxFrameCounter(uint16_t count) {
    eeprom_write_word(&eeprom_lw_rx_frame_counter, count);
}

// Rx2DataRate
inline uint8_t SlimLoRa::GetRx2DataRate() {
    uint8_t value = eeprom_read_byte(&eeprom_lw_rx2_data_rate);

    if (value == 0xFF) {
#if OTAA
        return SF12BW125;
#else
        // TTN
        return SF9BW125;
#endif
    }

    return value;
}

inline void SlimLoRa::SetRx2DataRate(uint8_t value) {
    eeprom_write_byte(&eeprom_lw_rx2_data_rate, value);
}

#if OTAA
uint8_t eeprom_lw_dev_addr[4] EEMEM;
uint16_t eeprom_lw_dev_nonce EEMEM = 1;
uint32_t eeprom_lw_join_nonce EEMEM = 0;
uint8_t eeprom_lw_app_s_key[16] EEMEM;
uint8_t eeprom_lw_f_nwk_s_int_key[16] EEMEM;
uint8_t eeprom_lw_s_nwk_s_int_key[16] EEMEM;
uint8_t eeprom_lw_nwk_s_enc_key[16] EEMEM;

// DevAddr
inline void SlimLoRa::GetDevAddr(uint8_t *dev_addr) {
    eeprom_read_block(dev_addr, eeprom_lw_dev_addr, 4);
}

inline void SlimLoRa::SetDevAddr(uint8_t *dev_addr) {
    eeprom_write_block(dev_addr, eeprom_lw_dev_addr, 4);
}

// DevNonce
inline uint16_t SlimLoRa::GetDevNonce() {
    uint16_t value = eeprom_read_word(&eeprom_lw_dev_nonce);

    if (value == 0xFFFF) {
        return 1;
    }

    return value;
}

inline void SlimLoRa::SetDevNonce(uint16_t dev_nonce) {
    eeprom_write_word(&eeprom_lw_dev_nonce, dev_nonce);
}

// JoinNonce
inline uint32_t SlimLoRa::GetJoinNonce() {
    uint32_t value = eeprom_read_dword(&eeprom_lw_join_nonce);

    if (value == 0xFFFFFFFF) {
        return 0;
    }

    return value;
}

inline void SlimLoRa::SetJoinNonce(uint32_t join_nonce) {
    eeprom_write_dword(&eeprom_lw_join_nonce, join_nonce);
}

// AppSKey
inline void SlimLoRa::GetAppSKey(uint8_t *key) {
    eeprom_read_block(key, eeprom_lw_app_s_key, 16);
}

inline void SlimLoRa::SetAppSKey(uint8_t *key) {
    eeprom_write_block(key, eeprom_lw_app_s_key, 16);
}

// FNwkSIntKey
inline void SlimLoRa::GetFNwkSIntKey(uint8_t *key) {
    eeprom_read_block(key, eeprom_lw_f_nwk_s_int_key, 16);
}

inline void SlimLoRa::SetFNwkSIntKey(uint8_t *key) {
    eeprom_write_block(key, eeprom_lw_f_nwk_s_int_key, 16);
}

// SNwkSIntKey
inline void SlimLoRa::GetSNwkSIntKey(uint8_t *key) {
    eeprom_read_block(key, eeprom_lw_s_nwk_s_int_key, 16);
}

inline void SlimLoRa::SetSNwkSIntKey(uint8_t *key) {
    eeprom_write_block(key, eeprom_lw_s_nwk_s_int_key, 16);
}

// NwkSEncKey
inline void SlimLoRa::GetNwkSEncKey(uint8_t *key) {
    eeprom_read_block(key, eeprom_lw_nwk_s_enc_key, 16);
}

inline void SlimLoRa::SetNwkSEncKey(uint8_t *key) {
    eeprom_write_block(key, eeprom_lw_nwk_s_enc_key, 16);
}
#endif // OTAA
