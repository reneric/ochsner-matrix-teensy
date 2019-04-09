#include <math.h>
#include <stdlib.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include "GIFDecoder.h"

// The filenames for the animation associated with each state
const char* files[3]={"ACTIVE.GIF", "PRES.GIF", "TRIP.GIF"};

// Animation Speed: range 1-10
// Do not modify the MIN and MAX
const int defaultSpeed = 5; 
const int minSpeed = 1;
const int maxSpeed = 10; 

// Brightness: range 0-255
// Do not modify the MIN and MAX
const int defaultBrightness = 50;             // Initial Brightness at power-up
const int minBrightness = 10;                 // Minimum brightness value for use with brightness buttons
const int maxBrightness = 200;                // Maximum brightness value for use with brightness buttons

const rgb24 COLOR_BLACK = { 0, 0, 0 };

// Presence States
#define ACTIVE_STATE 0
#define PRESENT_STATE 1
#define IDLE_STATE 3
#define TRIPPED_STATE 2

// Presence State Pins (we don't need an idle state pin)
const int activeStatePin = 13;
const int presentStatePin = 15;

const int defaultState = IDLE_STATE;          // Set the default state for the the initial run
int speed = defaultSpeed;                     // Set the default brightness for the initial run
int currentBrightness = defaultBrightness;    // Set the default brightness for the initial run

// Technically, the speed is adjusting the framerate delay. 
// So the higher the number, the slower the speed. 
// This flips that around for readability/ease of use
int currentSpeed = 11 - speed;

// Initialize the LED Matricies and tiles. 
Adafruit_NeoMatrix strip = Adafruit_NeoMatrix(8, 8, 3, 2, 17,
  NEO_TILE_TOP   + NEO_TILE_LEFT   + NEO_TILE_ROWS   + NEO_TILE_PROGRESSIVE +
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);


// Chip select for SD card on the Teensy 3.6
#define SD_CS BUILTIN_SDCARD

#define GIF_DIRECTORY "/gifs/"

int num_files;

// Function to clear the display (write all pixels to black)
void screenClearCallback(void) {
  for (int i=0; i<1024; i++)
  {
    strip.setPixelColor(i, strip.Color(0, 0, 0));
  }
}

void updateScreenCallback(void) {
  strip.show();
}

void drawPixelCallback(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
  strip.drawPixel(x, y, strip.Color(red, green, blue));
}

// Initialize brightness up/down pushbutton interrupts
void initButtons(void) {
  pinMode(5, INPUT_PULLUP);
  digitalWrite(5, HIGH);
  attachInterrupt(digitalPinToInterrupt(5), brightnessUp, FALLING);

  pinMode(4, INPUT_PULLUP);
  digitalWrite(4, HIGH);
  attachInterrupt(digitalPinToInterrupt(4), brightnessDown, FALLING);
}

void setup() {
    setScreenClearCallback(screenClearCallback);
    setUpdateScreenCallback(updateScreenCallback);
    setDrawPixelCallback(drawPixelCallback);

    Serial.begin(9600);

    // Initialize matrix
    strip.begin();
    strip.show(); // Initialize all pixels to 'off'
    strip.setBrightness(defaultBrightness);

    // Initialize brightness adjustment buttons
    initButtons();

    // initialize the SD card
    pinMode(SD_CS, OUTPUT);
    if (!SD.begin(SD_CS)) {
        Serial.println("No SD card");
        while(1);
    }

    // Determine how many animated GIF files are in the "/gifs" directory
    num_files = enumerateGIFFiles(GIF_DIRECTORY, true);

    if(num_files < 0) {
        Serial.println("No gifs directory");
        while(1);
    }

    if(!num_files) {
        Serial.println("Empty gifs directory");
        while(1);
    }

    pinMode(activeStatePin, INPUT);
    pinMode(presentStatePin, INPUT);
}

// Brightness Up Interrupt Handler
void brightnessUp() {
  int tempBrightness;

  // Debounce
  volatile static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  
  if (interrupt_time - last_interrupt_time > 200UL)   // Ignore interupts for 200 milliseconds
  {
    tempBrightness = currentBrightness;               
    
    if (tempBrightness >= maxBrightness) {            // Brightness is at maximum
      tempBrightness = maxBrightness;                 // No change
    }
    else {                                            // Brightness is below maximum
      tempBrightness += 10;                           // Increase brightness by 10
    }
    strip.setBrightness(tempBrightness);              // Send new brightness value to LED's
    currentBrightness = tempBrightness;               // Store new brightness value
    //Serial.print("Brightness = ");                  // Debug
    //Serial.println(currentBrightness);
    }

    last_interrupt_time = interrupt_time;             // Debounce
}

// Brightness Down Interrupt
void brightnessDown() {
  int tempBrightness;

  // Debounce
  volatile static unsigned long last_interrupt_time1 = 0;
  unsigned long interrupt_time1 = millis();
  
  if (interrupt_time1 - last_interrupt_time1 > 200UL)   // Ignore interupts for 200 milliseconds
  {
    tempBrightness = currentBrightness; 
    
    if (tempBrightness <= minBrightness) {              // Brightness is already at minimum
      tempBrightness = minBrightness;                   // No change
    }
    else {                                              // Brightness is above minimum
      tempBrightness -= 10;                             // Decrease brightness by 10
    }
    strip.setBrightness(tempBrightness);                // Send new brightness value to LED's
    currentBrightness = tempBrightness;                 // Store new brightness value
//    Serial.print("Brightness = ");                    // Debug
//    Serial.println(currentBrightness);
    }

    last_interrupt_time1 = interrupt_time1;             // Debounce
}

void loop() {
    // unsigned character buffer;
    char pathname[30];

    // Do forever
    while (true) {
      int index = getState();
      int state = index;
      
      if (state == IDLE_STATE) {
        return;
      }
      Serial.println(state);
      // Can clear screen for new animation here, but this causes the animation to end prematurely
      // screenClearCallback();
     
      getGIFFilename(GIF_DIRECTORY, files[index], pathname);
      Serial.println(files[index]);

      while (index == state) {
        processGIFFile(pathname, currentSpeed);
        state = getState();
      }
    }
}

int getState() {
  bool present = analogRead(presentStatePin) > 500;
  bool active = digitalRead(activeStatePin) == HIGH;
  bool trigger = active && present;
  
  if (!active && analogRead(presentStatePin) < 500) {
    return IDLE_STATE;
  }

  if (active && analogRead(presentStatePin) > 500) {
    return TRIPPED_STATE;
  }

  return active ? ACTIVE_STATE : PRESENT_STATE;
}