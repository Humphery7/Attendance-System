// Import required libraries
#include <LittleFS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>

HardwareSerial serialPort(2); // use UART2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialPort);
uint8_t id;
uint8_t getFingerprintID();

#define SS_PIN  5  // ESP32 pin GPIO5 
#define RST_PIN 27 // ESP32 pin GPIO27 

#define led_success 13

// byte readCard[4];
// Replace with your network credentials
const char* ssid = "Feys' Samsung Galaxy";//"Feys' Samsung Galaxy"; //"////;//"DESKTOP-AO4A53G 9889";
const char* password = "123456789";//"123456789";// //"83s9$60Z";

String currentCourse = "";
String currentName = "";
String tagID = "";
String tagCheck = "";
int fingerIDCheck;
String lastSentMessage = "";
// Create instances
MFRC522 rfid(SS_PIN, RST_PIN);
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool stateEnroll = false;
bool stateAttendance = false;
uint8_t fingerprint_store;

struct Person {
    String name;
    String course;
    String uid;
    uint8_t fingerprint;
    int attendance;
};




enum MainState {
  IDLE,
  ENROLL,
  ATTENDANCE,
};

enum EnrollSubState {
  ENROLL_INIT,
  ENROLL_WAITING_RFID,
  ENROLL_WAITING_FINGERPRINT,
  ENROLL_PROCESSING
};

enum AttendanceSubState {
  ATTENDANCE_INIT,
  ATTENDANCE_WAITING_RFID,
  ATTENDANCE_WAITING_FINGERPRINT,
  ATTENDANCE_PROCESSING
};

MainState currentMainState = IDLE;
EnrollSubState currentEnrollSubState = ENROLL_INIT;
AttendanceSubState currentAttendanceSubState = ATTENDANCE_INIT;



//create a dynamic array of type Persons
std::vector<Person> persons;





void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, (char*)data);
    if (!error){
      if (doc["type"] == "setName"){
        currentName = doc["name"].as<String>();
        Serial.println("Name set to: " + currentName);
        sendWebMessage("Name set to: " + currentName);
      }else if (doc["type"] == "setCourse"){
        currentCourse = doc["course"].as<String>();
        Serial.println("Course set to: " + currentCourse);
        sendWebMessage("Course set to: " + currentCourse);
      }else if (doc["type"] == "setFingerprintID"){
        fingerprint_store = doc["id"].as<uint8_t>();
        Serial.println("Fingerprint ID set to: " + String(fingerprint_store));
        sendWebMessage("Fingerprint ID set to: " + String(fingerprint_store));
      }
    }else{
        if (strcmp((char*)data, "enroll") == 0) {
        currentMainState = ENROLL;
      } else if (strcmp((char*)data, "attendance") == 0) {
        currentMainState = ATTENDANCE;
      } else if (strcmp((char*)data, "print") == 0) {
        sendPersonsDataToWeb();
      }else if (strcmp((char*)data, "wipe") == 0) { // Handle wipe message
        wipePersonsDataFromFlash();
        persons.clear();
        finger.emptyDatabase();
        sendWebMessage("Database Wiped");
      }
    }
  }
}




void eventHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}




// Save persons data to LittleFS
void savePersonsDataToFlash() {
    File file = LittleFS.open("/persons.json", "w");
    if (!file) {
        Serial.println("Failed to open persons.json for writing");
        return;
    }

    DynamicJsonDocument doc(2048);
    JsonArray array = doc.to<JsonArray>();
    
    for (const auto& person : persons) {
        JsonObject obj = array.createNestedObject();
        obj["name"] = person.name;
        obj["uid"] = person.uid;
        obj["fingerprint"] = person.fingerprint;
        obj["attendance"] = person.attendance;
    }
    
    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to persons.json");
    }
    file.close();
}


