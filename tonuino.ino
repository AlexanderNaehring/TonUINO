// uncomment ONE OF THE BELOW TWO LINES to enable status led support
// the first enables support for a vanilla led
// the second enables support for ws281x led(s)
// #define STATUSLED
// #define STATUSLEDRGB

// uncomment the below line to enable low voltage shutdown support
// #define LOWVOLTAGE

// uncomment the below line to flip the shutdown pin logic
// #define POLOLUSWITCH

// include required libraries
#include <Arduino.h>
#include <ESP.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <EEPROM.h>
#include <MFRC522.h>
#include <DFMiniMp3.h>
#include <AceButton.h>
using namespace ace_button;

// include additional library if ws281x status led support is enabled
#if defined STATUSLED ^ defined STATUSLEDRGB
#if defined STATUSLEDRGB
#include <WS2812.h>
#endif
#endif

// include additional library if low voltage shutdown support is enabled
#if defined LOWVOLTAGE
#include <Vcc.h>
#endif

// playback modes
enum {NOMODE, STORY, ALBUM, PARTY, SINGLE, STORYBOOK, VSTORY, VALBUM, VPARTY};

// button actions
enum {NOP,
      B0P, B1P, B2P, B3P, B4P,
      B0H, B1H, B2H, B3H, B4H,
      B0D, B1D, B2D, B3D, B4D,
      IRU, IRD, IRL, IRR, IRC, IRM, IRP
     };

// button modes
enum {INIT, PLAY, PAUSE, PIN, CONFIG};

// shutdown timer actions
enum {START, STOP, CHECK, SHUTDOWN};

// preference actions
enum {READ, WRITE, MIGRATE, RESET, RESET_PROGRESS};

// status led actions
enum {OFF, SOLID, PULSE, BLINK, BURST2, BURST4, BURST8};

// define general configuration constants
const uint8_t mp3SerialRxPin = D2;                  // mp3 serial rx, wired to tx pin of DFPlayer Mini
const uint8_t mp3SerialTxPin = D1;                  // mp3 serial tx, wired to rx pin of DFPlayer Mini
const uint8_t mp3BusyPin = D0;                      // reports play state of DFPlayer Mini (LOW = playing)
#if defined STATUSLED ^ defined STATUSLEDRGB
const uint8_t statusLedPin = 6;                     // pin used for vanilla status led or ws281x status led(s)
const uint8_t statusLedCount = 1;                   // number of ws281x status led(s)
const uint8_t statusLedMaxBrightness = 20;          // max brightness of ws281x status led(s) (in percent)
#endif
// const uint8_t shutdownPin = ;                    // pin used to shutdown the system
const uint8_t nfcResetPin = D3;                     // used for spi communication to nfc module
const uint8_t nfcSlaveSelectPin = D8;               // used for spi communication to nfc module
const uint8_t button0Pin = D4;                      // middle button (Play/Pause)
const uint8_t button1Pin = 1;                       // right button
const uint8_t button2Pin = 3;                       // left button 3 = RX
// Attention: on nodeMCU, not many GPIO are available
// when using TX/RX for btn input, pins are pulled high and cannot be used for serial debug!
// to prevent problems, do not use GPIO 1 (TX) for button input.

const uint16_t buttonClickDelay = 400;             // time during which a button press is still a click (in milliseconds)
const uint16_t buttonShortLongPressDelay = 500;    // time after which a button press is considered a long press (in milliseconds)
const uint16_t buttonLongLongPressDelay = 5000;     // longer long press delay for special cases, i.e. to trigger the parents menu (in milliseconds)
const uint32_t debugConsoleSpeed = 115200;          // speed for the debug console

// define magic cookie (by default 0x13 0x37 0xb3 0x47)
const uint8_t magicCookieHex[4] = {0x13, 0x37, 0xb3, 0x47};


// default values for preferences
const uint8_t preferenceVersion = 1;
const uint8_t mp3StartVolumeDefault = 5;
const uint8_t mp3MaxVolumeDefault = 15;
const uint8_t mp3MenuVolumeDefault = 10;
const uint8_t mp3EqualizerDefault = 1;
const uint8_t shutdownMinutesDefault = 10;

#if defined LOWVOLTAGE
// define constants for shutdown feature
const float shutdownMinVoltage = 4.4;                        // minimum expected voltage level (in volts)
const float shutdownWarnVoltage = 4.8;                       // warning voltage level (in volts)
const float shutdownMaxVoltage = 5.0;                        // maximum expected voltage level (in volts)
const float shutdownVoltageCorrection = 1.0 / 1.0;           // voltage measured by multimeter divided by reported voltage
#endif

// define strings
const char *playbackModeName[] = {" ", "story", "album", "party", "single", "storybook", "vstory", "valbum", "vparty"};
const char *nfcStatusMessage[] = {" ", "read", "write", "ok", "failed"};
const char *mp3EqualizerName[] = {" ", "normal", "pop", "rock", "jazz", "classic", "bass"};

// this struct stores nfc tag data
struct nfcTagStruct {
  uint32_t cookie = 0;
  uint8_t version = 0;
  uint8_t folder = 0;
  uint8_t mode = 0;
  uint8_t multiPurposeData1 = 0;
  uint8_t multiPurposeData2 = 0;
};

// this struct stores playback state
struct playbackStruct {
  bool isLocked = false;
  bool isPlaying = false;
  bool isFresh = true;
  bool isRepeat = false;
  bool playListMode = false;
  uint8_t mp3CurrentVolume = 0;
  uint8_t folderStartTrack = 0;
  uint8_t folderEndTrack = 0;
  uint8_t playList[255] = {};
  uint8_t playListItem = 0;
  uint8_t playListItemCount = 0;
  nfcTagStruct currentTag;
};

// this struct stores preferences
struct preferenceStruct {
  uint32_t cookie = 0;
  uint8_t version = 0;
  uint8_t mp3StartVolume = 0;
  uint8_t mp3MaxVolume = 0;
  uint8_t mp3MenuVolume = 0;
  uint8_t mp3Equalizer = 0;
  uint8_t shutdownMinutes = 0;
};

// global variables
uint8_t inputEvent = NOP;
uint32_t magicCookie = 0;
uint32_t preferenceCookie = 0;
playbackStruct playback;
preferenceStruct preference;

// ################################################################################################################################################################
// ############################################################### no configuration below this line ###############################################################
// ################################################################################################################################################################

// declare functions
void checkForInput();
void translateButtonInput(AceButton *button, uint8_t eventType, uint8_t /* buttonState */);
void switchButtonConfiguration(uint8_t buttonMode);
void waitPlaybackToFinish(uint8_t red, uint8_t green, uint8_t blue, uint16_t statusLedUpdateInterval);
void printModeFolderTrack(bool cr);
void playNextTrack(uint16_t globalTrack, bool directionForward, bool triggeredManually);
uint8_t readNfcTagData();
uint8_t writeNfcTagData(uint8_t nfcTagWriteBuffer[], uint8_t nfcTagWriteBufferSize);
void printNfcTagData(uint8_t dataBuffer[], uint8_t dataBufferSize, bool cr);
void printNfcTagType(MFRC522::PICC_Type nfcTagType);
void shutdownTimer(uint8_t timerAction);
void preferences(uint8_t preferenceAction);
uint8_t prompt(uint8_t promptOptions, uint16_t promptHeading, uint16_t promptOffset, uint8_t promptCurrent, uint8_t promptFolder, bool promptPreview, bool promptChangeVolume);
void parentsMenu();
#if defined STATUSLED ^ defined STATUSLEDRGB
void statusLedUpdate(uint8_t statusLedAction, uint8_t red, uint8_t green, uint8_t blue, uint16_t statusLedUpdateInterval);
void statusLedUpdateHal(uint8_t red, uint8_t green, uint8_t blue, int16_t brightness);
#endif

