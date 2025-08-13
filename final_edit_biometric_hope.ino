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
#include <set> // Include set library for tracking attendance

#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>

HardwareSerial serialPort(2); // use UART2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialPort);
uint8_t id;
uint8_t getFingerprintID();

#define SS_PIN 5   // ESP32 pin GPIO5
#define RST_PIN 27 // ESP32 pin GPIO27

#define led_success 13

String formattedDate;
String timeStamp;

// byte readCard[4];

/* Put your SSID & Password */
const char *ssid = "ESP32";        // Enter SSID here
const char *password = "12345678"; // Enter Password here

const char *COURSES_FILE = "/courses.json";
const char *STUDENTS_FILE = "/students.json";
const char *ENROLLMENT_FILE = "/enrollment.json";

// Initialize empty JSON objects
DynamicJsonDocument coursesDoc(1024);
DynamicJsonDocument studentsDoc(2048);
DynamicJsonDocument enrollmentDoc(2048);

String currentCourse = "";
String courseEnroll = "";
String courseAttendance = "";
String currentName = "";
String tagID = "";
String tagCheck = "";
int fingerIDCheck;
String lastSentMessage = "";
String matricNumber = "";

// Create instances
MFRC522 rfid(SS_PIN, RST_PIN);
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool isAttendanceTaken = false;
bool isCourseAttendanceTaken = false;
bool stateEnroll = false;
bool stateAttendance = false;
uint8_t fingerprint_store;

struct Person
{
  String name;
  String course;
  String uid;
  uint8_t fingerprint;
  int attendance;
  String matric;
  String status;
  String time;
};

struct Student_DATA
{
  String studentID;
  String name;
  String course_code;
  int attendanceAttended;
  float attendancePercentage;
  String matric;
};

std::vector<Student_DATA> studentsDATA; // Global vector to hold all students
std::vector<Student_DATA> eligible;

struct Courses_DATA
{
  String course_code;
  int totalAttendance;
};

std::vector<Courses_DATA> coursesDATA;

struct CourseStudent
{
  String id;   // Student ID
  String name; // Student Name
  String matric;
};

struct CourseStorage
{
  String courseName;                   // Course Name
  std::vector<CourseStudent> students; // List of students in the course
};

// Example of how to store courses
std::vector<CourseStorage> coursesSTORAGE;

std::set<String> attendedStudents;

std::set<String> attendedCourses;

enum MainState
{
  IDLE,
  ENROLL,
  ATTENDANCE,
};

enum EnrollSubState
{
  ENROLL_INIT,
  ENROLL_WAITING_RFID,
  ENROLL_WAITING_FINGERPRINT,
  ENROLL_PROCESSING
};

enum AttendanceSubState
{
  ATTENDANCE_INIT,
  ATTENDANCE_WAITING_RFID,
  ATTENDANCE_WAITING_FINGERPRINT,
  ATTENDANCE_PROCESSING
};

MainState currentMainState = IDLE;
EnrollSubState currentEnrollSubState = ENROLL_INIT;
AttendanceSubState currentAttendanceSubState = ATTENDANCE_INIT;

