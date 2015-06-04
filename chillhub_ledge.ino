#include "chillhub.h"
#include "crc.h"
#include <EEPROM.h>
#include <string.h>
#include <stddef.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>

#define FIVE_MINUTE_TIMER_ID  0x70
#define MAX_UUID_LENGTH 48
#define EEPROM_SIZE 1024
#define NeoPixelPin A5

Adafruit_NeoPixel strip = Adafruit_NeoPixel(100, NeoPixelPin, NEO_GRB + NEO_KHZ800);


// Define cloud IDs for remote communication
enum E_CloudIDs {
  rgbRecordingActual = 0x96,
  rgbRecording = 0x97,
  rgbActual = 0x98,
  rgb = 0x99,
  LastID
};

// Define the EEPROM data structure.
typedef struct Store {
  char UUID[MAX_UUID_LENGTH+1];
  unsigned int crc; 
} Store;

// A union to make reading and writing the EEPROM convenient.
typedef union Eeprom {
    Store store;
    unsigned char bytes[sizeof(Store)];
} Eeprom;

// The RAM copy of the EEPROM.
Eeprom eeprom;

// A default UUID to use if none has been assigned.
// Each device needs it's own UUID.
const char defaultUUID[] = "41e1b18e-2d12-4306-9211-c1068bf7f76d";

// *playlist* items
const uint8_t MAX_RECORDING_SIZE = 50;
const uint16_t MS_PAUSE_BETWEEN_COLORS = 20;
bool recording = false;
uint8_t recordingIndex = 0;
uint8_t playlistIndex = 0;
uint32_t rgbPlaylist[MAX_RECORDING_SIZE];
uint32_t playlistLastMillis = 0;

// register the name (type) of this device with the chillhub
// syntax is ChillHub.setup(device type, UUID);
chInterface ChillHub;

SoftwareSerial debugSerial(5,7);

//
// Function prototypes
//
static void saveEeprom(void);
static void initializeEeprom(void);
void keepaliveCallback(uint8_t dummy);
void setDeviceUUID(char *pUUID);

static void setLedRGB(uint32_t rgbValue)
{
  if (recording==true)
  {
    debugSerial.println("setLedRGB:true");
    recordPoint(rgbValue);
  }
  else
  {
    debugSerial.println("setLedRGB:false");
    startRecording();
    recordPoint(rgbValue);
    stopRecording();
    ChillHub.updateCloudResourceU32(rgbActual, rgbValue);
  }
}

static void setRecording(uint32_t recording)
{
  debugSerial.println("setRecording");

  if (recording == 1)
  {
    startRecording();
  }
  else 
  {
    stopRecording();
  }
  ChillHub.updateCloudResourceU32(rgbRecordingActual, recording);
}

static void startRecording()
{
  debugSerial.println("startRecording");
  recording = true;
  recordingIndex = 0;
  playlistIndex = 0;
  memset(rgbPlaylist,0,sizeof(rgbPlaylist));
}

static void playlistTick()
{
  if(recording == false)
  {
    if (millis() - playlistLastMillis > MS_PAUSE_BETWEEN_COLORS || millis() < MS_PAUSE_BETWEEN_COLORS)
    {
      playlistLastMillis = millis();
      if (playlistIndex < recordingIndex)
      {
        uint32_t rgbValue = rgbPlaylist[playlistIndex++];
        uint8_t pixelR = (((rgbValue) & 0xFF0000)>>16);
        uint8_t pixelG = (((rgbValue) & 0xFF00)>>8);
        uint8_t pixelB = (((rgbValue) & 0xFF));
        colorWipe(strip.Color(pixelR, pixelG, pixelB), 5);
      }
      else
      {
        playlistIndex = 0;
      }
    }
  }
}

static void stopRecording()
{
  debugSerial.println("stopRecording");
  recording = false;
}

static void recordPoint(uint32_t rgbValue)
{
  debugSerial.println("recordPoint");
  if (recordingIndex < MAX_RECORDING_SIZE)
  {
    rgbPlaylist[recordingIndex++] = rgbValue;
  }
}
  

// This function gets called when you plug the Arduino into the chillhub.
// It registers the Arduino with the chill hub.
// It registers resources in the cloud.
// It registers listeners for Arduino resources which can be controlled
// via the cloud.
void deviceAnnounce() { 
  // Each device has a "type name" and a UUID.
  // A type name could be something like "toaster" or "light bulb"
  // Each device must has a unique version 4 UUID.  See
  // http://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_.28random.29
  // for details.
  ChillHub.setup("ledges", eeprom.store.UUID);
  
  // add a listener for device ID request type
  // Device ID is a request from the chill hub for the Arduino to register itself.
  ChillHub.subscribe(deviceIdRequestType, (chillhubCallbackFunction)deviceAnnounce);

  // add a listener for keepalive from chillhub
  // The chillhub sends this periodcally.
  ChillHub.subscribe(keepAliveType, (chillhubCallbackFunction)keepaliveCallback);

  // add a listener for setting the UUID of the device
  // The UUID is set via the USB port and the set-device-uuid.js script as part of
  // chillhub-firmware.
  // No cloud listener is required for this.
  ChillHub.subscribe(setDeviceUUIDType, (chillhubCallbackFunction)setDeviceUUID);

  //this is the incoming value that the user requested
  ChillHub.addCloudListener(rgb, (chillhubCallbackFunction)setLedRGB);
  ChillHub.addCloudListener(rgbRecording, (chillhubCallbackFunction)setRecording);
  
  ChillHub.createCloudResourceU32("rgb", rgb, 1, 0);
  ChillHub.createCloudResourceU32("rgbActual", rgbActual, 0, 0);
  ChillHub.createCloudResourceU32("rgbRecording", rgbRecording, 1, 0);
  ChillHub.createCloudResourceU32("rgbRecordingActual", rgbRecordingActual, 0, 0);
  colorWipe(strip.Color(0, 0, 10), 5); 
}