// Load persons data from LittleFS
void loadPersonsDataFromFlash() {
    File file = LittleFS.open("/persons.json", "r");
    if (!file) {
        Serial.println("Failed to open persons.json for reading");
        return;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.println("Failed to read from persons.json");
        return;
    }
    
    persons.clear();
    JsonArray array = doc.as<JsonArray>();
    for (JsonObject obj : array) {
        Person p = {obj["name"].as<String>(), "", obj["uid"].as<String>(), obj["fingerprint"], 0};
        Serial.println(obj["name"].as<String>());
        Serial.println(obj["uid"].as<String>());
        Serial.println(obj["fingerprint"].as<String>());
        // person.name = obj["name"].as<String>();
        // person.course = obj["course"].as<String>();
        // person.uid = obj["uid"].as<String>();
        // person.fingerprint = obj["fingerprint"];
        // person.attendance = obj["attendance"];
        persons.push_back(p);
    }
    file.close();
}


// Wipe persons data from LittleFS
void wipePersonsDataFromFlash() {
    if (LittleFS.remove("/persons.json")) {
        Serial.println("persons.json removed");
    } else {
        Serial.println("Failed to remove persons.json");
    }
}



void sendWebMessage(const String& message) {
  DynamicJsonDocument doc(256);
  doc["type"] = "message";
  doc["message"] = message;
  String jsonString;
  serializeJson(doc, jsonString);
  // ws.textAll(jsonString);
    // Check if the new message is different from the last sent message
  if (jsonString != lastSentMessage) {
    ws.textAll(jsonString);
    lastSentMessage = jsonString;  // Update the last sent message
  }
}



// IPAddress local_IP(192, 168, 101, 50);  // Define your desired static IP address
// IPAddress gateway(192, 168, 1, 1);     // Define the gateway (usually your router's IP)
// IPAddress subnet(255, 255, 255, 0);    // Define the subnet mask
// IPAddress primaryDNS(8, 8, 8, 8);      // Optional: Define the primary DNS server
// IPAddress secondaryDNS(8, 8, 4, 4);    // Optional: Define the secondary DNS server



void setup() {
  // put your setup code here, to run once:
  // Initiating
  Serial.begin(9600);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  pinMode(led_success, OUTPUT);
  //set led pins low
  digitalWrite(led_success,LOW);

  //   // Set up the static IP address
  // if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
  //   Serial.println("STA Failed to configure");
  // }

   // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("Connected..!");
  Serial.print("Got IP: ");  Serial.println(WiFi.localIP());

  ws.onEvent(eventHandler);
  server.addHandler(&ws);

  if (!LittleFS.begin()) {
      Serial.println("An error has occurred while mounting LittleFS");
      return;
  }
  Serial.println("LittleFS mounted successfully");

  SPI.begin(); // SPI bus
  rfid.PCD_Init(); // MFRC522
  // set the data rate for the sensor serial port
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

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(LittleFS, "/enroll.html", "text/html");
  });
  
  server.on("/attendance", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(LittleFS, "/attendance.html", "text/html");
  });

  currentCourse = "";
  // Start server
  server.begin();
  loadPersonsDataFromFlash();
}




void loop() {
  ws.cleanupClients();
  // put your main code here, to run repeatedly:

 switch (currentMainState) {
    case IDLE:
      //Do nothing
      break;

    case ENROLL:
      switch (currentEnrollSubState){

        case ENROLL_INIT:
          if (currentName != "") {
            currentEnrollSubState =  ENROLL_WAITING_RFID;
          }
          break;
          
          case ENROLL_WAITING_RFID:
            
            if (getID()) {
              currentEnrollSubState = ENROLL_WAITING_FINGERPRINT;
            }
            break;

          case ENROLL_WAITING_FINGERPRINT:
           
            if (getFingerprintEnroll()) {
                currentEnrollSubState = ENROLL_PROCESSING;
            }
            break;
          
          case ENROLL_PROCESSING:
            Person p = {currentName, "", tagID, id, 0};
            persons.push_back(p);
            printPersonsData();
            currentName = "";
            savePersonsDataToFlash();
            currentEnrollSubState = ENROLL_INIT;
            currentMainState = IDLE;
            break;
        }
      
      break;


    case ATTENDANCE:
      switch(currentAttendanceSubState){
        
        case ATTENDANCE_INIT:
          if (currentCourse != "") {
            currentAttendanceSubState = ATTENDANCE_WAITING_RFID;
          }
          break;

        case ATTENDANCE_WAITING_RFID:
          
          if(checkID()){
            currentAttendanceSubState = ATTENDANCE_WAITING_FINGERPRINT;
            }
          break;

        case ATTENDANCE_WAITING_FINGERPRINT:
          
          if (checkFingerprintID()) {
              currentAttendanceSubState = ATTENDANCE_PROCESSING;
            }
          break;
        
        case ATTENDANCE_PROCESSING:
          updateParticularPersonData(tagCheck, fingerIDCheck);
          printParticularPersonData(tagCheck, fingerIDCheck);
          sendWebMessage("Attendance taken successfully");
          sendPersonsDataToWeb();
          currentMainState = IDLE;
          currentAttendanceSubState = ATTENDANCE_INIT;
          delay(1000);
          digitalWrite(led_success, LOW);
          break;
        }
      break;

  }
}


