// #include <HTTP_Method.h>
// #include <Uri.h>
// #include <WebServer.h>
#include "esp_task_wdt.h"
// #include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

HardwareSerial serialPort(2); // use UART2

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialPort);

uint8_t id = 0;
uint8_t getFingerprintEnroll();


#define SS_PIN  5  // ESP32 pin GPIO5 
#define RST_PIN 27 // ESP32 pin GPIO27 

MFRC522 rfid_(SS_PIN, RST_PIN);

// Replace with your network credentials
const char* ssid = "Umidigi A3_Pro" ;//"DESKTOP-AO4A53G 9889";
const char* password = "maxkkott2018"; //"83s9$60Z";

AsyncWebServer server(80);

struct Person {
    String name;
    String rfid;
    uint8_t fingerprint;
};

std::vector<Person> persons;

void setup() {

    // Initialize the task watchdog timer with a timeout of 10 seconds
    esp_task_wdt_init(10, true);
    
    Serial.begin(115200);
    SPI.begin(); // init SPI busx
    rfid_.PCD_Init(); // init MFRC522

    finger.begin(57600);

    if (finger.verifyPassword())
    {
      Serial.println("Found fingerprint sensor!");
    }
    else
    {
      Serial.println("Did not find fingerprint sensor :(");
      while (1)
      {
        delay(1);
      }
    }

    if (!LittleFS.begin()) {
        Serial.println("An error has occurred while mounting LittleFS");
        return;
    }
    Serial.println("LittleFS mounted successfully");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
        
    }
    Serial.println("Connected to WiFi");
    Serial.print("Got IP: ");  Serial.println(WiFi.localIP());
    printPersonsData();
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/enrollRFID", HTTP_GET, [](AsyncWebServerRequest *request){
      String name;
      if (request->hasParam("name")) {
        name = request->getParam("name")->value();
        String rfid;
        delay(1000);
        rfid = scanRFID();  // Implement this function
        Serial.print("rfid:");
        Serial.println(rfid);
        esp_task_wdt_reset();
        
        // if (!rfid.isEmpty()) {
        //     Person p = {name, rfid, 0};
        //     persons.push_back(p);
        //     request->send(200, "text/plain", "RFID enrolled for " + name);
        // }else{
        //     request->send(500, "text/plain", "RFID enrollment failed");
        // }

        delay(1000);
        uint8_t fingerprint = scanFingerprint();  // Implement this function
        Serial.print("fingerprint: ");
        Serial.println(fingerprint);
        printPersonsData();
        if (fingerprint != 0 && !rfid.isEmpty()) {
          bool personExists = false;
         // Check if person already exists in database
          for (auto &person : persons) {
              if (person.name == name && person.rfid == rfid && person.fingerprint == fingerprint) {
                  personExists = true;
                  break;
                }
              }
          if (!personExists) {
                // If person does not exist, enroll
                Person p = {name, rfid, fingerprint};
                persons.push_back(p);
                printPersonsData();
                request->send(200, "text/plain", "Enrolled RFID and Fingerprint for " + name);
            } else {
                // Person already exists
                request->send(200, "text/plain", "Person already exists in the database");
              }

          } else 
              {
              // If either RFID or fingerprint enrollment failed
              request->send(500, "text/plain", "RFID or Fingerprint enrollment failed");
              }

        } else 
            {
            // If name parameter is missing
            request->send(400, "text/plain", "Missing 'name' parameter");
            }
            
            // Serial.println("Entered main for");
            // for (auto &person : persons) {
            //     esp_task_wdt_reset();
                // Serial.println("entered for:");
            //     if (person.name != name || person.fingerprint != fingerprint || person.rfid != rfid) {
            //       Serial.println("entered if");
            //         Person p = {name, rfid, fingerprint};
            //         persons.push_back(p);
            //         //person.fingerprint = fingerprint;
            //         printPersonsData();
            //         request->send(200, "text/plain", "Fingerprint enrolled for " + name);
            //         // break;
            //     }
            // }
            // esp_task_wdt_reset();

      

        //     Serial.println("Passed for:");
        //     request->send(200, "text/plain", "Person already exists in the database");
        //   } else {
        //     request->send(500, "text/plain", "Fingerprint enrollment failed");
        // }
      // }
    });

    server.on("/enrollFingerprint", HTTP_GET, [](AsyncWebServerRequest *request){
        // String name;
        // if (request->hasParam("name")) {
        //     name = request->getParam("name")->value();
        // }
        // uint8_t fingerprint = scanFingerprint();  // Implement this function
        // if (!fingerprint==0) {
        //     for (auto &person : persons) {
        //         if (person.name == name) {
        //             person.fingerprint = fingerprint;
        //             break;
        //         }
        //     }
        //     request->send(200, "text/plain", "Fingerprint enrolled for " + name);
        // } else {
        //     request->send(500, "text/plain", "Fingerprint enrollment failed");
        // }
    });

    server.on("/takeAttendance", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("name")) {  
        delay(1000);
          String rfid = scanRFID();  // Implement this function
          int fingerprint = scanFingerprint();  // Implement this function

          for (auto &person : persons) {
              if (person.rfid == rfid && person.fingerprint == fingerprint) {
                  request->send(200, "text/plain", "Attendance marked for " + person.name);
                  return;
              }
          }
          request->send(404, "text/plain", "Person not found");
        }
    });

    server.begin();
}