// used by DFPlayer Mini library during callbacks
class Mp3Notify {
  public:
    static void OnError(uint16_t returnValue) {
      Serial.print(F("DFPlayer Error:"));
      switch (returnValue) {
        case DfMp3_Error_Busy: {
            Serial.print(F("busy"));
            break;
          }
        case DfMp3_Error_Sleeping: {
            Serial.print(F("sleep"));
            break;
          }
        case DfMp3_Error_SerialWrongStack: {
            Serial.print(F("serial stack"));
            break;
          }
        case DfMp3_Error_CheckSumNotMatch: {
            Serial.print(F("checksum"));
            break;
          }
        case DfMp3_Error_FileIndexOut: {
            Serial.print(F("file index"));
            break;
          }
        case DfMp3_Error_FileMismatch: {
            Serial.print(F("file mismatch"));
            break;
          }
        case DfMp3_Error_Advertise: {
            Serial.print(F("advertise"));
            break;
          }
        case DfMp3_Error_RxTimeout: {
            Serial.print(F("rx timeout"));
            break;
          }
        case DfMp3_Error_PacketSize: {
            Serial.print(F("packet size"));
            break;
          }
        case DfMp3_Error_PacketHeader: {
            Serial.print(F("packet header"));
            break;
          }
        case DfMp3_Error_PacketChecksum: {
            Serial.print(F("packet checksum"));
            break;
          }
        case DfMp3_Error_General: {
            Serial.print(F("general"));
            break;
          }
        default: {
            Serial.print(F("unknown"));
            break;
          }
      }
      Serial.println(F(" error"));
    }
    static void PrintlnSourceAction(DfMp3_PlaySources source, const char* action) {
      if (source & DfMp3_PlaySources_Sd) Serial.print("sd ");
      if (source & DfMp3_PlaySources_Usb) Serial.print("usb ");
      if (source & DfMp3_PlaySources_Flash) Serial.print("flash ");
      Serial.println(action);
    }
    static void OnPlayFinished(DfMp3_PlaySources source, uint16_t returnValue) {
      playNextTrack(returnValue, true, false);
    }
    static void OnPlaySourceOnline(DfMp3_PlaySources source) {
      PrintlnSourceAction(source, "online");
    }
    static void OnPlaySourceInserted(DfMp3_PlaySources source) {
      PrintlnSourceAction(source, "in");
    }
    static void OnPlaySourceRemoved(DfMp3_PlaySources source) {
      PrintlnSourceAction(source, "out");
    }
};

SoftwareSerial mp3Serial(mp3SerialRxPin, mp3SerialTxPin);                     // create DFPlayer SoftwareSerial
MFRC522 mfrc522(nfcSlaveSelectPin, nfcResetPin);                              // create MFRC522 instance
DFMiniMp3<SoftwareSerial, Mp3Notify> mp3(mp3Serial);                          // create DFMiniMp3 instance
ButtonConfig button0Config;                                                   // create ButtonConfig instance
ButtonConfig button1Config;                                                   // create ButtonConfig instance
ButtonConfig button2Config;                                                   // create ButtonConfig instance
AceButton button0(&button0Config);                                            // create AceButton instance
AceButton button1(&button1Config);                                            // create AceButton instance
AceButton button2(&button2Config);                                            // create AceButton instance

#if defined STATUSLED ^ defined STATUSLEDRGB
#if defined STATUSLEDRGB
WS2812 rgbLed(statusLedCount);                                                // create WS2812 instance
#endif
#endif

#if defined LOWVOLTAGE
Vcc shutdownVoltage(shutdownVoltageCorrection);                               // create Vcc instance
#endif

void setup() {
  // things we need to do immediately on startup
//  pinMode(shutdownPin, OUTPUT);
#if defined POLOLUSWITCH
//  digitalWrite(shutdownPin, LOW);
#else
//  digitalWrite(shutdownPin, HIGH);
#endif
  
  EEPROM.begin(sizeof(preferenceStruct));
  
  magicCookie = (uint32_t)magicCookieHex[0] << 24;
  magicCookie += (uint32_t)magicCookieHex[1] << 16;
  magicCookie += (uint32_t)magicCookieHex[2] << 8;
  magicCookie += (uint32_t)magicCookieHex[3];
  preferenceCookie = (uint32_t)magicCookieHex[2] << 24;
  preferenceCookie += (uint32_t)magicCookieHex[3] << 16;
  preferenceCookie += (uint32_t)magicCookieHex[0] << 8;
  preferenceCookie += (uint32_t)magicCookieHex[1];

  // start normal operation
  Serial.begin(debugConsoleSpeed);
  Serial.println(F("\n\nTonUINO JUKEBOX"));
  Serial.println(F("by Thorsten Voß"));
  Serial.println(F("Stephan Eisfeld"));
  Serial.println(F("and many others"));
  Serial.println(F("---------------"));
  Serial.println(F("flashed"));
  Serial.print(F("  "));
  Serial.println(__DATE__);
  Serial.print(F("  "));
  Serial.println(__TIME__);

  preferences(READ);

  Serial.println(F("init RFID"));
  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();

  Serial.println(F("init mp3"));
  pinMode(mp3BusyPin, INPUT);
  /*Serial.print(F("Wait for busy pin to get low."));
  while(!digitalRead(mp3BusyPin)) {
    delay(10);
    Serial.print(F("."));
  }*/
  delay(1000);
  Serial.println("");
  Serial.println(F("mp3.begin"));
  mp3.begin();
  Serial.println(F("mp3.reset"));
  mp3.reset();  // todo check how to improve compatibility across different implementations of the DFPlayer chip (bad china copies have serial com problems when booted wrongly) 
  delay(2000);

  Serial.println(F("MP3 configuration:"));
  Serial.print(F("    start volume: "));
  Serial.println(preference.mp3StartVolume);
  mp3.setVolume(playback.mp3CurrentVolume = preference.mp3StartVolume);
  Serial.print(F("      max volume: "));
  Serial.println(preference.mp3MaxVolume);
  Serial.print(F("     menu volume: "));
  Serial.println(preference.mp3MenuVolume);
  // Serial.print(F("  equalizer mode: "));
  // Serial.println(mp3EqualizerName[preference.mp3Equalizer]);
  // mp3.setEq((DfMp3_Eq)(preference.mp3Equalizer - 1));

  Serial.print(F("init buttons"));
  pinMode(button0Pin, INPUT_PULLUP);
  pinMode(button1Pin, INPUT_PULLUP);
  pinMode(button2Pin, INPUT_PULLUP);
  button0.init(button0Pin, HIGH, 0);
  button1.init(button1Pin, HIGH, 1);
  button2.init(button2Pin, HIGH, 2);
  Serial.println(F(" 3 buttons"));
  switchButtonConfiguration(INIT);

  Serial.print(F("init shutdown: "));
  Serial.print(preference.shutdownMinutes);
  Serial.println(F("m timer"));
  shutdownTimer(START);


#if defined STATUSLED ^ defined STATUSLEDRGB
#if defined STATUSLED
  Serial.println(F("init LED"));
  pinMode(statusLedPin, OUTPUT);
#endif
#if defined STATUSLEDRGB
  Serial.println(F("init ws281x"));
  rgbLed.setOutput(statusLedPin);
  rgbLed.setColorOrderRGB();
  //rgbLed.setColorOrderBRG();
  //rgbLed.setColorOrderGRB();
#endif
  statusLedUpdate(SOLID, 0, 0, 0, 0);
#endif

#if defined LOWVOLTAGE
  Serial.println(F("init low voltage mode:"));
  Serial.print(F("  ex-"));
  Serial.print(shutdownMaxVoltage);
  Serial.print(F("V"));
  Serial.print(F(" wa-"));
  Serial.print(shutdownWarnVoltage);
  Serial.print(F("V"));
  Serial.print(F(" sh-"));
  Serial.print(shutdownMinVoltage);
  Serial.print(F("V"));
  Serial.print(F(" cu-"));
  Serial.print(shutdownVoltage.Read_Volts());
  Serial.print(F("V ("));
  Serial.print(shutdownVoltage.Read_Perc(shutdownMinVoltage, shutdownMaxVoltage));
  Serial.println(F("%)"));
#endif

  // hold down all three buttons while powering up: erase the eeprom contents
  if (button0.isPressedRaw() && button1.isPressedRaw() && button2.isPressedRaw()) {
      Serial.println(F("init eeprom"));
      for (uint16_t i = 0; i < EEPROM.length(); i++) {
        EEPROM.write(i, 0);
      }
      preferences(RESET);
      mp3.setVolume(playback.mp3CurrentVolume = preference.mp3StartVolume);
      mp3.setEq((DfMp3_Eq)(preference.mp3Equalizer - 1));
      shutdownTimer(START);
      mp3.playMp3FolderTrack(809);
      waitPlaybackToFinish(0, 255, 0, 100);
  }

  switchButtonConfiguration(PAUSE);
  mp3.playMp3FolderTrack(800);  // Los gehts!
  Serial.println(F("ready"));
}

