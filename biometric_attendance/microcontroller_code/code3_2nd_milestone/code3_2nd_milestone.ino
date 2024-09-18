#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

HardwareSerial serialPort(2); // use UART2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialPort);
uint8_t id;
uint8_t getFingerprintID();

#define SS_PIN  5  // ESP32 pin GPIO5 
#define RST_PIN 27 // ESP32 pin GPIO27 

// byte readCard[4];
// Replace with your network credentials
const char* ssid = "DESKTOP-AO4A53G 9889";//"Umidigi A3_Pro" ;//"DESKTOP-AO4A53G 9889";
const char* password = "83s9$60Z";//"maxkkott2018"; //"83s9$60Z";

String tagID = "";
String tagCheck = "";
int fingerIDCheck;
// Create instances
MFRC522 rfid(SS_PIN, RST_PIN);

struct Person {
    String name;
    String uid;
    uint8_t fingerprint;
};

std::vector<Person> persons;

void setup() {
  // put your setup code here, to run once:
  // Initiating
  Serial.begin(9600);
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
  finger.emptyDatabase();
}

void loop() {
  // put your main code here, to run repeatedly:
  rfid_fingerprint();
}




void rfid_fingerprint(){
  while (getID()){
    while (!getFingerprintEnroll())
      ; 
    Person p = {"", tagID, id};
    persons.push_back(p);
    printPersonsData();
  }
}


void attendance_rfid_fingerprint(){
    while (checkID()){
      while (!checkFingerprintID())
        ; 
      printParticularPersonData(tagCheck, fingerIDCheck);
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
    return false;
  }
  rfid.PICC_HaltA(); // Stop reading
  rfid.PCD_StopCrypto1(); // stop encryption on PCD
  return true;
}



bool checkID(){
    // Getting ready for Reading PICCs
  if (!rfid.PICC_IsNewCardPresent()) { //If a new PICC placed to RFID reader continue
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
  tagID.toUpperCase();
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
  Serial.println("Ready to enroll a fingerprint!");
  Serial.println("Please type in the ID # (from 1 to 127) you want to save this finger as...");
  id = readnumber();
  if (id == 0 || isFingerprintIdRegistered(id))
  { // ID #0 not allowed, try again!
    Serial.println("ID not allowed already taken");
    return false;
  }
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
    return false;
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
  uint8_t p = finger.getImage();
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image taken");
    break;
  case FINGERPRINT_NOFINGER:
    Serial.println("No finger detected");
    return false;
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return false;
  case FINGERPRINT_IMAGEFAIL:
    Serial.println("Imaging error");
    return false;
  default:
    Serial.println("Unknown error");
    return false;
  }

  // OK success!

  p = finger.image2Tz();
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
  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Found a print match!");
    // found a match!
    Serial.print("Found ID #");
    fingerIDCheck = finger.fingerID;
    Serial.print(finger.fingerID);
    Serial.print(" with confidence of ");
    Serial.println(finger.confidence);
    return true;
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return false;
  }
  else if (p == FINGERPRINT_NOTFOUND)
  {
    Serial.println("Did not find a match");
    return false;
  }
  else
  {
    Serial.println("Unknown error");
    return false;
  }
}




void printParticularPersonData(const String& uid, int fingerprint) {
    for (const auto& person : persons) {
        if (person.uid == uid && person.fingerprint == fingerprint) {
            Serial.println("Person Details:");
            Serial.println("---------------------------");
            Serial.println("Name: " + person.name);
            Serial.println("RFID: " + person.uid);
            Serial.println("Fingerprint ID: " + String(person.fingerprint));
            Serial.println("---------------------------");
            return;  // Exit the function after printing
        }
    }
}




void printPersonsData() {
    Serial.println("Persons Data:");
    for (const auto& person : persons) {
        Serial.println("Name: " + person.name);
        Serial.println("RFID: " + person.uid);
        Serial.println("Fingerprint: " + String(person.fingerprint));
        Serial.println("---------------------------");
    }
}