// create a dynamic array of type Persons
std::vector<Person> persons;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP); // 3600 seconds offset for UTC+1 (Nigerian Time)

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, (char *)data);
    if (!error)
    {
      if (doc["type"] == "setName")
      {
        currentName = doc["name"].as<String>();
        Serial.println("Name set to: " + currentName);
        sendWebMessage("Name set to: " + currentName);
      }
      else if (doc["type"] == "setCourse")
      {
        currentCourse = doc["course"].as<String>();
        currentCourse.toLowerCase();
        Serial.println("Course set to: " + currentCourse);
        sendWebMessage("Course set to: " + currentCourse);
        incrementTotalAttendanceForCourse(currentCourse);
      }
      else if (doc["type"] == "setMatricNumber")
      {
        matricNumber = doc["matricNumber"].as<String>();
        matricNumber.toLowerCase();
        Serial.println("Matric Number set to: " + matricNumber);
        sendWebMessage("matricNumber: " + matricNumber);
      }
      else if (doc["type"] == "setFingerprintID")
      {
        fingerprint_store = doc["id"].as<uint8_t>();
        Serial.println("Fingerprint ID set to: " + String(fingerprint_store));
        sendWebMessage("Fingerprint ID set to: " + String(fingerprint_store));
      }
      else if (doc["type"] == "getAttendance")
      {
        // sendPersonsDataToWeb();

        courseAttendance = doc["data"].as<String>();
        courseAttendance.toLowerCase();
        updateRecord();
        sendStudentsDataToWeb();
        Serial.println(courseAttendance);
      }
      else if (doc["type"] == "setCourseCode")
      {
        // sendPersonsDataToWeb();

        courseEnroll = doc["code"].as<String>();
        courseEnroll.toLowerCase();
        Serial.println(courseEnroll);
        sendWebMessage("Course to be enrolled: " + courseEnroll);
      }
      // ...existing code...
      else if (doc["type"] == "generateExamList")
      {
        String course = doc["course"].as<String>();
        float threshold = doc["threshold"].as<float>();
        filterStudentsForExam(course, threshold);
        sendExamListToWeb(eligible);
      }
      // ...existing code...
      else if (doc["type"] == "getCourseEnrollDetails")
      {
        // sendPersonsDataToWeb();
        // Serial.println(courseEnroll);
        sendCourseEnrollToWeb(courseEnroll);
      }
    }
    else
    {
      if (strcmp((char *)data, "enroll") == 0)
      {
        currentMainState = ENROLL;
      }
      else if (strcmp((char *)data, "attendance") == 0)
      {
        currentMainState = ATTENDANCE;
      }
      else if (strcmp((char *)data, "wipe") == 0)
      { // Handle wipe message
        wipePersonsDataFromFlash();
        persons.clear();
        studentsDATA.clear();
        coursesDATA.clear();
        finger.emptyDatabase();
        sendWebMessage("Database Wiped");
      }
    }
  }
}

void eventHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
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
void savePersonsDataToFlash()
{
  File file = LittleFS.open("/persons.json", "w");
  if (!file)
  {
    Serial.println("Failed to open persons.json for writing");
    return;
  }

  DynamicJsonDocument doc(2048);
  JsonArray array = doc.to<JsonArray>();

  for (const auto &person : persons)
  {
    JsonObject obj = array.createNestedObject();
    obj["name"] = person.name;
    obj["uid"] = person.uid;
    obj["fingerprint"] = person.fingerprint;
    obj["attendance"] = person.attendance;
    obj["matric"] = person.matric;
  }

  if (serializeJson(doc, file) == 0)
  {
    Serial.println("Failed to write to persons.json");
  }
  file.close();
}

void saveStudentsDataToFlash()
{
  File file = LittleFS.open("/students.json", "w");
  if (!file)
  {
    Serial.println("Failed to open students.json for writing");
    return;
  }

  // Increase buffer size to accommodate potentially large data
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.to<JsonArray>();

  // Iterate through each student and save their data
  for (const auto &student : studentsDATA)
  {
    JsonObject obj = array.createNestedObject();
    obj["studentID"] = student.studentID;
    obj["name"] = student.name;
    obj["course_code"] = student.course_code;
    obj["attendanceAttended"] = student.attendanceAttended;
    obj["attendancePercentage"] = student.attendancePercentage;
    obj["matric"] = student.matric;
  }

  // Serialize and save to file with pretty printing for readability
  if (serializeJsonPretty(doc, file) == 0)
  {
    Serial.println("Failed to write to students.json");
  }

  file.close();

  // Optional: Verify file size and print for debugging
  Serial.printf("Saved %d students to students.json\n", studentsDATA.size());
}

void saveCoursesStorageToFlash()
{
  // Open the file for writing
  File file = LittleFS.open("/enrollment.json", "w");
  if (!file)
  {
    Serial.println("Failed to open enrollment.json for writing.");
    return;
  }

  // Create a JSON document to store the data
  DynamicJsonDocument doc(4096); // Adjust size if needed
  JsonArray coursesArray = doc.to<JsonArray>();

  // Iterate through each course in coursesSTORAGE
  for (const auto &course : coursesSTORAGE)
  {
    JsonObject courseObj = coursesArray.createNestedObject();
    courseObj["course_name"] = course.courseName;

    JsonArray studentsArray = courseObj.createNestedArray("students");
    for (const auto &student : course.students)
    {
      JsonObject studentObj = studentsArray.createNestedObject();
      studentObj["id"] = student.id;
      studentObj["name"] = student.name;
      studentObj["matric"] = student.matric;
    }
  }

  // Serialize and write JSON data to the file
  if (serializeJson(doc, file) == 0)
  {
    Serial.println("Failed to write courses data to courses.json.");
  }
  else
  {
    Serial.println("Courses data saved to courses.json successfully.");
  }

  file.close(); // Close the file
}

