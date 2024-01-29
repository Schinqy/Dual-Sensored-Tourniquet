#include <WiFi.h>
#include <WiFiClient.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WebSocketsServer.h>
#include "HX710B.h"


const int DOUT_Pin = 12;   //sensor data pin
const int SCK_Pin  = 14;   //sensor clock pin
HX710B pressure_sensor; 
long offset = -7244154;


const char* ssid = "Tourniquet";
const char* password = "passc0d3";

AsyncWebServer server(80);
WebSocketsServer webSocket(81);

float temperature = 0;
float roomTemperature = 0;
boolean gotRT = false;
float pressure = 0;
float targetTemperature = 0;
float targetPressure = 0;
float previousThreshPressure = 0;
int pumpRelay = 16;
int solenoidRelay = 17;
int heaterRelay = 5;

#define ONE_WIRE_BUS 4 // GPIO pin where DS18B20 is connected
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    // Handle WebSocket events if needed
}

void setup() {
    Serial.begin(115200);
     pressure_sensor.begin(DOUT_Pin, SCK_Pin, 128);
  pressure_sensor.set_offset(offset);
    pinMode(pumpRelay, OUTPUT);
    pinMode(solenoidRelay, OUTPUT);
    digitalWrite(pumpRelay, HIGH);
    digitalWrite(solenoidRelay, HIGH);

    WiFi.softAP(ssid, password);
    Serial.println("SoftAP created with SSID: " + String(ssid));

    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    sensors.begin();

    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/index.html", "text/html");
    });

    server.on("/getTemperature", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/plain", String(temperature).c_str());
    });
     server.on("/getRoomTemperature", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/plain", String(roomTemperature).c_str());
    });

    server.on("/getPressure", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/plain", String(pressure).c_str());
    });

server.on("/adjustTemperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
        targetTemperature = request->getParam("value")->value().toFloat();
        Serial.print("Adjusted Temperature: ");
        Serial.println(targetTemperature);
        request->send(200, "text/plain", "Temperature adjusted");
        webSocket.broadcastTXT(String('{"temperature": ' + String(temperature) + ', "pressure": ' + String(pressure) + '}').c_str());
    } else {
        request->send(400, "text/plain", "Invalid request");
    }
});

server.on("/adjustPressure", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
        targetPressure = request->getParam("value")->value().toFloat();
        Serial.print("Adjusted Pressure: ");
        Serial.println(targetPressure);
        request->send(200, "text/plain", "Pressure adjusted");
        webSocket.broadcastTXT(String('{"temperature": ' + String(temperature) + ', "pressure": ' + String(pressure) + '}').c_str());
    } else {
        request->send(400, "text/plain", "Invalid request");
    }
});


    server.begin();
}

void loop() {
    sensors.requestTemperatures();
    if (pressure_sensor.is_ready()) {
    Serial.print("mmHg: ");
    Serial.println(pressure_sensor.mmHg());
    pressure = pressure_sensor.mmHg();
    if(pressure < 0)
    {
      pressure = 0;
    }
    
  } else {
    Serial.println("Pressure sensor not found.");
   pressure = 0;
  }
    float totalTemp = 0;
    if(gotRT == false){
      for(int i=0; i<=30; i++)
      {
      temperature = sensors.getTempCByIndex(0);
      totalTemp = totalTemp + temperature;
      delay(30);
      }
      roomTemperature = totalTemp / 30;
      gotRT = true;
    }
      temperature = sensors.getTempCByIndex(0);
    
  Serial.println("This is the target temp:" +  String(targetTemperature));
 Serial.println("This is the target pressure:" +  String(targetPressure));
 Serial.println("This is the current temp:" +  String(temperature));
  Serial.println("This is the room temp:" +  String(roomTemperature));
controlTemperature(targetTemperature);
controlPressure(targetPressure);
    // WebSocket handling
    webSocket.loop();

   // delay(1000); // Adjust delay according to your needs
}

void controlTemperature(float tempThresh)
{
  if(temperature != DEVICE_DISCONNECTED_C && temperature > tempThresh)
  {
     Serial.println("Heater OFF");
     digitalWrite(heaterRelay, HIGH);
     
  }
  else if (temperature != DEVICE_DISCONNECTED_C && temperature < tempThresh - 2)
  {
    Serial.println("Heater ON"); 
    digitalWrite(heaterRelay, LOW);
  }
}

void controlPressure(float targetPressure)
{
  const int pressureTolerance = 2; // Adjust this value based on your system requirements

  if (pressure > targetPressure + 15)
  {
    // Current pressure is above the target, deflate - open solenoid and switch off pump
    Serial.println("Pressure Above Target - Deflate");
    digitalWrite(pumpRelay, HIGH);    // Switch OFF PUMP
    digitalWrite(solenoidRelay, HIGH); // Open Solenoid
    delay(20);
    digitalWrite(solenoidRelay, LOW);
  }
  else if (pressure < targetPressure - pressureTolerance)
  {
    // Current pressure is below the target, inflate - close solenoid and switch on pump
    Serial.println("Pressure Below Target - Inflate");
    digitalWrite(pumpRelay, LOW);     // Switch ON PUMP
    digitalWrite(solenoidRelay, LOW); // Close Solenoid
     delay(50);
    digitalWrite(pumpRelay, HIGH);
  }
  else if( pressure >= targetPressure - 25 || pressure <= targetPressure + 25 )
  {
    // Pressure is within an acceptable range, maintain the pressure - switch off pump and close solenoid
    Serial.println("Pressure Within Target Range - Maintain");
    digitalWrite(pumpRelay, HIGH);    // Switch OFF PUMP
    digitalWrite(solenoidRelay, LOW); // Close Solenoid
  }
}

//void controlPressure(float preshThresh)
//{
//  float pressureChange = preshThresh - previousThreshPressure;
//int pumpDelay = mmHgToDelay(pressureChange);
//    if(pumpDelay > 0)
//    {
//      //Inflate
//      digitalWrite(pumpRelay, LOW); // Switch ON PUMP
//      digitalWrite(solenoidRelay, LOW); // Close Solenoid
//      delay(pumpDelay);
//      digitalWrite(pumpRelay, HIGH); // Switch OFF PUMP
//    }
//    else
//    {
//      //Deflate
//      digitalWrite(pumpRelay, HIGH); // Switch ON PUMP
//      digitalWrite(solenoidRelay, HIGH); // Close Solenoid
//      int pumpDelayDeflate =  -1 * pumpDelay; 
//      delay(pumpDelayDeflate);
//      digitalWrite(solenoidRelay, LOW); // Switch OFF PUMP
//      
//    }
//
//previousThreshPressure = preshThresh;
//}



float mmHgToDelay( float pressure_mmHg )
{
  // say 1mmHg = 200ms
  float calibrationFactor = 200;
  return pressure_mmHg*calibrationFactor;
}



//         Notes: 
// 197 to 250mmHg recommended pressure for forearm
// 35-40 temperature recommended for forearm