bool isFingerprintIdRegistered(int id) {
    for (const auto& person : persons) {
        if (person.fingerprint == id) {
            return true;
        }
    }
    return false;
}



bool isFingerPrintImageRegistered(int &p){
  if (p == FINGERPRINT_OK)
  {
    // found a match!
    Serial.println("Found a print match!");
    Serial.print("Found ID #");
    Serial.print(finger.fingerID);
    Serial.print(" with confidence of ");
    Serial.println(finger.confidence);
    return true;
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return true;
  }
  else if (p == FINGERPRINT_NOTFOUND)
  {
    Serial.println("Did not find a match");
    return false;
  }
  else
  {
    Serial.println("Unknown error");
    return true;
  }
}


bool isRfidUidRegistered(const String& uid) {
    for (const auto& person : persons) {
        if (person.uid == uid) {
            return true;
        }
    }
    return false;
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



boolean getID() 
{
  // Getting ready for Reading PICCs
  if (!rfid.PICC_IsNewCardPresent()) { //If a new PICC placed to RFID reader continue
    sendWebMessage("Please Scan RFID tag");
    return false;
  }
  if (!rfid.PICC_ReadCardSerial()) { //Since a PICC placed get Serial and continue
    return false;
  }
  tagID = "";
  for ( uint8_t i = 0; i < 4; i++) { // The MIFARE PICCs that we use have 4 byte UID
    //readCard[i] = mfrc522.uid.uidByte[i];
    tagID.concat(String(rfid.uid.uidByte[i], HEX)); // Adds the 4 bytes in a single String variable
  }
  tagID.toUpperCase();
  Serial.print("tagID :");
  Serial.println(tagID);
  if (isRfidUidRegistered(tagID)){
    Serial.println("This RFID tag is already registered.");
    sendWebMessage("RFID already registered");
    return false;
  }
  rfid.PICC_HaltA(); // Stop reading
  rfid.PCD_StopCrypto1(); // stop encryption on PCD
  return true;
}



boolean checkID(){
    // Getting ready for Reading PICCs
  if (!rfid.PICC_IsNewCardPresent()) { //If a new PICC placed to RFID reader continue
    sendWebMessage("Please Scan RFID tag");
    return false;
  }
  if (!rfid.PICC_ReadCardSerial()) { //Since a PICC placed get Serial and continue
    return false;
  }
  tagCheck = "";
  for ( uint8_t i = 0; i < 4; i++) { // The MIFARE PICCs that we use have 4 byte UID
    //readCard[i] = mfrc522.uid.uidByte[i];
    tagCheck.concat(String(rfid.uid.uidByte[i], HEX)); // Adds the 4 bytes in a single String variable
  }
  tagCheck.toUpperCase();
  Serial.print("tagCheck :");
  Serial.println(tagCheck);
  if (isRfidUidRegistered(tagCheck)){
    Serial.println("This RFID tag is registered.");
    rfid.PICC_HaltA(); // Stop reading
    rfid.PCD_StopCrypto1(); // stop encryption on PCD
    return true;
  }

  return false;
}




uint8_t getFingerprintEnroll()
{
  delay(1000);
  Serial.println("Ready to enroll a fingerprint!");
  Serial.println("Please type in the ID # (from 1 to 127) you want to save this finger as...");
  id = fingerprint_store;
  fingerprint_store = 0;
  if (id == 0 || isFingerprintIdRegistered(id))
  { // ID #0 not allowed, try again!
    Serial.println("ID not allowed already taken");
    sendWebMessage("ID not allowed or already taken");
    return false;
  }

  sendWebMessage("Please place your finger on the sensor");
  Serial.print("Enrolling ID #");
  Serial.println(id);

  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      sendWebMessage("Image taken");
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
    return false;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return false;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    return false;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    return false;
  default:
    Serial.println("Unknown error");
    return false;
  }

  Serial.println("Remove finger");
  sendWebMessage("Remove finger");
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
  sendWebMessage("Place same finger again");
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      sendWebMessage("Image taken");
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
    return false;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return false;
  case FINGERPRINT_FEATUREFAIL:
    Serial.println("Could not find fingerprint features");
    return false;
  case FINGERPRINT_INVALIDIMAGE:
    Serial.println("Could not find fingerprint features");
    return false;
  default:
    Serial.println("Unknown error");
    return false;
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
    return false;
  }
  else if (p == FINGERPRINT_ENROLLMISMATCH)
  {
    Serial.println("Fingerprints did not match");
    return false;
  }
  else
  {
    Serial.println("Unknown error");
    return false;
  }


  p = finger.fingerSearch();
  if (isFingerPrintImageRegistered(p)){
    Serial.print("Fingerprint already registered");
    sendWebMessage("Fingerprint already registered");
    return false;
  }


  Serial.print("ID ");
  Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Stored!");
    sendWebMessage("Stored");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return false;
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not store in that location");
    return false;
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    return false;
  }
  else
  {
    Serial.println("Unknown error");
    return false;
  }

  return true;
}




