// Bugs to fix
// TODO: occasionally the loging timer goes to 42947XXX seconds
// TODO: button press is not always detected
// X TODO: temperature is not displayed for all servents at once
// X TODO: temperature values from offline-targets are replaced with the value of the last online-target on the display
// X TODO: temperature values from offline-targets are replaced with the value of the last online-target in the log file
// X TODO: display does not reset propperly after a fatal error-display
// TODO: when connection is lost, the display does still show the last temperature values
// TODO: temperture values are not compleatly over written on the display (could be a problem, when the number of digits reduces)
// TODO: display and LED freezes during the temperature request (display "Updating Temperature")
// TODO: status LED and ERROR message accasionally (and only very briefly) display "No connection" even if the connection is established



#include <esp_now.h>
#include <WiFi.h>
#include <RTClib.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>

//User variables
int sendTimeout         = 1000;     //Timeout for waiting for a servent response data in ms
int logIntervall        = 10000;    //Log intervall in ms (>= 10000 ms = 10s)
int pingCheckIntervall  = 1000;     //Ping check intervall in ms
int tempUpdateIntervall = 10000;    //Temperature update intervall in ms

// structure to send data
typedef struct struct_message {
    int actionID;
    float value;
} struct_message;
struct_message TXdata;


typedef struct temp {
    int actionID;
    float sens1;
    float sens2;
    float sens3;
    float sens4;
    float sens5;
    float sens6;
    float sens7;
    float sens8;
    float sens9;
} temp;
temp RXData;

// system variables
volatile bool messageReceived   = false;
volatile int receivedActionID   = 0;
int numConnections              = 0;
int timeLeft                    = 0;
char timestamp[19];
char fileName[24];
bool connectionStatus           = false;
bool logState                   = false;
esp_err_t lastSendStatus        = ESP_FAIL;
temp receivedData;
File file;

// Pin definitions
#define CS_PIN 5
#define LED_PIN 4
#define BUTTON_PIN 0


Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);  // Create an instance of the Adafruit_NeoPixel class

uint8_t broadcastAddresses[][6] = {
    {0x48, 0xE7, 0x29, 0x8C, 0x79, 0x68},
    {0x48, 0xE7, 0x29, 0x8C, 0x73, 0x18},
    {0x4C, 0x11, 0xAE, 0x65, 0xBD, 0x54},
    {0x48, 0xE7, 0x29, 0x8C, 0x72, 0x50}    
};
esp_now_peer_info_t peerInfo[4];

RTC_DS3231 rtc;

LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 20 chars and 4 line display


void sendLogState(bool logState){
        if(logState){
        TXdata.actionID = 1002;
        for (int i = 0; i < 4; i++) {
            esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
        }
    }else{
        TXdata.actionID = 1003;
        for (int i = 0; i < 4; i++) {
            esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
        }
    }
}


void buttonState(){ //MARK: Button state
    // Serial.println("Button Check");
    if (digitalRead(BUTTON_PIN) == LOW){    // If the button is pressed (dont get confused, the button is active low)
        logState = !logState;
        while(digitalRead(BUTTON_PIN) == LOW){
            //delay(60);
        }
        sendLogState(logState);
        if (logState){
            timeLeft = 0;
        }
    }
}


void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {

    Serial.print(mac_addr[0], HEX); Serial.print(":");
    Serial.print(mac_addr[1], HEX); Serial.print(":");
    Serial.print(mac_addr[2], HEX); Serial.print(":");
    Serial.print(mac_addr[3], HEX); Serial.print(":");
    Serial.print(mac_addr[4], HEX); Serial.print(":");
    Serial.print(mac_addr[5], HEX); Serial.print(" --> ");

    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("Delivery Success");
        connectionStatus = true;
    } else {
        Serial.println("Delivery Fail");
        connectionStatus = false;
    }
    lastSendStatus = status == ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL;
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    memcpy(&receivedData, incomingData, sizeof(receivedData));
    receivedActionID = receivedData.actionID;
    messageReceived = true;
}

void SerialUserInput() {
    while (!Serial.available()) {
        // Wait for user input
    }
    int userActionID = Serial.parseInt();
    TXdata.actionID = userActionID != 0 ? userActionID : 1; // Use user input if available, otherwise use default value

    TXdata.value = 2.0; // Replace with your actual default value

    for (int i = 0; i < 4; i++) {
        esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));
    }
}

