#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> 
#include <Bluepad32.h>


#define RXD2 16  
#define TXD2 17  

// Packet size: 1 + 2 + 1 + 1 + 2 = 7 Bytes
// Total Packet with Header: 2 (Header) + 7 (Data) = 9 Bytes
typedef struct __attribute__((packed)) {
    uint8_t health;
    uint16_t bullets;
    uint8_t shells;
    uint8_t rockets;
    uint16_t cells;
} GameDataPacket;

GameDataPacket gameData;
bool newDataAvailable = false; 

bool controllerIsConnected = false; 

#define PIN_FIRE    18
#define PIN_USE     19
#define PIN_ESCAPE  21
#define PIN_ENTER   22
#define PIN_RIGHT   23
#define PIN_DOWN    25
#define PIN_LEFT    32 
#define PIN_UP      33

#define SCREEN_ADDRESS 0x3C
#define OLED_SDA 27 
#define OLED_SCLK 26 
Adafruit_SH1106G display(128, 64, &Wire, -1);

unsigned long lastDrawTime = 0;
const int drawInterval = 50; // 20 FPS


void screenSetup() {
  Wire.begin(OLED_SDA, OLED_SCLK);
  if (!display.begin(SCREEN_ADDRESS)) {
     return; 
  }
  display.clearDisplay();
  display.display();
}

// State A: Looking for Bluetooth Controller
void drawConnectingScreen() {
    static int dots = 0;
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    
    display.setCursor(10, 20);
    display.println("SEARCHING FOR");
    display.setCursor(25, 30);
    display.println("CONTROLLER");
    
    display.setCursor(10, 50);
    display.print("Connecting");
    
    for(int i=0; i<dots; i++) display.print(".");
    
    display.display();

    dots++;
    if(dots > 3) dots = 0;
}

// State B: Controller Connected, Waiting for UART Data
void drawWaitingForData() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    display.setCursor(0, 10);
    display.println("Controller: LINKED");
    
    display.setCursor(0, 35);
    display.println("Waiting for");
    display.setCursor(0, 45);
    display.println("Doom Data...");
    
    display.display();
}

// State C: Gaming Mode
void drawDoomHud() {
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("HP:"); 
  display.print(gameData.health); 
  display.print("%");

  display.setTextSize(1);
  display.setCursor(0, 25);
  display.print("BUL: "); display.print(gameData.bullets);
  display.setCursor(64, 25);
  display.print("SHL: "); display.print(gameData.shells);

  display.setCursor(0, 45);
  display.print("RKT: "); display.print(gameData.rockets);
  display.setCursor(64, 45);
  display.print("CEL: "); display.print(gameData.cells);

  display.display();
}


void controlGPIO(ControllerPtr ctl) {
    uint8_t dpad = ctl->dpad();
    
    digitalWrite(PIN_UP,    (dpad & DPAD_UP)    ? LOW : HIGH);
    digitalWrite(PIN_DOWN,  (dpad & DPAD_DOWN)  ? LOW : HIGH);
    digitalWrite(PIN_LEFT,  (dpad & DPAD_LEFT)  ? LOW : HIGH);
    digitalWrite(PIN_RIGHT, (dpad & DPAD_RIGHT) ? LOW : HIGH);

    digitalWrite(PIN_FIRE,   ctl->a()           ? LOW : HIGH);
    digitalWrite(PIN_USE,    ctl->b()           ? LOW : HIGH);
    digitalWrite(PIN_ENTER,  ctl->miscStart()   ? LOW : HIGH);
    digitalWrite(PIN_ESCAPE, ctl->miscSelect()  ? LOW : HIGH);
}


ControllerPtr myControllers[BP32_MAX_GAMEPADS];

void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            Serial.printf("CALLBACK: Controller connected\n");
            myControllers[i] = ctl;
            controllerIsConnected = true; 
            break;
        }
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            Serial.printf("CALLBACK: Controller disconnected\n");
            myControllers[i] = nullptr;
            controllerIsConnected = false; 
            break;
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

    screenSetup();

    pinMode(PIN_FIRE, OUTPUT); digitalWrite(PIN_FIRE, HIGH);
    pinMode(PIN_USE, OUTPUT); digitalWrite(PIN_USE, HIGH);
    pinMode(PIN_ESCAPE, OUTPUT); digitalWrite(PIN_ESCAPE, HIGH);
    pinMode(PIN_ENTER, OUTPUT); digitalWrite(PIN_ENTER, HIGH);
    pinMode(PIN_RIGHT, OUTPUT); digitalWrite(PIN_RIGHT, HIGH);
    pinMode(PIN_DOWN, OUTPUT); digitalWrite(PIN_DOWN, HIGH);
    pinMode(PIN_LEFT, OUTPUT); digitalWrite(PIN_LEFT, HIGH);
    pinMode(PIN_UP, OUTPUT); digitalWrite(PIN_UP, HIGH);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.forgetBluetoothKeys();
    BP32.enableVirtualDevice(false);
}

void loop() {
    bool dataUpdated = BP32.update();
    if (dataUpdated) {
        for (auto myController : myControllers) {
            if (myController && myController->isConnected() && myController->hasData()) {
                controlGPIO(myController);
            }
        }
    }

  
    while (Serial2.available() > 0) {
        
        if (Serial2.peek() != 0xAA) {
            Serial2.read(); //  garbage data check next byte
            continue;       
        }

        //found 0xAA
        // Header (2 bytes) + Struct (7 bytes) = 9 bytes total
        if (Serial2.available() < (2 + sizeof(GameDataPacket))) {
            break; // not enough data yet, wait for next loop
        }

        Serial2.read(); 
        
        if (Serial2.read() == 0x55) {
            // 0xAA and 0x55 found, next bytes are real data
            Serial2.readBytes((char*)&gameData, sizeof(GameDataPacket));
            newDataAvailable = true;
        }
    }

    if (millis() - lastDrawTime > drawInterval) {
        
        if (!controllerIsConnected) {
            drawConnectingScreen();
        } 
        else if (newDataAvailable) {
            drawDoomHud();
            newDataAvailable = false;
        }
        else {
            drawWaitingForData();
        }
        
        lastDrawTime = millis();
    }
}