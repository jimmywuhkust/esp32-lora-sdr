#include <LoRaRadio.h>

using namespace lora_sdr;
LoRaRadio radio;
bool ready=false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  LoRaSettings settings;
  settings.frequencyMHz=2440.125;
  settings.spreadingFactor=7;
  settings.codingRate=8;
  settings.transmitPowerPercent=75;
  Error status=radio.begin(settings);
  ready=status==Error::Ok;
  Serial.println(errorName(status));
  Serial.println("Type s to send one packet. No automatic RF transmission.");
}

void loop() {
  if(!Serial.available()){delay(1);return;}
  if(Serial.read()!='s'||!ready)return;
  const uint8_t payload[]="Hello from XIAO!";
  Error status=radio.transmit(payload,sizeof(payload)-1);
  const TxResult& result=radio.lastTransmit();
  delay(2);
  Serial.printf("TX %s; %.3f ms; late=%u\n",errorName(status),result.packet.airtimeMs,result.lateUpdates);
  Serial.println("Verify exact bytes and CRC on the independent receiver.");
  Serial.flush();
}