void loop() {
  playback.isPlaying = !digitalRead(mp3BusyPin);
  checkForInput();
  shutdownTimer(CHECK);

#if defined LOWVOLTAGE
  // if low voltage level is reached, store progress and shutdown
  if (shutdownVoltage.Read_Volts() <= shutdownMinVoltage) {
    if (playback.currentTag.mode == STORYBOOK) EEPROM.write(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
    mp3.playMp3FolderTrack(808);  // Batterie leer
    waitPlaybackToFinish(255, 0, 0, 100);
    shutdownTimer(SHUTDOWN);
  }
  else if (shutdownVoltage.Read_Volts() <= shutdownWarnVoltage) {
#if defined STATUSLED ^ defined STATUSLEDRGB
    statusLedUpdate(BLINK, 255, 0, 0, 100);
#endif
  }
  else {
#if defined STATUSLED ^ defined STATUSLEDRGB
    if (playback.isPlaying) statusLedUpdate(SOLID, 0, 255, 0, 100);
    else statusLedUpdate(PULSE, 0, 255, 0, 100);
#endif
  }
#else
#if defined STATUSLED ^ defined STATUSLEDRGB
  if (playback.isPlaying) statusLedUpdate(SOLID, 0, 255, 0, 100);
  else statusLedUpdate(PULSE, 0, 255, 0, 100);
#endif
#endif

  // ################################################################################
  // # main code block, if nfc tag is detected and TonUINO is not locked do something
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial() && !playback.isLocked) {
    Serial.println("New RFID card detected");
    // if the current playback mode is story book mode, only while playing: store the current progress
    if (playback.currentTag.mode == STORYBOOK && playback.isPlaying) {
      Serial.print(F("save "));
      printModeFolderTrack(true);
      EEPROM.write(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
    }
    // readNfcTagData() populates the playback.currentTag structure if read is successful
    uint8_t readNfcTagStatus = readNfcTagData();
    // ##############################
    // # nfc tag is successfully read
    Serial.println("Card successfully read");
    if (readNfcTagStatus == 1) {
      // #############################################################################
      // # nfc tag has our magic cookie on it, use data from nfc tag to start playback
      if (playback.currentTag.cookie == magicCookie) {
        Serial.println("Magic cookie found");
        switchButtonConfiguration(PLAY);
        shutdownTimer(STOP);

        randomSeed(micros());

        // prepare boundaries for playback
        switch (playback.currentTag.mode) {
          case STORY:
          case ALBUM:
          case PARTY:
          case SINGLE:
          case STORYBOOK: {
              playback.folderStartTrack = 1;
              playback.folderEndTrack = mp3.getFolderTrackCount(playback.currentTag.folder);
              break;
            }
          case VSTORY:
          case VALBUM:
          case VPARTY: {
              playback.folderStartTrack = playback.currentTag.multiPurposeData1;
              playback.folderEndTrack = playback.currentTag.multiPurposeData2;
              break;
            }
        }

        // prepare playlist for playback
        for (uint8_t i = 0; i < 255; i++) playback.playList[i] = playback.folderStartTrack + i <= playback.folderEndTrack ? playback.folderStartTrack + i : 0;
        playback.playListItemCount = playback.folderEndTrack - playback.folderStartTrack + 1;

        // prepare first track for playback
        switch (playback.currentTag.mode) {
          case VSTORY: {}
          case STORY: {
              playback.playListItem = random(1, playback.playListItemCount + 1);
              break;
            }
          case VALBUM: {}
          case ALBUM: {
              playback.playListItem = 1;
              break;
            }
          case VPARTY: {}
          case PARTY: {
              playback.playListItem = 1;
              // shuffle playlist
              for (uint8_t i = 0; i < playback.playListItemCount; i++) {
                uint8_t j = random(0, playback.playListItemCount);
                uint8_t temp = playback.playList[i];
                playback.playList[i] = playback.playList[j];
                playback.playList[j] = temp;
              }
              break;
            }
          case SINGLE: {
              playback.playListItem = playback.currentTag.multiPurposeData1;
              break;
            }
          case STORYBOOK: {
              uint8_t storedTrack = EEPROM.read(playback.currentTag.folder);
              // don't resume from eeprom, play from the beginning
              if (storedTrack == 0 || storedTrack > playback.folderEndTrack) playback.playListItem = 1;
              // resume from eeprom
              else {
                playback.playListItem = storedTrack;
                Serial.print(F("resume "));
              }
              break;
            }
          default: {
              break;
            }
        }

        playback.isFresh = true;
        playback.isRepeat = false;
        playback.playListMode = true;
        printModeFolderTrack(true);
        mp3.playFolderTrack(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
      }
      // # end - nfc tag has our magic cookie on it
      // ##########################################

      // #####################################################################################
      // # nfc tag does not have our magic cookie on it, start setup to configure this nfc tag
      else if (playback.currentTag.cookie == 0) {
        Serial.println("No magic cookie, setup new RFID card...");
        nfcTagStruct newTag;
        playback.playListMode = false;

        // set volume to menu volume
        mp3.setVolume(preference.mp3MenuVolume);

        switchButtonConfiguration(CONFIG);
        shutdownTimer(STOP);

        while (true) {
          Serial.println(F("setup tag"));
          Serial.println(F("folder"));
          newTag.folder = prompt(99, 801, 0, 0, 0, true, false);  // 801 = Neue Karte erkannt
          if (newTag.folder == 0) {
            mp3.playMp3FolderTrack(807);  // Karte konfigurieren abgebrochen.
            waitPlaybackToFinish(255, 0, 0, 100);
            break;
          }
          Serial.println(F("mode"));
          newTag.mode = prompt(8, 820, 820, 0, 0, false, false);
          if (newTag.mode == 0) {
            mp3.playMp3FolderTrack(807);  // Karte konfigurieren abgebrochen.
            waitPlaybackToFinish(255, 0, 0, 100);
            break;
          }
          else if (newTag.mode == SINGLE) {
            Serial.println(F("singletrack"));
            newTag.multiPurposeData1 = prompt(mp3.getFolderTrackCount(newTag.folder), 802, 0, 0, newTag.folder, true, false);
            newTag.multiPurposeData2 = 0;
            if (newTag.multiPurposeData1 == 0) {
              mp3.playMp3FolderTrack(807);  // Karte konfigurieren abgebrochen.
              waitPlaybackToFinish(255, 0, 0, 100);
              break;
            }
          }
          else if (newTag.mode == VSTORY || newTag.mode == VALBUM || newTag.mode == VPARTY) {
            Serial.println(F("starttrack"));
            newTag.multiPurposeData1 = prompt(mp3.getFolderTrackCount(newTag.folder), 803, 0, 0, newTag.folder, true, false);
            if (newTag.multiPurposeData1 == 0) {
              mp3.playMp3FolderTrack(807);  // Karte konfigurieren abgebrochen.
              waitPlaybackToFinish(255, 0, 0, 100);
              break;
            }
            Serial.println(F("endtrack"));
            newTag.multiPurposeData2 = prompt(mp3.getFolderTrackCount(newTag.folder), 804, 0, newTag.multiPurposeData1, newTag.folder, true, false);
            newTag.multiPurposeData2 = max(newTag.multiPurposeData1, newTag.multiPurposeData2);
            if (newTag.multiPurposeData2 == 0) {
              mp3.playMp3FolderTrack(807);  // Karte konfigurieren abgebrochen.
              waitPlaybackToFinish(255, 0, 0, 100);
              break;
            }
          }
          uint8_t bytesToWrite[] = {magicCookieHex[0],                 // 1st byte of magic cookie (by default 0x13)
                                    magicCookieHex[1],                 // 2nd byte of magic cookie (by default 0x37)
                                    magicCookieHex[2],                 // 3rd byte of magic cookie (by default 0xb3)
                                    magicCookieHex[3],                 // 4th byte of magic cookie (by default 0x47)
                                    0x01,                              // version 1
                                    newTag.folder,                     // the folder selected by the user
                                    newTag.mode,                       // the playback mode selected by the user
                                    newTag.multiPurposeData1,          // multi purpose data (ie. single track for mode 4 and start of vfolder)
                                    newTag.multiPurposeData2,          // multi purpose data (ie. end of vfolder, depending on mode)
                                    0x00, 0x00, 0x00,                  // reserved for future use
                                    0x00, 0x00, 0x00, 0x00             // reserved for future use
                                   };
          uint8_t writeNfcTagStatus = writeNfcTagData(bytesToWrite, sizeof(bytesToWrite));
          if (writeNfcTagStatus == 1) {
            mp3.playMp3FolderTrack(805);  // Ok, ich habe die Karte konfiguriert. Viel Spass damit.
            waitPlaybackToFinish(0, 255, 0, 100);
          }
          else {
            mp3.playMp3FolderTrack(806);
            waitPlaybackToFinish(255, 0, 0, 100);
          }
          break;
        }
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();

        // restore playback volume, can't be higher than maximum volume
        mp3.setVolume(playback.mp3CurrentVolume = min(playback.mp3CurrentVolume, preference.mp3MaxVolume));

        switchButtonConfiguration(PAUSE);
        shutdownTimer(START);
        inputEvent = NOP;
      }
      // # end - nfc tag does not have our magic cookie on it
      // ####################################################
    }
    // # end - nfc tag is successfully read
    // ####################################
  }
  // # end - main code block
  // #######################

  // ##################################################################################
  // # handle button and ir remote events during playback or while waiting for nfc tags
  // ir remote center: toggle box lock
  if (inputEvent == IRC) {
    if ((playback.isLocked = !playback.isLocked)) {
      Serial.println(F("lock"));
#if defined STATUSLED ^ defined STATUSLEDRGB
      statusLedUpdate(BURST4, 255, 0, 0, 0);
#endif
    }
    else {
      Serial.println(F("unlock"));
#if defined STATUSLED ^ defined STATUSLEDRGB
      statusLedUpdate(BURST8, 0, 255, 0, 0);
#endif
    }
  }
  // button 0 (middle) press or ir remote play/pause: toggle playback
  else if ((inputEvent == B0P && !playback.isLocked) || inputEvent == IRP) {
    if (playback.isPlaying) {
      switchButtonConfiguration(PAUSE);
      shutdownTimer(START);
      Serial.println(F("pause"));
      mp3.pause();
      // if the current playback mode is story book mode: store the current progress
      if (playback.currentTag.mode == STORYBOOK) {
        Serial.print(F("save "));
        printModeFolderTrack(true);
        EEPROM.write(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
      }
    }
    else {
      if (playback.playListMode) {
        switchButtonConfiguration(PLAY);
        shutdownTimer(STOP);
        Serial.println(F("play"));
        mp3.start();
      }
    }
  }
  // button 1 (right) press or ir remote up while playing: increase volume
  else if (((inputEvent == B1P && !playback.isLocked) || inputEvent == IRU) && playback.isPlaying) {
    if (playback.mp3CurrentVolume < preference.mp3MaxVolume) {
      mp3.setVolume(++playback.mp3CurrentVolume);
      Serial.print(F("volume "));
      Serial.println(playback.mp3CurrentVolume);
    }
#if defined STATUSLED
    else statusLedUpdate(BURST2, 255, 0, 0, 0);
#endif
  }
  // button 2 (left) press or ir remote down while playing: decrease volume
  else if (((inputEvent == B2P && !playback.isLocked) || inputEvent == IRD) && playback.isPlaying) {
    if (playback.mp3CurrentVolume > 1) {
      mp3.setVolume(--playback.mp3CurrentVolume);
      Serial.print(F("volume "));
      Serial.println(playback.mp3CurrentVolume);
    }
#if defined STATUSLED
    else statusLedUpdate(BURST2, 255, 0, 0, 0);
#endif
  }
  // button 1 (right) hold for 2 sec or button 5 press or ir remote right, only during (v)album, (v)party and story book mode while playing: next track
  else if ((((inputEvent == B1H || inputEvent == B4P) && !playback.isLocked) || inputEvent == IRR) && (playback.currentTag.mode == ALBUM || playback.currentTag.mode == PARTY || playback.currentTag.mode == STORYBOOK || playback.currentTag.mode == VALBUM || playback.currentTag.mode == VPARTY) && playback.isPlaying) {
    Serial.println(F("next"));
    playNextTrack(0, true, true);
  }
  // button 2 (left) hold for 2 sec or button 4 press or ir remote left, only during (v)album, (v)party and story book mode while playing: previous track
  else if ((((inputEvent == B2H || inputEvent == B3P) && !playback.isLocked) || inputEvent == IRL) && (playback.currentTag.mode == ALBUM || playback.currentTag.mode == PARTY || playback.currentTag.mode == STORYBOOK || playback.currentTag.mode == VALBUM || playback.currentTag.mode == VPARTY) && playback.isPlaying) {
    Serial.println(F("prev"));
    playNextTrack(0, false, true);
  }
  // button 0 (middle) hold for 5 sec or ir remote menu, only during (v)story, (v)album, (v)party and single mode while playing: toggle single track repeat
  else if (((inputEvent == B0H && !playback.isLocked) || inputEvent == IRM) && (playback.currentTag.mode == STORY || playback.currentTag.mode == ALBUM || playback.currentTag.mode == PARTY || playback.currentTag.mode == SINGLE || playback.currentTag.mode == VSTORY || playback.currentTag.mode == VALBUM || playback.currentTag.mode == VPARTY) && playback.isPlaying) {
    Serial.print(F("repeat "));
    if ((playback.isRepeat = !playback.isRepeat)) {
      Serial.println(F("on"));
#if defined STATUSLED ^ defined STATUSLEDRGB
      statusLedUpdate(BURST4, 255, 255, 255, 0);
#endif
    }
    else {
      Serial.println(F("off"));
#if defined STATUSLED ^ defined STATUSLEDRGB
      statusLedUpdate(BURST8, 255, 255, 255, 0);
#endif
    }
  }
  // button 0 (middle) hold for 5 sec or ir remote menu, only during story book mode while playing: reset progress
  else if (((inputEvent == B0H && !playback.isLocked) || inputEvent == IRM) && playback.currentTag.mode == STORYBOOK && playback.isPlaying) {
    playback.playListItem = 1;
    Serial.print(F("reset "));
    printModeFolderTrack(true);
    EEPROM.write(playback.currentTag.folder, 0);
    mp3.playFolderTrack(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
#if defined STATUSLED ^ defined STATUSLEDRGB
    statusLedUpdate(BURST8, 255, 0, 255, 0);
#endif
  }
  // button 0 (middle) hold for 5 sec or ir remote menu while not playing: parents menu
  else if (((inputEvent == B0H && !playback.isLocked) || inputEvent == IRM) && !playback.isPlaying) {
    parentsMenu();
    Serial.println(F("ready"));
  }
  // # end - handle button or ir remote events during playback or while waiting for nfc tags
  // #######################################################################################

  mp3.loop();
}

// ################################################################################################################################################################
// ################################################################ functions are below this line! ################################################################
// ################################################################################################################################################################

// checks all input sources (and populates the global inputEvent variable for ir events)
void checkForInput() {
  // clear inputEvent
  inputEvent = NOP;

  // check all buttons
  button0.check();
  button1.check();
  button2.check();
}

// translates the various button events into enums and populates the global inputEvent variable
void translateButtonInput(AceButton *button, uint8_t eventType, uint8_t /* buttonState */) {
  switch (button->getId()) {
    // button 0 (middle)
    case 0: {
        switch (eventType) {
          case AceButton::kEventClicked: {
              inputEvent = B0P;
              break;
            }
          case AceButton::kEventLongPressed: {
              inputEvent = B0H;
              break;
            }
          case AceButton::kEventDoubleClicked: {
              inputEvent = B0D;
              break;
            }
          default: {
              break;
            }
        }
        break;
      }
    // button 1 (right)
    case 1: {
        switch (eventType) {
          case AceButton::kEventClicked: {
              inputEvent = B1P;
              break;
            }
          case AceButton::kEventLongPressed: {
              inputEvent = B1H;
              break;
            }
          default: {
              break;
            }
        }
        break;
      }
    // button 2 (left)
    case 2: {
        switch (eventType) {
          case AceButton::kEventClicked: {
              inputEvent = B2P;
              break;
            }
          case AceButton::kEventLongPressed: {
              inputEvent = B2H;
              break;
            }
          default: {
              break;
            }
        }
        break;
      }
    default: {
        break;
      }
  }
}

// switches button configuration dependig on the state that TonUINO is in
void switchButtonConfiguration(uint8_t buttonMode) {
  switch (buttonMode) {
    case INIT: {
        // button 0 (middle)
        button0Config.setEventHandler(translateButtonInput);
        button0Config.setFeature(ButtonConfig::kFeatureClick);
        button0Config.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
        button0Config.setClickDelay(buttonClickDelay);
        button0Config.setFeature(ButtonConfig::kFeatureLongPress);
        button0Config.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
        button0Config.setLongPressDelay(buttonShortLongPressDelay);
        // button 1 (right)
        button1Config.setEventHandler(translateButtonInput);
        button1Config.setFeature(ButtonConfig::kFeatureClick);
        button1Config.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
        button1Config.setClickDelay(buttonClickDelay);
        button1Config.setFeature(ButtonConfig::kFeatureLongPress);
        button1Config.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
        button1Config.setLongPressDelay(buttonShortLongPressDelay);
        // button 2 (left)
        button2Config.setEventHandler(translateButtonInput);
        button2Config.setFeature(ButtonConfig::kFeatureClick);
        button2Config.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
        button2Config.setClickDelay(buttonClickDelay);
        button2Config.setFeature(ButtonConfig::kFeatureLongPress);
        button2Config.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
        button2Config.setLongPressDelay(buttonShortLongPressDelay);
        break;
      }
    case PLAY: {
        // button 0 (middle)
        button0Config.clearFeature(ButtonConfig::kFeatureDoubleClick);
        button0Config.clearFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
        button0Config.clearFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
        button0Config.setLongPressDelay(buttonLongLongPressDelay);
        break;
      }
    case PAUSE: {
        // button 0 (middle)
        button0Config.clearFeature(ButtonConfig::kFeatureDoubleClick);
        button0Config.clearFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
        button0Config.clearFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
        button0Config.setLongPressDelay(buttonLongLongPressDelay);
        break;
      }
    case PIN: {
        // button 0 (middle)
        button0Config.clearFeature(ButtonConfig::kFeatureDoubleClick);
        button0Config.clearFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
        button0Config.clearFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
        button0Config.setLongPressDelay(buttonShortLongPressDelay);
        break;
      }
    case CONFIG: {
        // button 0 (middle)
        button0Config.setFeature(ButtonConfig::kFeatureDoubleClick);
        button0Config.setFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
        button0Config.setFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
        button0Config.setLongPressDelay(buttonShortLongPressDelay);
        break;
      }
    default: {
        break;
      }
  }
}

// waits for current playing track to finish
void waitPlaybackToFinish(uint8_t red, uint8_t green, uint8_t blue, uint16_t statusLedUpdateInterval) {
  uint64_t waitPlaybackToStartMillis = millis();

  delay(500);
  // wait for playback to start
  while (digitalRead(mp3BusyPin)) {
    if (millis() - waitPlaybackToStartMillis >= 1000) break;
#if defined STATUSLED ^ defined STATUSLEDRGB
    statusLedUpdate(BLINK, red, green, blue, statusLedUpdateInterval);
#endif
  }
  // wait for playback to finish
  while (!digitalRead(mp3BusyPin)) {
#if defined STATUSLED ^ defined STATUSLEDRGB
    statusLedUpdate(BLINK, red, green, blue, statusLedUpdateInterval);
#endif
    // check for messages
    mp3.loop();
    delay(10);
  }
}

// prints current mode, folder and track information
void printModeFolderTrack(bool cr) {
  Serial.print(playbackModeName[playback.currentTag.mode]);
  Serial.print(F("-"));
  Serial.print(playback.currentTag.folder);
  Serial.print(F("-"));
  Serial.print(playback.playListItem);
  Serial.print(F("/"));
  Serial.print(playback.playListItemCount);
  if (playback.currentTag.mode == PARTY) {
    Serial.print(F("-("));
    Serial.print(playback.playList[playback.playListItem - 1]);
    Serial.print(F(")"));
  }
  else if (playback.currentTag.mode == VSTORY || playback.currentTag.mode == VALBUM || playback.currentTag.mode == VPARTY) {
    Serial.print(F("-("));
    Serial.print(playback.folderStartTrack);
    Serial.print(F("->"));
    Serial.print(playback.folderEndTrack);
    Serial.print(F("|"));
    Serial.print(playback.playList[playback.playListItem - 1]);
    Serial.print(F(")"));
  }
  if (cr) Serial.println();
}

// plays next track depending on the current playback mode
void playNextTrack(uint16_t globalTrack, bool directionForward, bool triggeredManually) {
  static uint16_t lastCallTrack = 0;

  // we only advance to a new track when in playlist mode, not during interactive prompt playback (ie. during configuration of a new nfc tag)
  if (!playback.playListMode) return;

  //delay 100ms to be on the safe side with the serial communication
  delay(100);

  // story mode (1): play one random track in folder
  // single mode (4): play one single track in folder
  // vstory mode (6): play one random track in virtual folder
  // there is no next track in story, single and vstory mode, stop playback
  if (playback.currentTag.mode == STORY || playback.currentTag.mode == SINGLE || playback.currentTag.mode == VSTORY) {
    if (playback.isRepeat) {
      lastCallTrack = 0;
      printModeFolderTrack(true);
      mp3.playFolderTrack(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
    }
    else {
      playback.playListMode = false;
      switchButtonConfiguration(PAUSE);
      shutdownTimer(START);
      Serial.print(playbackModeName[playback.currentTag.mode]);
      Serial.println(F("-stop"));
      mp3.stop();
    }
  }

  // album mode (2): play the complete folder
  // party mode (3): shuffle the complete folder
  // story book mode (5): play the complete folder and track progress
  // valbum mode (7): play the complete virtual folder
  // vparty mode (8): shuffle the complete virtual folder
  // advance to the next or previous track, stop if the end of the folder is reached
  if (playback.currentTag.mode == ALBUM || playback.currentTag.mode == PARTY || playback.currentTag.mode == STORYBOOK || playback.currentTag.mode == VALBUM || playback.currentTag.mode == VPARTY) {

    // **workaround for some DFPlayer mini modules that make two callbacks in a row when finishing a track**
    // reset lastCallTrack to avoid lockup when playback was just started
    if (playback.isFresh) {
      playback.isFresh = false;
      lastCallTrack = 0;
    }
    // check if we automatically get called with the same track number twice in a row, if yes return immediately
    if (lastCallTrack == globalTrack && !triggeredManually) return;
    else lastCallTrack = globalTrack;

    // play next track?
    if (directionForward) {
      // single track repeat is on, repeat current track
      if (playback.isRepeat && !triggeredManually) {
        lastCallTrack = 0;
        printModeFolderTrack(true);
        mp3.playFolderTrack(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
      }
      // there are more tracks after the current one, play next track
      else if (playback.playListItem < playback.playListItemCount) {
        playback.playListItem++;
        printModeFolderTrack(true);
        mp3.playFolderTrack(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
      }
      // there are no more tracks after the current one
      else {
        // if not triggered manually, stop playback (and reset progress)
        if (!triggeredManually) {
          playback.playListMode = false;
          switchButtonConfiguration(PAUSE);
          shutdownTimer(START);
          Serial.print(playbackModeName[playback.currentTag.mode]);
          Serial.print(F("-stop"));
          if (playback.currentTag.mode == STORYBOOK) {
            Serial.print(F("-reset"));
            EEPROM.write(playback.currentTag.folder, 0);
          }
          Serial.println();
          mp3.stop();
        }
#if defined STATUSLED
        else statusLedUpdate(BURST2, 255, 0, 0, 0);
#endif
      }
    }
    // play previous track?
    else {
      // there are more tracks before the current one, play the previous track
      if (playback.playListItem > 1) {
        playback.playListItem--;
        printModeFolderTrack(true);
        mp3.playFolderTrack(playback.currentTag.folder, playback.playList[playback.playListItem - 1]);
      }
#if defined STATUSLED
      else statusLedUpdate(BURST2, 255, 0, 0, 0);
#endif
    }
  }
}

// reads data from nfc tag
uint8_t readNfcTagData() {
  Serial.println("Read NFC Tag Data");
  uint8_t nfcTagReadBuffer[16] = {};
  uint8_t piccReadBuffer[18] = {};
  uint8_t piccReadBufferSize = sizeof(piccReadBuffer);
  bool nfcTagReadSuccess = false;
  MFRC522::StatusCode piccStatus;
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);

  // decide which code path to take depending on picc type
  if (piccType == MFRC522::PICC_TYPE_MIFARE_MINI || piccType ==  MFRC522::PICC_TYPE_MIFARE_1K || piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
    uint8_t classicBlock = 4;
    uint8_t classicTrailerBlock = 7;
    MFRC522::MIFARE_Key classicKey;
    for (uint8_t i = 0; i < 6; i++) classicKey.keyByte[i] = 0xFF;

    // check if we can authenticate with classicKey
    piccStatus = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_B, classicTrailerBlock, &classicKey, &mfrc522.uid);
    if (piccStatus == MFRC522::STATUS_OK) {
      // read 16 bytes from nfc tag (by default sector 1 / block 4)
      piccStatus = (MFRC522::StatusCode)mfrc522.MIFARE_Read(classicBlock, piccReadBuffer, &piccReadBufferSize);
      if (piccStatus == MFRC522::STATUS_OK) {
        nfcTagReadSuccess = true;
        memcpy(nfcTagReadBuffer, piccReadBuffer, sizeof(nfcTagReadBuffer));
      }
      else Serial.println(mfrc522.GetStatusCodeName(piccStatus));
    }
    else Serial.println(mfrc522.GetStatusCodeName(piccStatus));
  }
  else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
    uint8_t ultralightStartPage = 8;
    uint8_t ultralightACK[2] = {};
    MFRC522::MIFARE_Key ultralightKey;
    for (uint8_t i = 0; i < 4; i++) ultralightKey.keyByte[i] = 0xFF;

    // check if we can authenticate with ultralightKey
    piccStatus = (MFRC522::StatusCode)mfrc522.PCD_NTAG216_AUTH(ultralightKey.keyByte, ultralightACK);
    if (piccStatus == MFRC522::STATUS_OK) {
      // read 16 bytes from nfc tag (by default pages 8-11)
      for (uint8_t ultralightPage = ultralightStartPage; ultralightPage < ultralightStartPage + 4; ultralightPage++) {
        piccStatus = (MFRC522::StatusCode)mfrc522.MIFARE_Read(ultralightPage, piccReadBuffer, &piccReadBufferSize);
        if (piccStatus == MFRC522::STATUS_OK) {
          nfcTagReadSuccess = true;
          memcpy(nfcTagReadBuffer + ((ultralightPage * 4) - (ultralightStartPage * 4)), piccReadBuffer, 4);
        }
        else {
          nfcTagReadSuccess = false;
          Serial.println(mfrc522.GetStatusCodeName(piccStatus));
          break;
        }
      }
    }
    else Serial.println(mfrc522.GetStatusCodeName(piccStatus));
  }
  // picc type is not supported
  else {
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return 0;
  }

  Serial.print(nfcStatusMessage[1]);
  Serial.print(nfcStatusMessage[0]);
  printNfcTagType(piccType);
  // read was successfull
  if (nfcTagReadSuccess) {
    // log data to the console
    Serial.print(nfcStatusMessage[3]);
    printNfcTagData(nfcTagReadBuffer, sizeof(nfcTagReadBuffer), true);

    // convert 4 byte magic cookie to 32bit decimal for easier handling
    uint32_t tempMagicCookie = 0;
    tempMagicCookie  = (uint32_t)nfcTagReadBuffer[0] << 24;
    tempMagicCookie += (uint32_t)nfcTagReadBuffer[1] << 16;
    tempMagicCookie += (uint32_t)nfcTagReadBuffer[2] << 8;
    tempMagicCookie += (uint32_t)nfcTagReadBuffer[3];

    // if cookie is not blank, update ncfTag object with data read from nfc tag
    if (tempMagicCookie != 0) {
      playback.currentTag.cookie = tempMagicCookie;
      playback.currentTag.version = nfcTagReadBuffer[4];
      playback.currentTag.folder = nfcTagReadBuffer[5];
      playback.currentTag.mode = nfcTagReadBuffer[6];
      playback.currentTag.multiPurposeData1 = nfcTagReadBuffer[7];
      playback.currentTag.multiPurposeData2 = nfcTagReadBuffer[8];
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
    // if magic cookie is blank, clear ncfTag object
    else {
      playback.currentTag.cookie = 0;
      playback.currentTag.version = 0;
      playback.currentTag.folder = 0;
      playback.currentTag.mode = 0;
      playback.currentTag.multiPurposeData1 = 0;
      playback.currentTag.multiPurposeData2 = 0;
    }
    return 1;
  }
  // read was not successfull
  else {
    Serial.println(nfcStatusMessage[4]);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return 0;
  }
}

// writes data to nfc tag
uint8_t writeNfcTagData(uint8_t nfcTagWriteBuffer[], uint8_t nfcTagWriteBufferSize) {
  Serial.println("write NFC Tag Data");
  uint8_t piccWriteBuffer[16] = {};
  bool nfcTagWriteSuccess = false;
  MFRC522::StatusCode piccStatus;
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);

   Serial.print("piccType: ");
   Serial.print(piccType);
   Serial.print(" ");
   
  // decide which code path to take depending on picc type
  if (piccType == MFRC522::PICC_TYPE_MIFARE_MINI || piccType ==  MFRC522::PICC_TYPE_MIFARE_1K || piccType == MFRC522::PICC_TYPE_MIFARE_4K) {
    Serial.println("Mini, 1K, 4K");
    uint8_t classicBlock = 4;
    uint8_t classicTrailerBlock = 7;
    MFRC522::MIFARE_Key classicKey;
    for (uint8_t i = 0; i < 6; i++) classicKey.keyByte[i] = 0xFF;

    // check if we can authenticate with classicKey
    piccStatus = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_B, classicTrailerBlock, &classicKey, &mfrc522.uid);
    if (piccStatus == MFRC522::STATUS_OK) {
      // write 16 bytes to nfc tag (by default sector 1 / block 4)
      memcpy(piccWriteBuffer, nfcTagWriteBuffer, sizeof(piccWriteBuffer));
      piccStatus = (MFRC522::StatusCode)mfrc522.MIFARE_Write(classicBlock, piccWriteBuffer, sizeof(piccWriteBuffer));
      if (piccStatus == MFRC522::STATUS_OK) nfcTagWriteSuccess = true;
      else Serial.println(mfrc522.GetStatusCodeName(piccStatus));
    }
    else Serial.println(mfrc522.GetStatusCodeName(piccStatus));
  }
  else if (piccType == MFRC522::PICC_TYPE_MIFARE_UL) {
    Serial.println("UL (ultralight)");
    uint8_t ultralightStartPage = 8;
    uint8_t ultralightACK[2] = {};
    MFRC522::MIFARE_Key ultralightKey;
    for (uint8_t i = 0; i < 4; i++) ultralightKey.keyByte[i] = 0xFF;

    // check if we can authenticate with ultralightKey
    piccStatus = (MFRC522::StatusCode)mfrc522.PCD_NTAG216_AUTH(ultralightKey.keyByte, ultralightACK);
    if (piccStatus == MFRC522::STATUS_OK) {
      // write 16 bytes to nfc tag (by default pages 8-11)
      for (uint8_t ultralightPage = ultralightStartPage; ultralightPage < ultralightStartPage + 4; ultralightPage++) {
        memcpy(piccWriteBuffer, nfcTagWriteBuffer + ((ultralightPage * 4) - (ultralightStartPage * 4)), 4);
        piccStatus = (MFRC522::StatusCode)mfrc522.MIFARE_Write(ultralightPage, piccWriteBuffer, sizeof(piccWriteBuffer));
        if (piccStatus == MFRC522::STATUS_OK) nfcTagWriteSuccess = true;
        else {
          nfcTagWriteSuccess = false;
          Serial.println(mfrc522.GetStatusCodeName(piccStatus));
          break;
        }
      }
    }
    else Serial.println(mfrc522.GetStatusCodeName(piccStatus));
  }
  // picc type is not supported
  else {
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return 0;
  }

  Serial.print(nfcStatusMessage[2]);
  Serial.print(nfcStatusMessage[0]);
  printNfcTagType(piccType);
  // write was successfull
  if (nfcTagWriteSuccess) {
    // log data to the console
    Serial.print(nfcStatusMessage[3]);
    printNfcTagData(nfcTagWriteBuffer, nfcTagWriteBufferSize, true);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return 1;
  }
  // write was not successfull
  else {
    Serial.println(nfcStatusMessage[4]);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return 0;
  }
}

// prints nfc tag data
void printNfcTagData(uint8_t dataBuffer[], uint8_t dataBufferSize, bool cr) {
  for (uint8_t i = 0; i < dataBufferSize; i++) {
    Serial.print(dataBuffer[i] < 0x10 ? " 0" : " ");
    Serial.print(dataBuffer[i], HEX);
  }
  if (cr) Serial.println();
}

// prints nfc tag type
void printNfcTagType(MFRC522::PICC_Type nfcTagType) {
  switch (nfcTagType) {
    case MFRC522::PICC_TYPE_MIFARE_MINI: {}
    case MFRC522::PICC_TYPE_MIFARE_1K: {}
    case MFRC522::PICC_TYPE_MIFARE_4K: {
        Serial.print(F("cl"));
        break;
      }
    case MFRC522::PICC_TYPE_MIFARE_UL: {
        Serial.print(F("ul|nt"));
        break;
      }
    default: {
        Serial.print(F("??"));
        break;
      }
  }
  Serial.print(nfcStatusMessage[0]);
}

// starts, stops and checks the shutdown timer
void shutdownTimer(uint8_t timerAction) {
  static uint64_t shutdownMillis = 0;

  switch (timerAction) {
    case START: {
        if (preference.shutdownMinutes != 0) shutdownMillis = millis() + (preference.shutdownMinutes * 60000);
        else shutdownMillis = 0;
        break;
      }
    case STOP: {
        shutdownMillis = 0;
        break;
      }
    case CHECK: {
        if (shutdownMillis != 0 && millis() > shutdownMillis) {
          shutdownTimer(SHUTDOWN);
        }
        break;
      }
    case SHUTDOWN: {
#if defined STATUSLED ^ defined STATUSLEDRGB
        statusLedUpdate(OFF, 0, 0, 0, 0);
#endif
#if defined POLOLUSWITCH
//        digitalWrite(shutdownPin, HIGH);
#else
//        digitalWrite(shutdownPin, LOW);
#endif
        mfrc522.PCD_AntennaOff();
        mfrc522.PCD_SoftPowerDown();
        mp3.sleep();
        ESP.deepSleep(0);
        cli();
        break;
      }
    default: {
        break;
      }
  }
}

// reads, writes, migrates and resets preferences in eeprom
void preferences(uint8_t preferenceAction) {
  Serial.print(F("prefs "));
  switch (preferenceAction) {
    case READ: {
        Serial.println(F("read"));
        EEPROM.get(100, preference);
        if (preference.cookie != preferenceCookie) preferences(RESET);
        else {
          Serial.print(F("  v"));
          Serial.println(preference.version);
          preferences(MIGRATE);
        }
        break;
      }
    case WRITE: {
        Serial.println(F("write"));
        EEPROM.put(100, preference);
        break;
      }
    case MIGRATE: {
        Serial.println(F("migrate"));
        // prepared for future preferences migration
        switch (preference.version) {
          //case 1: {
          //    Serial.println(F("  v1->v2"));
          //    preference.version = 2;
          //  }
          //case 2: {
          //    Serial.println(F("  v2->v3"));
          //    preference.version = 3;
          //    preferences(WRITE);
          //    break;
          //  }
          default: {
              Serial.println(F("  -"));
              break;
            }
        }
        break;
      }
    case RESET: {
        Serial.println(F("reset"));
        preference.cookie = preferenceCookie;
        preference.version = preferenceVersion;
        preference.mp3StartVolume = mp3StartVolumeDefault;
        preference.mp3MaxVolume = mp3MaxVolumeDefault;
        preference.mp3MenuVolume = mp3MenuVolumeDefault;
        preference.mp3Equalizer = mp3EqualizerDefault;
        preference.shutdownMinutes = shutdownMinutesDefault;
        preferences(WRITE);
        break;
      }
    case RESET_PROGRESS: {
        Serial.println(F("reset progress"));
        for (uint16_t i = 1; i < 100; i++) EEPROM.write(i, 0);
        break;
      }
    default: {
        break;
      }
  }
}

// interactively prompts the user for options
uint8_t prompt(uint8_t promptOptions, uint16_t promptHeading, uint16_t promptOffset, uint8_t promptCurrent, uint8_t promptFolder, bool promptPreview, bool promptChangeVolume) {
  uint8_t promptResult = promptCurrent;

  Serial.print("prompt: ");
  Serial.print("# Options: ");
  Serial.print(promptOptions);
  Serial.print(", Announcement: ");
  Serial.print(promptHeading);
  Serial.print(", Offset: ");
  Serial.print(promptOffset);
  Serial.print(", Current: ");
  Serial.print(promptCurrent);
  Serial.print(", Folder: ");
  Serial.print(promptFolder);
  Serial.print(", Preview: ");
  Serial.print(promptPreview);
  Serial.print(", Volume: ");
  Serial.print(promptChangeVolume);
  Serial.println(".");
  
  mp3.playMp3FolderTrack(promptHeading);
  while (true) {
    playback.isPlaying = !digitalRead(mp3BusyPin);
    checkForInput();
    // serial console input
    if (Serial.available() > 0) {
      uint32_t promptResultSerial = Serial.parseInt();
      if (promptResultSerial != 0 && promptResultSerial <= promptOptions) {
        Serial.println(promptResultSerial);
        return (uint8_t)promptResultSerial;
      }
    }
    // button 0 (middle) press or ir remote play/pause: confirm selection
    if ((inputEvent == B0P || inputEvent == IRP) && promptResult != 0) {
      if (promptPreview && !playback.isPlaying) {
        if (promptFolder == 0) mp3.playFolderTrack(promptResult, 1);
        else mp3.playFolderTrack(promptFolder, promptResult);
      }
      else return promptResult;
    }
    // button 0 (middle) double click or ir remote center: announce current folder, track number or option
    else if ((inputEvent == B0D || inputEvent == IRC) && promptResult != 0) {
      if (promptPreview && playback.isPlaying) mp3.playAdvertisement(promptResult);
      else if (promptPreview && !playback.isPlaying) {
        if (promptFolder == 0) mp3.playFolderTrack(promptResult, 1);
        else mp3.playFolderTrack(promptFolder, promptResult);
      }
      else {
        if (promptChangeVolume) mp3.setVolume(promptResult + promptOffset);
        mp3.playMp3FolderTrack(promptResult + promptOffset);
      }
    }
    // button 1 (right) press or ir remote up: next folder, track number or option
    else if (inputEvent == B1P || inputEvent == IRU) {
      promptResult = min<int>(promptResult + 1, promptOptions);
      Serial.println(promptResult);
      if (promptPreview) {
        if (promptFolder == 0) mp3.playFolderTrack(promptResult, 1);
        else mp3.playFolderTrack(promptFolder, promptResult);
      }
      else {
        if (promptChangeVolume) mp3.setVolume(promptResult + promptOffset);
        mp3.playMp3FolderTrack(promptResult + promptOffset);
      }
    }
    // button 2 (left) press or ir remote up: previous folder, track number or option
    else if (inputEvent == B2P || inputEvent == IRD) {
      promptResult = max(promptResult - 1, 1);
      Serial.println(promptResult);
      if (promptPreview) {
        if (promptFolder == 0) mp3.playFolderTrack(promptResult, 1);
        else mp3.playFolderTrack(promptFolder, promptResult);
      }
      else {
        if (promptChangeVolume) mp3.setVolume(promptResult + promptOffset);
        mp3.playMp3FolderTrack(promptResult + promptOffset);
      }
    }
    // button 0 (middle) hold for 2 sec or ir remote menu: cancel
    else if (inputEvent == B0H || inputEvent == IRM) {
      Serial.println(F("cancel"));
      return 0;
    }
    // button 1 (right) hold or ir remote right: jump 10 folders, tracks or options forward
    else if (inputEvent == B1H || inputEvent == B4P || inputEvent == IRR) {
      promptResult = min<int>(promptResult + 10, promptOptions);
      Serial.println(promptResult);
      if (promptChangeVolume) mp3.setVolume(promptResult + promptOffset);
      mp3.playMp3FolderTrack(promptResult + promptOffset);
    }
    // button 2 (left) hold or ir remote left: jump 10 folders, tracks or options backwards
    else if (inputEvent == B2H || inputEvent == B3P || inputEvent == IRL) {
      promptResult = max(promptResult - 10, 1);
      Serial.println(promptResult);
      if (promptChangeVolume) mp3.setVolume(promptResult + promptOffset);
      mp3.playMp3FolderTrack(promptResult + promptOffset);
    }
#if defined STATUSLED ^ defined STATUSLEDRGB
    statusLedUpdate(BLINK, 255, 255, 0, 500);
#endif
    mp3.loop();
  }
}

// parents menu, offers various settings only parents do
void parentsMenu() {
  playback.playListMode = false;

  // set volume to menu volume
  mp3.setVolume(preference.mp3MenuVolume);

  switchButtonConfiguration(CONFIG);
  shutdownTimer(STOP);

  while (true) {
    Serial.println(F("parents"));
    uint8_t selectedOption = prompt(10, 900, 909, 0, 0, false, false);
    // cancel
    if (selectedOption == 0) {
      mp3.playMp3FolderTrack(904);
      waitPlaybackToFinish(255, 255, 0, 100);
      break;
    }
    // erase tag
    else if (selectedOption == 1) {
      Serial.println(F("erase tag"));
      mp3.playMp3FolderTrack(920);
      // loop until tag is erased
      uint8_t writeNfcTagStatus = 0;
      while (!writeNfcTagStatus) {
        checkForInput();
        // button 0 (middle) hold for 2 sec or ir remote menu: cancel erase nfc tag
        if (inputEvent == B0H || inputEvent == IRM) {
          Serial.println(F("cancel"));
          mp3.playMp3FolderTrack(923);
          waitPlaybackToFinish(255, 0, 0, 100);
          break;
        }
        // wait for nfc tag, erase once detected
        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
          uint8_t bytesToWrite[16] = {};
          writeNfcTagStatus = writeNfcTagData(bytesToWrite, sizeof(bytesToWrite));
          if (writeNfcTagStatus == 1) {
            mp3.playMp3FolderTrack(921);
            waitPlaybackToFinish(0, 255, 0, 100);
          }
          else mp3.playMp3FolderTrack(922);
        }
#if defined STATUSLED ^ defined STATUSLEDRGB
        statusLedUpdate(BLINK, 255, 0, 255, 500);
#endif
        mp3.loop();
      }
    }
    // startup volume
    else if (selectedOption == 2) {
      Serial.println(F("start vol"));
      uint8_t promptResult = prompt(preference.mp3MaxVolume, 930, 0, preference.mp3StartVolume, 0, false, true);
      if (promptResult != 0) {
        preference.mp3StartVolume = promptResult;
        preferences(WRITE);
        // set volume to menu volume
        mp3.setVolume(preference.mp3MenuVolume);
        mp3.playMp3FolderTrack(901);
        waitPlaybackToFinish(0, 255, 0, 100);
      }
    }
    // maximum volume
    else if (selectedOption == 3) {
      Serial.println(F("max vol"));
      uint8_t promptResult = prompt(30, 931, 0, preference.mp3MaxVolume, 0, false, true);
      if (promptResult != 0) {
        preference.mp3MaxVolume = promptResult;
        // startup volume can't be higher than maximum volume
        preference.mp3StartVolume = min(preference.mp3StartVolume, preference.mp3MaxVolume);
        preferences(WRITE);
        // set volume to menu volume
        mp3.setVolume(preference.mp3MenuVolume);
        mp3.playMp3FolderTrack(901);
        waitPlaybackToFinish(0, 255, 0, 100);
      }
    }
    // parents volume
    else if (selectedOption == 4) {
      Serial.println(F("menu vol"));
      uint8_t promptResult = prompt(30, 932, 0, preference.mp3MenuVolume, 0, false, true);
      if (promptResult != 0) {
        preference.mp3MenuVolume = promptResult;
        preferences(WRITE);
        // set volume to menu volume
        mp3.setVolume(preference.mp3MenuVolume);
        mp3.playMp3FolderTrack(901);
        waitPlaybackToFinish(0, 255, 0, 100);
      }
    }
    // equalizer
    else if (selectedOption == 5) {
      Serial.println(F("eq"));
      uint8_t promptResult = prompt(6, 940, 940, preference.mp3Equalizer, 0, false, false);
      if (promptResult != 0) {
        preference.mp3Equalizer = promptResult;
        mp3.setEq((DfMp3_Eq)(preference.mp3Equalizer - 1));
        preferences(WRITE);
        mp3.playMp3FolderTrack(901);
        waitPlaybackToFinish(0, 255, 0, 100);
      }
    }
    // learn ir remote
    else if (selectedOption == 6) {
      mp3.playMp3FolderTrack(950);
      waitPlaybackToFinish(255, 0, 0, 100);
    }
    // shutdown timer
    else if (selectedOption == 7) {
      Serial.println(F("timer"));
      uint8_t promptResult = prompt(7, 960, 960, 0, 0, false, false);
      if (promptResult != 0) {
        switch (promptResult) {
          case 1: {
              preference.shutdownMinutes = 5;
              break;
            }
          case 2: {
              preference.shutdownMinutes = 10;
              break;
            }
          case 3: {
              preference.shutdownMinutes = 15;
              break;
            }
          case 4: {
              preference.shutdownMinutes = 20;
              break;
            }
          case 5: {
              preference.shutdownMinutes = 30;
              break;
            }
          case 6: {
              preference.shutdownMinutes = 60;
              break;
            }
          case 7: {
              preference.shutdownMinutes = 0;
              break;
            }
          default: {
              break;
            }
        }
        preferences(WRITE);
        mp3.playMp3FolderTrack(901);
        waitPlaybackToFinish(0, 255, 0, 100);
      }
    }
    // reset progress
    else if (selectedOption == 8) {
      preferences(RESET_PROGRESS);
      mp3.playMp3FolderTrack(902);
      waitPlaybackToFinish(0, 255, 0, 100);
    }
    // reset preferences
    else if (selectedOption == 9) {
      preferences(RESET);
      mp3.setVolume(preference.mp3MenuVolume);
      mp3.setEq((DfMp3_Eq)(preference.mp3Equalizer - 1));
      mp3.playMp3FolderTrack(903);
      waitPlaybackToFinish(0, 255, 0, 100);
    }
    // manual box shutdown
    else if (selectedOption == 10) {
      Serial.println(F("manual shut"));
      shutdownTimer(SHUTDOWN);
    }
    mp3.loop();
  }

  // restore playback volume, can't be higher than maximum volume
  mp3.setVolume(playback.mp3CurrentVolume = min(playback.mp3CurrentVolume, preference.mp3MaxVolume));

  switchButtonConfiguration(PAUSE);
  shutdownTimer(START);
  inputEvent = NOP;
}

#if defined STATUSLED ^ defined STATUSLEDRGB
// updates status led(s) with various pulse, blink or burst patterns
void statusLedUpdate(uint8_t statusLedAction, uint8_t red, uint8_t green, uint8_t blue, uint16_t statusLedUpdateInterval) {
  static bool statusLedState = true;
  static bool statusLedDirection = false;
  static int16_t statusLedFade = 255;
  static uint64_t statusLedOldMillis;

  if (millis() - statusLedOldMillis >= statusLedUpdateInterval) {
    statusLedOldMillis = millis();
    switch (statusLedAction) {
      case OFF: {
          statusLedUpdateHal(red, green, blue, 0);
          break;
        }
      case SOLID: {
          statusLedFade = 255;
          statusLedUpdateHal(red, green, blue, 255);
          break;
        }
      case PULSE: {
          if (statusLedDirection) {
            statusLedFade += 10;
            if (statusLedFade >= 255) {
              statusLedFade = 255;
              statusLedDirection = !statusLedDirection;
            }
          }
          else {
            statusLedFade -= 10;
            if (statusLedFade <= 0) {
              statusLedFade = 0;
              statusLedDirection = !statusLedDirection;
            }
          }
          statusLedUpdateHal(red, green, blue, statusLedFade);
          break;
        }
      case BLINK: {
          statusLedState = !statusLedState;
          if (statusLedState) statusLedUpdateHal(red, green, blue, 255);
          else statusLedUpdateHal(0, 0, 0, 0);
          break;
        }
      case BURST2: {
          for (uint8_t i = 0; i < 4; i++) {
            statusLedState = !statusLedState;
            if (statusLedState) statusLedUpdateHal(red, green, blue, 255);
            else statusLedUpdateHal(0, 0, 0, 0);
            delay(100);
          }
          break;
        }
      case BURST4: {
          for (uint8_t i = 0; i < 8; i++) {
            statusLedState = !statusLedState;
            if (statusLedState) statusLedUpdateHal(red, green, blue, 255);
            else statusLedUpdateHal(0, 0, 0, 0);
            delay(100);
          }
          break;
        }
      case BURST8: {
          for (uint8_t i = 0; i < 16; i++) {
            statusLedState = !statusLedState;
            if (statusLedState) statusLedUpdateHal(red, green, blue, 255);
            else statusLedUpdateHal(0, 0, 0, 0);
            delay(100);
          }
          break;
        }
      default: {
          break;
        }
    }
  }
}

// abstracts status led(s) depending on what hardware is actually used (vanilla or ws281x led(s))
void statusLedUpdateHal(uint8_t red, uint8_t green, uint8_t blue, int16_t brightness) {
#if defined STATUSLEDRGB
  cRGB rgbLedColor;

  // apply brightness and max brightness
  rgbLedColor.r = (uint8_t)(((brightness / 255.0) * red) * (min(statusLedMaxBrightness, 100) / 100.0));
  rgbLedColor.g = (uint8_t)(((brightness / 255.0) * green) * (min(statusLedMaxBrightness, 100) / 100.0));
  rgbLedColor.b = (uint8_t)(((brightness / 255.0) * blue) * (min(statusLedMaxBrightness, 100) / 100.0));

  // update led buffer
  for (uint8_t i = 0; i < statusLedCount; i++) rgbLed.set_crgb_at(i, rgbLedColor);

  // send out the updated buffer
  rgbLed.sync();
#else
  // update vanilla led
  analogWrite(statusLedPin, (uint8_t)(brightness));
#endif
}
#endif