uint8_t checkFingerprintID()
{
  sendWebMessage("Please place your finger on the sensor");
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK)
    return false;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)
    return false;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK)
    return false;

  // found a match!
  Serial.print("Found ID #");
  Serial.print(finger.fingerID);
  Serial.print(" with confidence of ");
  Serial.println(finger.confidence);
  fingerIDCheck = finger.fingerID;
  if (fingerIDCheck != -1) {
    Serial.print("Found fingerprint with ID #");
    sendWebMessage("Found A Match");
    digitalWrite(led_success, HIGH);
    return true;
  }
  //delay(50);
 
}




void printParticularPersonData(const String& uid, int fingerprint) {
    for (const auto& person : persons) {
        if (person.uid == uid && person.fingerprint == fingerprint) {
            Serial.println("Attendance Marked for Person Details:");
            Serial.println("---------------------------");
            Serial.println("Course: " + person.course);
            Serial.println("Name: " + person.name);
            Serial.println("RFID: " + person.uid);
            Serial.println("Fingerprint ID: " + String(person.fingerprint));
            Serial.println("---------------------------");
            return;  // Exit the function after printing
        }
    }
}


void updateParticularPersonData(const String& uid, int fingerprint) {
    for (auto& person : persons) {
        if (person.uid == uid && person.fingerprint == fingerprint) {
            person.attendance = 1;
            person.course = currentCourse;
            return;  // Exit the function after printing
        }
    }
}


void printPersonsData() {
    Serial.println("Persons Data:");
    for (const auto& person : persons) {
      Serial.println("Course: " + person.course);
        Serial.println("Name: " + person.name);
        Serial.println("RFID: " + person.uid);
        Serial.println("Fingerprint: " + String(person.fingerprint));
        Serial.println("---------------------------");
    }
}



void sendPersonsDataToWeb() {
  String jsonData = "[";
  bool firstEntry = true;
  for (const auto& person : persons) {
    if (person.attendance == 1) {
      if (!firstEntry) {
        jsonData += ",";
      }
      jsonData += "{";
      jsonData += "\"course\":\"" + person.course + "\",";
      jsonData += "\"name\":\"" + person.name + "\",";
      jsonData += "\"uid\":\"" + person.uid + "\",";
      jsonData += "\"fingerprint\":\"" + String(person.fingerprint) + "\"";
      jsonData += "}";
      firstEntry = false;
    }
  }
  jsonData += "]";

  ws.textAll(jsonData);
}