void saveCoursesDataToFlash()
{
  File file = LittleFS.open("/courses.json", "w");
  if (!file)
  {
    Serial.println("Failed to open courses.json for writing");
    return;
  }

  DynamicJsonDocument doc(4096);
  JsonArray array = doc.to<JsonArray>();

  // Iterate through each course and save their data
  for (const auto &courseData : coursesDATA)
  {
    JsonObject obj = array.createNestedObject();
    obj["course_code"] = courseData.course_code;
    obj["totalAttendance"] = courseData.totalAttendance;
  }

  // Serialize and save to file
  if (serializeJson(doc, file) == 0)
  {
    Serial.println("Failed to write to courses.json");
  }
  file.close();
}

// Load persons data from LittleFS
void loadPersonsDataFromFlash()
{
  File file = LittleFS.open("/persons.json", "r");
  if (!file)
  {
    Serial.println("Failed to open persons.json for reading");
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    Serial.println("Failed to read from persons.json");
    return;
  }

  persons.clear();
  JsonArray array = doc.as<JsonArray>();
  for (JsonObject obj : array)
  {
    Person p = {obj["name"].as<String>(), "", obj["uid"].as<String>(), obj["fingerprint"], 0, obj["matric"].as<String>()};
    Serial.println(obj["name"].as<String>());
    Serial.println(obj["uid"].as<String>());
    Serial.println(obj["matric"].as<String>());
    persons.push_back(p);
  }
  file.close();
}

void loadStudentsDataFromFlash()
{
  File file = LittleFS.open("/students.json", "r");
  if (!file)
  {
    Serial.println("Failed to open students.json for reading");
    return;
  }

  DynamicJsonDocument doc(2048); // Adjust size if needed
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    Serial.println("Failed to read from students.json");
    return;
  }

  studentsDATA.clear(); // Clear any previous data
  JsonArray array = doc.as<JsonArray>();
  for (JsonObject obj : array)
  {
    Student_DATA s;
    s.studentID = obj["studentID"].as<String>();
    s.name = obj["name"].as<String>();
    s.course_code = obj["course_code"].as<String>();
    s.attendanceAttended = obj["attendanceAttended"].as<int>();
    s.matric = obj["matric"].as<String>();

    Serial.println(obj["name"].as<String>());
    Serial.println(obj["studentID"].as<String>());
    Serial.println(obj["course_code"].as<String>());
    Serial.println(obj["attendanceAttended"].as<String>());
    studentsDATA.push_back(s); // Add the student to the global vector
  }

  file.close(); // Close the file
}

void loadCoursesDataFromFlash()
{
  File file = LittleFS.open("/courses.json", "r");
  if (!file)
  {
    Serial.println("Failed to open courses.json for reading");
    return;
  }

  DynamicJsonDocument doc(2048); // Adjust size if needed
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    Serial.println("Failed to read from courses.json");
    return;
  }

  coursesDATA.clear(); // Clear any previous data

  JsonArray array = doc.as<JsonArray>();
  for (JsonObject obj : array)
  {
    Courses_DATA courseData;                                  // Create a new Courses_DATA object
    courseData.course_code = obj["course_code"].as<String>(); // Ensure course_code is a String
    courseData.totalAttendance = obj["totalAttendance"].as<int>();

    coursesDATA.push_back(courseData); // Add the course data to the global vector
  }

  file.close(); // Close the file
}

