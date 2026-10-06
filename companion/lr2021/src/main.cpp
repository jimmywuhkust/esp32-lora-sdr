// Independent receiver using public RadioLib; no private AeroLink source.
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

SPIClass radioSPI(HSPI);
LR2021 radio = new Module(8,14,12,13,radioSPI);
volatile bool packetPending=false;
bool ready=false;
int16_t startupStatus=RADIOLIB_ERR_UNKNOWN;
void IRAM_ATTR packetInterrupt() { packetPending=true; }

void setup() {
    Serial.begin(115200);delay(1200);
    radioSPI.begin(9,11,10,8);
    // LR2021 DIO9 is wired to ESP32 GPIO14 on this verified board.
    radio.irqDioNum=9;
    int16_t status=radio.begin(2440.125f,203.125f,7,8,0x12,-12,16,1.8f);
    if(status==RADIOLIB_ERR_NONE)status=radio.explicitHeader();
    if(status==RADIOLIB_ERR_NONE)status=radio.setCRC(2);
    if(status==RADIOLIB_ERR_NONE)status=radio.setRxBoostedGainMode(4);
    radio.setPacketReceivedAction(packetInterrupt);
    if(status==RADIOLIB_ERR_NONE)status=radio.startReceive();
    ready=status==RADIOLIB_ERR_NONE;
    startupStatus=status;
    Serial.printf("LR2021_PUBLIC status=%d freq=2440.125 bw=203.125 sf=7 crc=required\n",status);
}

void loop() {
    if(Serial.available()) {
        String command=Serial.readStringUntil('\n');command.trim();
        if(command=="INFO")Serial.printf("LR2021_PUBLIC status=%d ready=%d irq=%08lx gpio=%d\n",startupStatus,ready,radio.getIrqFlags(),digitalRead(14));
        else if(command.startsWith("BW ")) {
            unsigned bw;char extra;
            if(sscanf(command.c_str(),"BW %u %c",&bw,&extra)!=1||(bw!=203125&&bw!=406250&&bw!=812500)){Serial.println("ERR BW");return;}
            int16_t status=radio.standby();
            if(status==0)status=radio.setBandwidth(bw/1000.0f);
            packetPending=false;
            if(status==0)status=radio.startReceive();
            ready=status==0;Serial.printf("BW %u status=%d\n",bw,status);
        }
        else if(command.startsWith("SF ")) {
            unsigned sf;char extra;
            if(sscanf(command.c_str(),"SF %u %c",&sf,&extra)!=1||sf<7||sf>9){Serial.println("ERR SF");return;}
            int16_t status=radio.standby();
            if(status==0)status=radio.setSpreadingFactor(sf);
            packetPending=false;
            if(status==0)status=radio.startReceive();
            ready=status==0;Serial.printf("SF %u status=%d\n",sf,status);
        }
    }
    if(ready&&!packetPending&&(radio.getIrqFlags()&(RADIOLIB_LR2021_IRQ_RX_DONE|RADIOLIB_LR2021_IRQ_CRC_ERROR|RADIOLIB_LR2021_IRQ_TIMEOUT)))packetPending=true;
    if(!ready||!packetPending){delay(1);return;}
    packetPending=false;
    // RadioLib's blocking receive also enters standby before draining data.
    // Re-entering RX from the continuous-RX state otherwise fails on this chip.
    int16_t standbyStatus=radio.standby();
    if(standbyStatus!=RADIOLIB_ERR_NONE){ready=false;Serial.printf("RX_STOP standby=%d\n",standbyStatus);return;}
    // Query CRC presence before readData clears IRQ/FIFO state. A successful
    // read without a CRC is deliberately rejected as packet proof.
    uint8_t codingRate=0;bool hasCRC=false;
    int16_t headerStatus=radio.getLoRaRxHeaderInfo(&codingRate,&hasCRC);
    size_t length=radio.getPacketLength();uint8_t payload[255]={};
    int16_t status=length>=1&&length<=sizeof(payload)?radio.readData(payload,length):RADIOLIB_ERR_PACKET_TOO_LONG;
    bool valid=status==RADIOLIB_ERR_NONE&&headerStatus==RADIOLIB_ERR_NONE&&hasCRC&&length>=1&&length<=255;
    Serial.printf("RX status=%d header=%d crc_present=%d crc_ok=%d bytes=%u rssi=%.1f snr=%.1f hex=",
        status,headerStatus,hasCRC,valid,unsigned(length),radio.getRSSI(),radio.getSNR());
    if(length<=255)for(size_t i=0;i<length;i++)Serial.printf("%02x",payload[i]);
    Serial.println();
    status=radio.startReceive();
    if(status!=RADIOLIB_ERR_NONE){ready=false;Serial.printf("RX_STOP error=%d\n",status);}
}
