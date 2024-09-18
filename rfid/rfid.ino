#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN  5  // ESP32 pin GPIO5 
#define RST_PIN 27 // ESP32 pin GPIO27 

byte readCard[4];
String MasterTag = "7368CF4";	// REPLACE this Tag ID with your Tag ID!!!
String tagID = "";

// Create instances
MFRC522 rfid(SS_PIN, RST_PIN);

void setup() 
{
  // Initiating
  Serial.begin(9600);
  SPI.begin(); // SPI bus
  rfid.PCD_Init(); // MFRC522
}

void loop() 
{
  
  //Wait until new tag is available
  while (getID()) 
  {
    
    if (tagID == MasterTag) 
    {
      
      Serial.print(" Access Granted! :");
      // You can write any code here like opening doors, switching on a relay, lighting up an LED, or anything else you can think of.
    }
    else
    {
      Serial.print(" Access Denied! :");
    }
    Serial.println(tagID);
    

  }
}

//Read new tag if available
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
  rfid.PICC_HaltA(); // Stop reading
  rfid.PCD_StopCrypto1(); // stop encryption on PCD
  return true;
}