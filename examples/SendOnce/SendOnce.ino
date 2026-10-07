#include <LoRaRadio.h>

using namespace lora_sdr;
LoRaRadio radio;
bool ready=false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Error status=radio.begin(2440.125);
  if(status==Error::Ok)status=radio.setSpreadingFactor(7);
  if(status==Error::Ok)status=radio.setBandwidth(203.125);
  if(status==Error::Ok)status=radio.setCodingRate(8);
  if(status==Error::Ok)status=radio.setTransmitPowerPercent(75);
  ready=status==Error::Ok;
  Serial.println(errorName(status));
  Serial.println("Type s to send one packet. No automatic RF transmission.");
}

void loop() {
  if(!Serial.available()){delay(1);return;}
  if(Serial.read()!='s'||!ready)return;
  const uint8_t payload[]="Hello from XIAO!";
  Error status=radio.send(payload,sizeof(payload)-1);
  const TxResult& result=radio.lastTransmit();
  delay(2);
  Serial.printf("TX %s; %.3f ms; late=%u\n",errorName(status),result.packet.airtimeMs,result.lateUpdates);
  Serial.println("Verify exact bytes and CRC on the independent receiver.");
  Serial.flush();
}