void loadCoursesStorageFromFlash()
{
  // Open the file for reading
  File file = LittleFS.open("/enrollment.json", "r");
  if (!file)
  {
    Serial.println("Failed to open enrollment.json for reading.");
    return;
  }

  // Create a JSON document to parse the data
  DynamicJsonDocument doc(2048); // Adjust size as needed
  DeserializationError error = deserializeJson(doc, file);

  // Check for deserialization errors
  if (error)
  {
    Serial.print("Failed to read from enrollment.json: ");
    Serial.println(error.c_str());
    file.close();
    return;
  }

  coursesSTORAGE.clear(); // Clear the current in-memory storage

  JsonArray coursesArray = doc.as<JsonArray>();
  for (JsonObject courseObj : coursesArray)
  {
    CourseStorage course;
    course.courseName = courseObj["course_name"].as<String>();

    JsonArray studentsArray = courseObj["students"].as<JsonArray>();
    for (JsonObject studentObj : studentsArray)
    {
      CourseStudent student;
      student.id = studentObj["id"].as<String>();
      student.name = studentObj["name"].as<String>();
      student.matric = studentObj["matric"].as<String>();
      course.students.push_back(student);
    }

    coursesSTORAGE.push_back(course); // Add the course to the storage
    Serial.println("Loaded course: " + course.courseName);
  }

  file.close(); // Close the file
  Serial.println("Courses data loaded successfully.");
}

// Wipe persons data from LittleFS
void wipePersonsDataFromFlash()
{
  if (LittleFS.remove("/persons.json"))
  {
    Serial.println("persons.json removed");
  }
  else
  {
    Serial.println("Failed to remove persons.json");
  }

  if (LittleFS.remove("/students.json"))
  {
    Serial.println("students.json removed");
  }
  else
  {
    Serial.println("Failed to remove students.json");
  }

  if (LittleFS.remove("/courses.json"))
  {
    Serial.println("courses.json removed");
  }
  else
  {
    Serial.println("Failed to remove courses.json");
  }

  if (LittleFS.remove("/enrollment.json"))
  {
    Serial.println("enrollment.json removed");
  }
  else
  {
    Serial.println("Failed to remove enrollment.json");
  }
}

void sendWebMessage(const String &message)
{
  DynamicJsonDocument doc(256);
  doc["type"] = "message";
  doc["message"] = message;
  String jsonString;
  serializeJson(doc, jsonString);
  // ws.textAll(jsonString);
  // Check if the new message is different from the last sent message
  if (jsonString != lastSentMessage)
  {
    ws.textAll(jsonString);
    lastSentMessage = jsonString; // Update the last sent message
  }
}

void recordAttendance(const char *courseCode, const char *studentID)
{
}

/* Put IP Address details */
IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

