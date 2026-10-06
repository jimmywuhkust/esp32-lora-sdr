#include <LoRaSDR.h>
#include <ESP32S3Radio.h>

using namespace lora_sdr;
ESP32S3Radio radio;
Config config;
bool ready=false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  config.transport=Transport::DacWindows;
  config.spreadingFactor=7;
  config.codingRate=4;
  Error status=radio.begin();
  ready=status==Error::Ok;
  Serial.println(errorName(status));
  Serial.println("Type s to send one packet. No automatic RF transmission.");
}

void loop() {
  if(!Serial.available()){delay(1);return;}
  if(Serial.read()!='s'||!ready)return;
  const uint8_t payload[]="Hello from XIAO!";
  TxResult result;
  Error status=radio.transmit(payload,sizeof(payload)-1,config,result);
  Serial.printf("TX %s; %.3f ms; late=%u\n",errorName(status),result.packet.airtimeMs,result.lateUpdates);
  Serial.println("Verify exact bytes and CRC on the independent receiver.");
}