// This is the regular Arduino setup function.
void setup() {
  // Start serial port for communications with the chill hub
  Serial.begin(115200);
  debugSerial.begin(4800);
  delay(200);
  
  initializeEeprom();
  
  ChillHub.setup("ledges", eeprom.store.UUID);
  ChillHub.subscribe(deviceIdRequestType, (chillhubCallbackFunction)deviceAnnounce);
  
  // Attempt to initialize with the chill hub
  //deviceAnnounce();
  
  strip.begin();
  strip.show();

  colorWipe(strip.Color(10, 0, 00), 5);
  
  colorWipeFront(strip.Color(255, 0, 00), 5);
  
  colorWipeBack(strip.Color(0, 0,255), 5);
  colorWipeMiddle(strip.Color(0, 0, 00), 5);
  
  debugSerial.println("debug serial initialized");
  
}

// This is the normal Arduino run loop.
void loop() {
  ChillHub.loop();
  playlistTick();
}

// Save the data to EEPROM.
static void saveEeprom(void) {
  int i;
  uint16_t crc = crc_init();
  unsigned char b;

  // Write the data, calculating the CRC as we go.
  for(i=0; i<offsetof(Store, crc); i++) {
    b = eeprom.bytes[i];
    EEPROM.write(i, b);
    crc = crc_update(crc, &b, 1);
  }

  // Finish calculating the CRC.
  crc = crc_finalize(crc);

  // Save the CRC to the RAM copy
  eeprom.store.crc = crc;
  
  // Write the CRC to the EEPROM.
  EEPROM.write(offsetof(Store, crc), eeprom.bytes[offsetof(Store, crc)]);
  EEPROM.write(offsetof(Store, crc)+1, eeprom.bytes[offsetof(Store, crc)+1]);
}

// Initialize the EEPROM RAM copy
// This will also initialize the EEPROM if it has not been initialized previously.
static void initializeEeprom(void) {
  int i;
  uint16_t crc = crc_init();
  unsigned char b;
  
  // Read the data from the EEPROM, calculate the CRC as we go.
  for(i=0; i<offsetof(Store, crc); i++) {
    b = EEPROM.read(i);
    eeprom.bytes[i] = b;
    crc = crc_update(crc, &b, 1);
  }
  
  // Get the stored CRC.
  eeprom.bytes[offsetof(Store, crc)] = EEPROM.read(offsetof(Store, crc));
  eeprom.bytes[offsetof(Store, crc)+1] = EEPROM.read(offsetof(Store, crc)+1);
  
  // Finish calculating the CRC.
  crc = crc_finalize(crc);
  
  // Compare the stored CRC with the calculated CRC.
  // If they are not equal, initialize the internal EEPROM.
  if (crc != eeprom.store.crc) {
    memcpy(eeprom.store.UUID, defaultUUID, sizeof(defaultUUID));
    saveEeprom();
  }
}

// This handles the keep alive message from the chillhub.
// This chillhub will send this message periodically.
void keepaliveCallback(uint8_t dummy) {
  (void)dummy;
}

void setDeviceUUID(char *pUUID) {
  uint8_t len = (uint8_t)pUUID[0];
  char *pStr = &pUUID[1];
  
  if (len <= MAX_UUID_LENGTH) {
    // add null terminator
    pStr[len] = 0;
    memcpy(eeprom.store.UUID, pStr, len+1);
    saveEeprom();
  } 
}


void colorWipeBack(uint32_t c, uint8_t wait)
{
  for(uint16_t i=26; i<strip.numPixels(); i++) {
    if (i%2==0)
    {  
      strip.setPixelColor(i, c);
        strip.show();
        delayMicroseconds(wait);
    }
  }
}

void colorWipeFront(uint32_t c, uint8_t wait)
{
  for(uint16_t i=0; i<20; i++) {
    if (i%2==0)
    {  
      strip.setPixelColor(i, c);
        strip.show();
        delayMicroseconds(wait);
    }
  }
  
}

void colorWipeMiddle(uint32_t c, uint8_t wait) 
{
  for(uint16_t i=20; i<45; i++) {
    //if (i%2==0)
    {  
      strip.setPixelColor(i, c);
        strip.show();
        delayMicroseconds(wait);
    }
  }
}

void colorWipe(uint32_t c, uint8_t wait) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    if (i%2==0)
    {  
      strip.setPixelColor(i, c);
        strip.show();
        delayMicroseconds(wait);
    }
  }
}