void loop() {
    // Your loop code
}





String scanRFID() {
    // Dummy function to simulate RFID scanning
    // Replace with actual implementation
    if (rfid_.PICC_IsNewCardPresent()) { // new tag is available
      if (rfid_.PICC_ReadCardSerial()) { // NUID has been readed
        MFRC522::PICC_Type piccType = rfid_.PICC_GetType(rfid_.uid.sak);
        Serial.print("RFID/NFC Tag Type: ");
        Serial.println(rfid_.PICC_GetTypeName(piccType));

        // print UID in Serial Monitor in the hex format
        String uid="";// Initialize all elements to 0
        Serial.print("UID:");
        for (int i = 0; i < rfid_.uid.size; i++) {
          if (rfid_.uid.uidByte[i] < 0x10) {
                uid += "0";  // Add leading zero for single digit hex values
              }
          // Serial.print(rfid_.uid.uidByte[i] < 0x10 ? " 0" : " ");
          Serial.print(rfid_.uid.uidByte[i], HEX);
          uid += String(rfid_.uid.uidByte[i], HEX);
        }
        Serial.println();

        rfid_.PICC_HaltA(); // halt PICC
        rfid_.PCD_StopCrypto1(); // stop encryption on PCD
        return uid;
    }
  }
 
}

int scanFingerprint() {
    // Dummy function to simulate fingerprint scanning
    // Replace with actual implementation
  Serial.println("Ready to enroll a fingerprint!");
  Serial.println("Please type in the ID # (from 1 to 127) you want to save this finger as...");
  // delay(2000);
  id = readnumber();
  if (id == 0)
  { // ID #0 not allowed, try again!
    return NULL;
  }
  Serial.print("Enrolling ID #");
  Serial.println(id);

  while (!getFingerprintEnroll())
    ;
  return id;
}



uint8_t getFingerprintEnroll()
{

  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);
  while (p != FINGERPRINT_OK)
  {
    esp_task_wdt_reset();
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.println(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(1);
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    Serial.println("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    return p;
  default:
    Serial.println("Unknown error");
    return p;
  }

  Serial.println("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER)
  {
    p = finger.getImage();
  }
  Serial.print("ID ");
  Serial.println(id);
  p = -1;
  Serial.println("Place same finger again");
  while (p != FINGERPRINT_OK)
  {
    esp_task_wdt_reset();
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.print(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image converted");
    break;
  case FINGERPRINT_IMAGEMESS:
    Serial.println("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    return p;
  default:
    Serial.println("Unknown error");
    return p;
  }

  // OK converted!
  Serial.print("Creating model for #");
  Serial.println(id);

  p = finger.createModel();
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Prints matched!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_ENROLLMISMATCH)
  {
    Serial.println("Fingerprints did not match");
    return p;
  }
  else
  {
    Serial.println("Unknown error");
    return p;
  }

  Serial.print("ID ");
  Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Stored!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not store in that location");
    return p;
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    return p;
  }
  else
  {
    Serial.println("Unknown error");
    return p;
  }

  return true;
}




uint8_t readnumber(void)
{
  uint8_t num = 0;

  while (num == 0)
  {
    while (!Serial.available())
      ;
    num = Serial.parseInt();
  }
  return num;
}



void printPersonsData() {
    Serial.println("Persons Data:");
    for (const auto& person : persons) {
        Serial.println("Name: " + person.name);
        Serial.println("RFID: " + person.rfid);
        Serial.println("Fingerprint: " + String(person.fingerprint));
        Serial.println("---------------------------");
    }
}