void setup()
{
  // put your setup code here, to run once:
  // Initiating
  Serial.begin(9600);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  pinMode(led_success, OUTPUT);
  // set led pins low
  digitalWrite(led_success, LOW);

  // Connect to Wi-Fi
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  delay(100);

  Serial.println("");
  Serial.println("Connected..!");
  Serial.print("Got IP: ");
  Serial.println(WiFi.localIP());

  ws.onEvent(eventHandler);
  server.addHandler(&ws);

  if (!LittleFS.begin())
  {
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");

  timeClient.begin();
  timeClient.setTimeOffset(3600);

  SPI.begin();     // SPI bus
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

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/enroll.html", "text/html"); });

  server.on("/attendance", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/attendance.html", "text/html"); });
            
  server.on("/exam", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/exam.html", "text/html");
  });

  currentCourse = "";
  // Start server
  server.begin();
  loadPersonsDataFromFlash();
  loadCoursesStorageFromFlash();
  delay(100);
  loadStudentsDataFromFlash();
  delay(100);
  loadCoursesDataFromFlash();
}

void loop()
{
  ws.cleanupClients();
  // put your main code here, to run repeatedly:

  switch (currentMainState)
  {
  case IDLE:
    // Do nothing
    break;

  case ENROLL:
    switch (currentEnrollSubState)
    {

    case ENROLL_INIT:
      if (currentName != "")
      {
        currentEnrollSubState = ENROLL_WAITING_RFID;
      }
      break;

    case ENROLL_WAITING_RFID:

      if (getID())
      {
        currentEnrollSubState = ENROLL_WAITING_FINGERPRINT;
      }
      break;

    case ENROLL_WAITING_FINGERPRINT:

      if (getFingerprintEnroll())
      {
        currentEnrollSubState = ENROLL_PROCESSING;
      }
      break;

    case ENROLL_PROCESSING:
      Person p = {currentName, "", tagID, id, 0};
      persons.push_back(p);
      printPersonsData();
      enrollStudent(courseEnroll, tagID, currentName, matricNumber);
      savePersonsDataToFlash();
      saveCoursesStorageToFlash();
      currentEnrollSubState = ENROLL_INIT;
      currentMainState = IDLE;
      currentName = "";
      matricNumber = "";
      break;
    }

    break;

  case ATTENDANCE:
    switch (currentAttendanceSubState)
    {

    case ATTENDANCE_INIT:
      if (currentCourse != "")
      {
        currentAttendanceSubState = ATTENDANCE_WAITING_RFID;
      }
      break;

    case ATTENDANCE_WAITING_RFID:

      if (checkID())
      {
        currentAttendanceSubState = ATTENDANCE_WAITING_FINGERPRINT;
      }
      break;

    case ATTENDANCE_WAITING_FINGERPRINT:

      if (checkFingerprintID())
      {
        currentAttendanceSubState = ATTENDANCE_PROCESSING;
      }
      break;

    case ATTENDANCE_PROCESSING:
      updateParticularPersonData(tagCheck, fingerIDCheck);
      if (isAttendanceTaken)
      {
        printParticularPersonData(tagCheck, fingerIDCheck);
        printParticularStudentData(tagCheck);
        sendWebMessage("Attendance taken successfully");
        sendPersonsDataToWeb();
        isAttendanceTaken = false;
      }
      currentMainState = IDLE;
      currentAttendanceSubState = ATTENDANCE_INIT;
      delay(1000);
      digitalWrite(led_success, LOW);
      break;
    }
    break;
  }
}

void getCurrentTime()
{
  timeClient.update();
  // while(!timeClient.update()) {
  //   timeClient.forceUpdate();
  // }

  timeStamp = timeClient.getFormattedTime();
}

bool isFingerprintIdRegistered(int id)
{
  for (const auto &person : persons)
  {
    if (person.fingerprint == id)
    {
      return true;
    }
  }
  return false;
}

bool isFingerPrintImageRegistered(int &p)
{
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

bool isRfidUidRegistered(const String &uid)
{
  for (const auto &person : persons)
  {
    if (person.uid == uid)
    {
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
  if (!rfid.PICC_IsNewCardPresent())
  { // If a new PICC placed to RFID reader continue
    sendWebMessage("Please Scan RFID tag");
    return false;
  }
  if (!rfid.PICC_ReadCardSerial())
  { // Since a PICC placed get Serial and continue
    return false;
  }
  tagID = "";
  for (uint8_t i = 0; i < 4; i++)
  { // The MIFARE PICCs that we use have 4 byte UID
    // readCard[i] = mfrc522.uid.uidByte[i];
    tagID.concat(String(rfid.uid.uidByte[i], HEX)); // Adds the 4 bytes in a single String variable
  }
  tagID.toUpperCase();
  Serial.print("tagID :");
  Serial.println(tagID);
  if (isRfidUidRegistered(tagID))
  {
    Serial.println("This RFID tag is already registered.");
    sendWebMessage("RFID already registered");
    return false;
  }
  rfid.PICC_HaltA();      // Stop reading
  rfid.PCD_StopCrypto1(); // stop encryption on PCD
  return true;
}

boolean checkID()
{
  // Getting ready for Reading PICCs
  if (!rfid.PICC_IsNewCardPresent())
  { // If a new PICC placed to RFID reader continue
    sendWebMessage("Please Scan RFID tag");
    return false;
  }
  if (!rfid.PICC_ReadCardSerial())
  { // Since a PICC placed get Serial and continue
    return false;
  }
  tagCheck = "";
  for (uint8_t i = 0; i < 4; i++)
  { // The MIFARE PICCs that we use have 4 byte UID
    // readCard[i] = mfrc522.uid.uidByte[i];
    tagCheck.concat(String(rfid.uid.uidByte[i], HEX)); // Adds the 4 bytes in a single String variable
  }
  tagCheck.toUpperCase();
  Serial.print("tagCheck :");
  Serial.println(tagCheck);
  if (isRfidUidRegistered(tagCheck))
  {
    Serial.println("This RFID tag is registered.");
    rfid.PICC_HaltA();      // Stop reading
    rfid.PCD_StopCrypto1(); // stop encryption on PCD
    return true;
  }
  sendWebMessage("This RFID tag is not registered");
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
  if (isFingerPrintImageRegistered(p))
  {
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
  if (fingerIDCheck != -1)
  {
    Serial.print("Found fingerprint with ID #");
    sendWebMessage("Found A Match");
    digitalWrite(led_success, HIGH);
    return true;
  }
  // delay(50);
}

void printParticularPersonData(const String &uid, int fingerprint)
{
  for (const auto &person : persons)
  {
    if (person.uid == uid && person.fingerprint == fingerprint)
    {
      Serial.println("Attendance Marked for Person Details:");
      Serial.println("---------------------------");
      Serial.println("Course: " + person.course);
      Serial.println("Name: " + person.name);
      Serial.println("RFID: " + person.uid);
      Serial.println("Fingerprint ID: " + String(person.fingerprint));
      Serial.println("---------------------------");
      return; // Exit the function after printing
    }
  }
}

void printParticularStudentData(const String &studentID)
{
  for (const auto &student : studentsDATA)
  {
    if (student.studentID == studentID)
    {
      Serial.println("Student Details:");
      Serial.println("---------------------------");
      Serial.println("Student ID: " + student.studentID);
      Serial.println("Name: " + student.name);

      Serial.println("Course Code: " + student.course_code);
      Serial.println("Attendance Attended: " + String(student.attendanceAttended));
      Serial.println("Attendance Percentage: " + String(student.attendancePercentage) + "%");
      Serial.println("---------------------------");
      return; // Exit the function after printing
    }
  }
  Serial.println("Student not found.");
}

void printParticularCourseData(const String &courseCode)
{
  for (const auto &courseData : coursesDATA)
  {
    if (courseData.course_code == courseCode)
    {
      Serial.println("Course Details:");
      Serial.println("---------------------------");
      Serial.println("Course Code: " + courseData.course_code);
      Serial.println("Total Attendance: " + String(courseData.totalAttendance));
      Serial.println("---------------------------");
      return; // Exit the function after printing
    }
  }
  Serial.println("Course not found.");
}

// Function to increment total attendance for a particular course
void incrementTotalAttendanceForCourse(const String &courseCode)
{
  if (attendedCourses.find(courseCode) != attendedCourses.end())
  {
    Serial.println("Attendance for this course already in session.");
    sendWebMessage("Attendance for this course already in session");
    isCourseAttendanceTaken = false;
    return;
  }

  for (auto &courseData : coursesDATA)
  {
    if (courseData.course_code == courseCode)
    {
      courseData.totalAttendance++; // Increment totalAttendance for the course
      Serial.println("course total: " + String(courseData.totalAttendance));
      saveCoursesDataToFlash();
      return; // Exit the function once the course is found and updated
    }
  }

  // If course is not found in coursesDATA, add it
  coursesDATA.push_back({courseCode, 1});
  attendedCourses.insert(courseCode);
  isCourseAttendanceTaken = true;
  saveCoursesDataToFlash();
}

void updateParticularPersonData(const String &uid, int fingerprint)
{

  // Check if the student is enrolled in the current course
  bool isEnrolled = false;
  for (const auto &course : coursesSTORAGE)
  {
    if (course.courseName == currentCourse)
    {
      for (const auto &student : course.students)
      {
        if (student.id == uid)
        {
          isEnrolled = true;
          break;
        }
      }
      break;
    }
  }

  if (!isEnrolled)
  {
    Serial.println("Student is not enrolled in the course.");
    sendWebMessage("Student is not enrolled in the course");
    isAttendanceTaken = false;
    return;
  }

  // Check if the student has already been marked present for this session
  if (attendedStudents.find(uid) != attendedStudents.end())
  {
    Serial.println("Attendance already marked for this student in the current session.");
    sendWebMessage("Attendance already marked for this student in the current session");
    isAttendanceTaken = false;
    return;
  }

  for (auto &person : persons)
  {
    if (person.uid == uid && person.fingerprint == fingerprint)
    {
      person.attendance = 1;         // Mark attendance as present
      person.course = currentCourse; // Assign current course
      person.status = "MARK";        // Update status
      getCurrentTime();              // Get current time
      person.time = timeStamp;       // Assign timestamp
      person.matric = matricNumber;

      bool courseFound = false;
      // Update students' data by checking if the student is already enrolled in the course
      for (auto &courseData : studentsDATA)
      {
        if (courseData.studentID == person.uid)
        {
          if (courseData.course_code == currentCourse)
          {
            courseData.attendanceAttended++; // Increment student's attendance for the course
            courseFound = true;
            break;
          }
        }
        if (courseFound)
          break;
      }

      // If course is not found, add it
      if (!courseFound)
      {
        studentsDATA.push_back({person.uid,  // studentID
                                person.name, // name
                                currentCourse,
                                1,
                                100, // attendanceAttended starts at 1, calculate percentage later
                                person.matric});
      }
      // Calculate and update attendance percentage for the student
      for (auto &courseData : studentsDATA)
      {
        if (courseData.studentID == person.uid)
        {
          if (courseData.course_code == currentCourse)
          {
            // Find totalAttendance for the course in coursesDATA
            for (auto &courseDataItem : coursesDATA)
            {
              if (courseDataItem.course_code == currentCourse)
              {
                // Calculate the attendance percentage
                // delay(100);
                // Serial.println("total Attendnace: "+ courseDataItem.totalAttendance);
                // Serial.println("attended : "+courseData.attendanceAttended);
                if (courseDataItem.totalAttendance > 0)
                {
                  courseData.attendancePercentage =
                      (static_cast<float>(courseData.attendanceAttended) / courseDataItem.totalAttendance) * 100;
                }
                else
                {
                  courseData.attendancePercentage = 0;
                  Serial.println("Warning: Total attendance is zero, percentage set to 0.");
                }

                break;
              }
            }
            break;
          }
        }
      }

      // Add the student to the attendedStudents set for the current session
      attendedStudents.insert(uid);
      isAttendanceTaken = true;
      saveStudentsDataToFlash();
      return; // Exit the function after updating
    }
  }

  // If the person wasn't found, handle this case
  Serial.println("Person not found or fingerprint mismatch.");
}

void updateRecord()
{

  for (auto &student : studentsDATA)
  {
    if (student.course_code == courseAttendance)
    {
      // Find totalAttendance for the course in coursesDATA
      for (auto &courseDataItem : coursesDATA)
      {
        if (courseDataItem.course_code == courseAttendance)
        {
          // Calculate the attendance percentage
          Serial.println("total Attendnace: " + String(courseDataItem.totalAttendance));
          Serial.println("attended : " + String(student.attendanceAttended));
          if (courseDataItem.totalAttendance > 0)
          {
            student.attendancePercentage =
                (static_cast<float>(student.attendanceAttended) / courseDataItem.totalAttendance) * 100;
          }
          else
          {
            student.attendancePercentage = 0;
            Serial.println("Warning: Total attendance is zero, percentage set to 0.");
          }

          break;
        }
      }
    }
  }
}

void printPersonsData()
{
  Serial.println("Persons Data:");
  for (const auto &person : persons)
  {
    Serial.println("Course: " + person.course);
    Serial.println("Name: " + person.name);
    Serial.println("RFID: " + person.uid);
    Serial.println("Fingerprint: " + String(person.fingerprint));
    Serial.println("---------------------------");
  }
}

void sendPersonsDataToWeb()
{
  String jsonData = "[";
  bool firstEntry = true;
  for (const auto &person : persons)
  {
    if (person.attendance == 1)
    {
      if (!firstEntry)
      {
        jsonData += ",";
      }
      jsonData += "{";
      jsonData += "\"course\":\"" + person.course + "\",";
      jsonData += "\"name\":\"" + person.name + "\",";
      jsonData += "\"uid\":\"" + person.uid + "\",";
      jsonData += "\"fingerprint\":\"" + String(person.fingerprint) + "\",";
      jsonData += "\"time\":\"" + person.time + "\",";
      jsonData += "\"status\":\"" + person.status + "\"";
      jsonData += "}";
      firstEntry = false;
    }
  }
  jsonData += "]";

  ws.textAll(jsonData);
}

void enrollStudent(String courseCode, String studentID, String studentName, String matric)
{
  // Check if courseCode exists
  for (auto &course : coursesSTORAGE)
  {
    if (course.courseName == courseCode)
    {
      // Check if student is already enrolled
      for (const auto &student : course.students)
      {
        if (student.id == studentID)
        {
          Serial.println("Student already enrolled in this course.");
          return;
        }
      }

      // Add new student to the course
      course.students.push_back({studentID, studentName, matric});
      Serial.println("Student enrolled successfully.");
      return;
    }
  }

  // If courseCode does not exist, create a new course and add the student
  CourseStorage newCourse = {courseCode, {{studentID, studentName, matric}}};
  coursesSTORAGE.push_back(newCourse);
  Serial.println("New course created and student enrolled successfully.");
}

void sendStudentsDataToWeb()
{
  if (courseAttendance.isEmpty())
  {
    sendWebMessage("No course specified for attendance retrieval.");
    return;
  }

  String jsonData = "[";
  bool firstEntry = true;
  bool courseFound = false;

  // Loop through the students data
  for (const auto &student : studentsDATA)
  {
    // Check if the student is enrolled in the specified course
    if (student.course_code == courseAttendance)
    {
      if (!firstEntry)
      {
        jsonData += ",";
      }
      jsonData += "{";
      jsonData += "\"studentID\":\"" + student.studentID + "\",";
      jsonData += "\"name\":\"" + student.name + "\",";
      jsonData += "\"matricNumber\":\"" + student.matric + "\",";
      jsonData += "\"attendanceAttended\":" + String(student.attendanceAttended) + ",";
      jsonData += "\"attendancePercentage\":" + String(student.attendancePercentage);
      jsonData += "}";

      firstEntry = false;
      courseFound = true;
    }
  }

  if (!courseFound)
  {
    sendWebMessage("No data for this course.");
    courseAttendance = " ";
    return;
  }

  jsonData += "]"; // End of JSON array

  // Send the data to the web
  ws.textAll(jsonData);
}

void sendCourseEnrollToWeb(const String &courseName)
{
  // Check if the course name is provided
  if (courseName.isEmpty())
  {
    sendWebMessage("No course specified for retrieval.");
    return;
  }

  String jsonData = "["; // Begin JSON array
  bool firstEntry = true;
  bool courseFound = false;

  // Loop through all the courses in coursesSTORAGE
  for (const auto &course : coursesSTORAGE)
  {
    // Check if the course matches the provided name
    if (course.courseName.equalsIgnoreCase(courseName))
    {
      courseFound = true;

      // Loop through the students in the matched course
      for (const auto &student : course.students)
      {
        // Serial.println(student.id);
        // Serial.println(student.name);
        // Serial.println(student.matric);
        if (!firstEntry)
        {
          jsonData += ",";
        }
        jsonData += "{";
        jsonData += "\"id\":\"" + student.id + "\",";
        jsonData += "\"name\":\"" + student.name + "\",";
        jsonData += "\"matricNumber\":\"" + student.matric + "\"";
        jsonData += "}";

        firstEntry = false;
      }
      break; // Exit the loop since the course is found
    }
  }

  // If the course is not found, send a message
  if (!courseFound)
  {
    sendWebMessage("No data for the specified course.");
    return;
  }

  jsonData += "]"; // End of JSON array

  // Send the data to the web
  ws.textAll(jsonData);
}



// Send exam list to web client
void sendExamListToWeb(const std::vector<Student_DATA> &list)
{
  DynamicJsonDocument doc(4096);
  doc["type"] = "examList";
  JsonArray arr = doc.createNestedArray("list");
  for (const auto &s : list)
  {
    JsonObject obj = arr.createNestedObject();
    obj["studentID"] = s.studentID;
    obj["name"] = s.name;
    obj["matricNumber"] = s.matric;
    obj["attendancePercentage"] = s.attendancePercentage;
  }
  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}
// ...existing code...


// ...existing code...

// Filter students for exam eligibility
void filterStudentsForExam(const String &courseCode, float threshold)
{
  for (const auto &student : studentsDATA)
  {
    if (student.course_code.equalsIgnoreCase(courseCode) &&
        student.attendancePercentage >= threshold)
    {
      eligible.push_back(student);
    }
  }
  // return eligible;
}