void updateConnectionStatus(bool status, int targetID) { //MARK: Update connection status
    lcd.setCursor(0, 1);
    lcd.print("S1:");
    lcd.setCursor(5, 1);
    lcd.print("S2:");
    lcd.setCursor(10, 1);
    lcd.print("S3:");
    lcd.setCursor(15, 1);
    lcd.print("S4:");

    
    switch (targetID)
    {
        case 1:
            lcd.setCursor(3, 1);
            break;

        case 2:
            lcd.setCursor(8, 1);
            break;

        case 3:
            lcd.setCursor(13, 1);
            break;

        case 4:
            lcd.setCursor(18, 1);
            break;
    }

    if (status) {
        lcd.write(byte(0)); // Tick mark
    } else {
        lcd.print("x");
    }
}


bool checkConnection(int locTargetID) { //MARK: Check connection
    esp_err_t result;
    uint8_t testArray[1] = {0}; // Test data to send

    // structure to Action Code as a connection test
    typedef struct struct_message {
        int actionID;
    } struct_message;
    struct_message testData;

    testData.actionID = 1001;

    esp_now_register_send_cb(OnDataSent);    // Register send callback
    result = esp_now_send(broadcastAddresses[locTargetID-1], (uint8_t *) &testData, sizeof(testData));

    if (result == ESP_OK) {     // Check if the message was queued for sending successfully
        delay(40);              // NEEDED: Delay to allow the send callback to be called
        if (lastSendStatus == ESP_OK) {        // Check the send status
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}


bool waitForActionID(int actionID, int targetID) { //MARK: Wait for action ID
    
    // if(checkConnection(targetID)){  //Only request data if the connection is established
        unsigned long startTime = millis();
        bool connectionStatus = false;

        while (!messageReceived || receivedActionID != actionID) {
            if (millis() - startTime > sendTimeout) {
                Serial.println("Timeout waiting for action ID on target: " + String(targetID));
                connectionStatus = false;
                break;
            }else{
                connectionStatus = true;
            }
        }
        messageReceived = false; // Reset for next message

        //updateConnectionStatus(connectionStatus, targetID);
        return (true);
    }
    // return (false);
// }


String tempToString(temp t, String timestamp, int serventID) {//MARK: To String
    String data = "";
    data += timestamp + "," + String(serventID) + ",1," + String(t.sens1) + "\n";
    data += timestamp + "," + String(serventID) + ",2," + String(t.sens2) + "\n";
    data += timestamp + "," + String(serventID) + ",3," + String(t.sens3) + "\n";
    data += timestamp + "," + String(serventID) + ",4," + String(t.sens4) + "\n";
    data += timestamp + "," + String(serventID) + ",5," + String(t.sens5) + "\n";
    data += timestamp + "," + String(serventID) + ",6," + String(t.sens6) + "\n";
    data += timestamp + "," + String(serventID) + ",7," + String(t.sens7) + "\n";
    data += timestamp + "," + String(serventID) + ",8," + String(t.sens8) + "\n";
    data += timestamp + "," + String(serventID) + ",9," + String(t.sens9) + "\n";
    return data;
}


const char* get_timestamp() {
    DateTime now = rtc.now();
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return timestamp;
}


void displayTemp(int targetID, temp t) { //MARK: Display temperature

    switch (targetID)
    {
        case 1:
            lcd.setCursor( 0, 2);
            break;

        case 2:
            lcd.setCursor( 5, 2);
            break;

        case 3:
            lcd.setCursor(10, 2);
            break;

        case 4:
            lcd.setCursor(15, 2);
            break;
    }

    if (checkConnection(targetID)) {
        float avgTemp = (t.sens1 + t.sens2 + t.sens3 + t.sens4 + t.sens5 + t.sens6 + t.sens7 + t.sens8 + t.sens9) / 9;

        lcd.printf("%.1f", avgTemp);
    }else{
        lcd.print("  -  ");
    }
}
    

void blinkLED(int red, int green, int blue, int blinkIntervall) {
    static unsigned long previousMillis = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= blinkIntervall) {
        previousMillis = currentMillis;
        ledState = !ledState;

        if (ledState) {
            strip.setPixelColor(0, strip.Color(red, green, blue));
        } else {
            strip.setPixelColor(0, strip.Color(0, 0, 0));
        }

        strip.show();
    }
}


void updateStatusLED(int status, int blinkIntervall = 1000){ //MARK: Update status LED
    switch (status)
    {

    case 0:
        strip.setPixelColor(0, strip.Color(0, 0, 0)); // Turn off the LED
        break;

    case 1:
        strip.setPixelColor(0, strip.Color(255, 100, 0));   // Constant yellow
        break;
    
    case 2:
        blinkLED(0, 255, 0, blinkIntervall);    // Blink the LED in green
        break;
    
    case 3:
        strip.setPixelColor(0, strip.Color(0, 255, 0)); // Constant green
        break;

    case 4:
        strip.setPixelColor(0, strip.Color(255, 0, 0)); // Constant red
        break;

    case 5:
        blinkLED(255, 0, 0, blinkIntervall);    // Blink the LED in red
        break;

    case 6:
        blinkLED(255, 100, 0, blinkIntervall);  // Blink the LED in yellow
        break;
    
    default:
        break;
    }

    strip.show();
}


void displayTimeStamp() {
    lcd.setCursor(0, 0);
    lcd.print(get_timestamp());
}


void displayError(String errorMessage = "", int errorNr = 0){ //MARK: Display error
    lcd.clear();
    lcd.setCursor(0, 1);

    if (errorMessage != "" && errorNr != 0){
        lcd.printf("FATAL ERROR: Nr. %d", errorNr);
        lcd.setCursor(0, 2);
        lcd.print(errorMessage);
    }else if (errorMessage != "" && errorNr == 0){
        lcd.print("FATAL ERROR:");
        lcd.setCursor(0, 2);
        lcd.print(errorMessage);
    }else{
        lcd.print("FATAL ERROR (undef.)");
    }
}


void writeToSD(String dataString) { //MARK: Write to SD
    file = SD.open(fileName, FILE_APPEND); // Open the file in append mode
    file.print(dataString);

    if (!file){
        Serial.println("Failed to write to file");
        lcd.clear();
        lcd.home();
        lcd.print("FATAL ERROR: Nr.2");
        lcd.setCursor(0, 1);
        lcd.print("Failed to open File");
        lcd.setCursor(0, 2);
        lcd.print("Check SD Card and");
        lcd.setCursor(0, 3);
        lcd.print("Reset the Device");

        while (true)
        {
            updateStatusLED(5);
        }
    }

    while(!file){
        Serial.println("Failed to open file for writing");
        displayError("Failed to open file", 2);
        updateStatusLED(5);
        delay(1000);
    }

    file.close();
}


void getAllTemps(bool save = true) {//MARK: Get temperatures

    updateStatusLED(0);
    lcd.setCursor(0, 3);
    lcd.print("Updating Temperature");

    TXdata.actionID = 3001; //Action ID for getting all temperatures from a servent

    for (int i = 0; i < 4; i++) {
        esp_err_t result = esp_now_send(broadcastAddresses[i], (uint8_t *) &TXdata, sizeof(TXdata));

        if (waitForActionID(2001,i+1/*Target ID*/) == true){
                
            if (save == true)
            {
                if (checkConnection(i+1)) {
                    writeToSD(tempToString(receivedData,get_timestamp(), i+1));
                }else{
                    writeToSD(String(get_timestamp()) + "," + String(i+1) + ",123456789,NAN\n");
                }
            }
            displayTemp(i+1, receivedData);
        }
    }
}


void logLoop() {    //MARK: Log loop
    static unsigned long previousExecution = logIntervall;
    unsigned long currentTime = millis();

    if (timeLeft == 0) {   // Update the connection status only every second, to avoid callback issues
        previousExecution = currentTime;
        Serial.println("Retrieving Data... ");
        lcd.setCursor(0, 3);
        lcd.print("Retrieving Data...  ");
        getAllTemps();
    }else{
        lcd.setCursor(0, 3);
        lcd.print("Logging:");
        lcd.setCursor(8, 3);
        lcd.printf(" %.d s        ",timeLeft);
    }

    timeLeft = ((logIntervall)-(currentTime - (previousExecution + 1000)))/1000;
    
    // Serial.println(timeLeft);
    // Serial.println(currentTime);
    // Serial.println(previousExecution);
}


void displayConnectionStatus() { //MARK: Display connection status
    numConnections = 0;

    lcd.setCursor(0, 1);
    lcd.print("S1:");
    lcd.setCursor(5, 1);
    lcd.print("S2:");
    lcd.setCursor(10, 1);
    lcd.print("S3:");
    lcd.setCursor(15, 1);
    lcd.print("S4:");
    
    lcd.setCursor(3, 1);
    if (checkConnection(1)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(8, 1);
    if (checkConnection(2)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(13, 1);
    if (checkConnection(3)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }

    lcd.setCursor(18, 1);
    if (checkConnection(4)) {
        lcd.write(byte(0)); // Tick mark
        numConnections++;
    } else {
        lcd.print("x");
    }
}


void setup() {  //MARK: Setup
    Serial.begin(115200);

    Serial.println("\n\n\nSELF CHECK:\n");


    //------------------ BUTTON - INIT - BEGIN ------------------
    pinMode(BUTTON_PIN, INPUT);
    //------------------ BUTTON - INIT - END ------------------

    //------------------ NEOPIXEL - INIT - BEGIN ------------------
    strip.begin(); // Initialize the NeoPixel library
    strip.show();  // Initialize all pixels to 'off'
    //------------------ NEOPIXEL - INIT - END ------------------
    updateStatusLED(1);

    //------------------ LCD - INIT - BEGIN ------------------
    lcd.init();                     // initialize the lcd
    lcd.backlight();                // Turn on the backlight

    byte tickMark[8] = {            // Custom character for tick mark
        B00000,
        B00000,
        B00001,
        B00011,
        B10110,
        B11100,
        B01000,
        B00000
        };
    lcd.createChar(0, tickMark);    // Create a new custom character

    lcd.setCursor(8, 0);            // set cursor to first column, first row
    lcd.print("Boot...");        // print message
    //------------------ LCD - INIT - END ------------------

    //------------------ ESP-NNOW -INIT - BEGIN ------------------
    WiFi.mode(WIFI_STA);

    while (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Initialization:\t\t\tFailed");
        displayError("Error init ESP-NOW", 6);
        updateStatusLED(4);
        delay(3000);
    }
    Serial.println("ESP-NOW Initialization:\t\t\tSuccess");

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    for (int i = 0; i < 4; i++) {
        memcpy(peerInfo[i].peer_addr, broadcastAddresses[i], 6);
        peerInfo[i].channel = 0;  
        peerInfo[i].encrypt = false;
        
        if (esp_now_add_peer(&peerInfo[i]) != ESP_OK){
            Serial.println("ESP-NOW Peer Addition (Target " + String(i+1) + "):\tFailed");
            displayError("Failed to add peer", 5);
            updateStatusLED(4);
            return;
        }else{
            Serial.print("ESP-NOW Peer Addition (Target " + String(i+1) + "):\tSuccess\n");
        }
    }
    //------------------ ESP-NNOW -INIT - END ------------------

    //------------------ SD CARD - INIT - BEGIN ------------------
    while(!SD.begin(CS_PIN)){
        Serial.println("SD Card Mount Failed");
        displayError("SD Card Mount Failed", 4);
        updateStatusLED(5);
    }

    uint8_t cardType = SD.cardType();

    while(cardType == CARD_NONE){
        updateStatusLED(5);
        Serial.println("SD Card Mount:\t\t\t\tFailed");
        displayError("SD Card Mount Failed", 2);
    }
    Serial.println("SD Card Mount:\t\t\t\tSuccess");

    strncpy(fileName, "/data_master.csv", sizeof(fileName));
    File file = SD.open(fileName, FILE_APPEND);
    
    while(!file){
        Serial.println("Writing to file:\t\t\tFailed");
        updateStatusLED(5);
        displayError("Failed to open file", 2);
    }
    Serial.println("Writing to file:\t\t\tSuccess");

    
    file.close();

    //------------------ SD CARD - INIT - END ------------------

    //------------------ RTC - INIT - BEGIN ------------------
  if (! rtc.begin()) {
    Serial.println("Init RTC:\t\t\t\tFailed");
    updateStatusLED(4);
    while (true){}
    } else {
      Serial.print("Init RTC:\t\t\t\tSuccess (");
      Serial.print(get_timestamp());
      Serial.println(")"); 
  }
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); //uncomment to set the RTC to the compile time
    //------------------ RTC - INIT - END ------------------
    //lcd.clear();

    lcd.clear();
    lcd.setCursor(4, 0);            // set cursor to first column, first row
    lcd.print("Connecting...");     // print message

    Serial.println("\nSELF-CHECK COMPLET\n\n\n");
    
    updateStatusLED(0);
}


void loop() {

    static unsigned long previousTempUpdate = tempUpdateIntervall;
    unsigned long currentTempUpdate = millis();
    if (currentTempUpdate - previousTempUpdate >= tempUpdateIntervall && logState == false) {   // Update displayed temperature every 10 seconds
        previousTempUpdate = currentTempUpdate;
        getAllTemps(false);
    }
    
    displayTimeStamp();

    static unsigned long previousConnectStat = pingCheckIntervall;
    unsigned long currentConnectStat = millis();
    if (currentConnectStat - previousConnectStat >= pingCheckIntervall) {   // Update the connection status only every second, to avoid callback issues
        previousConnectStat = currentConnectStat;
        displayConnectionStatus();
        sendLogState(logState);
    }

    while(numConnections == 0){
        lcd.setCursor(0, 3);
        lcd.print("ERROR: No connection");
        displayConnectionStatus();
        updateStatusLED(5);
        displayTimeStamp();
        logState = false;
    }

    buttonState();

    if(logState){
        if (numConnections == 4){
            updateStatusLED(3); // Constant green
            logLoop();

        }else{
            updateStatusLED(1); // Constant yellow
            logLoop();
        }

    }else{
        if (numConnections == 4){
            updateStatusLED(2); // Blink green
        }else{
            updateStatusLED(6); // Blink yellow
        }

        lcd.setCursor(0, 3);
        lcd.print("Idle (ready to log) ");
    }
}