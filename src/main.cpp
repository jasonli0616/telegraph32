#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LiquidCrystal_I2C.h>


const int BUTTON_1_PIN = 14;
const int BUTTON_2_PIN = 27;
const int GREEN_LED_PIN = 16;
const int BUZZER_PIN = 17;


const int MAX_MESSAGE_SIZE = 128; // 128 bits = 16 bytes


const int LCD_COLS = 16;
const int LCD_ROWS = 2;


LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);


uint8_t receiverMacAddress[6];
esp_now_peer_info_t peerInfo;

char hexadecimalCharacters[] = "0123456789ABCDEF";

// Morse code translations
const int AMOUNT_OF_TRANSLATABLE_CHARACTERS = 36;
char characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
String morseCodeCharacters[] = {"01", "1000", "1010", "100", "0", "0010", "110", "0000", "00", "0111", "101", "0100", "11", "10", "111", "0110", "1101", "010", "000", "1", "001", "0001", "011", "1001", "1011", "1100", "01111", "00111", "00011", "00001", "00000", "10000", "11000", "11100", "11110", "11111"};


// Message structure
typedef struct struct_message {
  boolean morseCode[MAX_MESSAGE_SIZE];
  int size;
  boolean isSpace;
} struct_message;

struct_message incomingReadings;


void lcdClearAndWrite(String text) {
  lcd.clear();
  lcd.setCursor(0, 0);

  for (int i = 0; i < text.length(); i++) {
    if (i == 16)
      lcd.setCursor(0, 1);
    
    lcd.write(text[i]);
  }
}


boolean isButtonPressed(int buttonNumber) {
  if (buttonNumber == 1) {
    return digitalRead(BUTTON_1_PIN) == LOW;
  } else if (buttonNumber == 2) {
    return digitalRead(BUTTON_2_PIN) == LOW;
  }
  return false;
}


int waitForButtonPress() {
  while (true) {
    if (isButtonPressed(1)) {
      return 1;
    } else if (isButtonPressed(2)) {
      return 2;
    }
  }
}


void getPeerMacAddress() {

  int peerMacAddressDecimal[12];
  uint8_t peerMacAddressHex[6];

  lcdClearAndWrite("Enter peer MAC:");

  for (int i = 0; i < 12; i++) {

    int characterIndex = 0;
    boolean confirmedCharacter = false;

    while (!confirmedCharacter) {

      lcd.setCursor(i, 1);
      lcd.write(hexadecimalCharacters[characterIndex]);

      int buttonPressed = waitForButtonPress();
      delay(300);
      if (buttonPressed == 1) {
        if (characterIndex < 15) {
          characterIndex++;
        } else {
          characterIndex = 0;
        }
      } else if (buttonPressed == 2) {
        confirmedCharacter = true;
        peerMacAddressDecimal[i] = characterIndex;
      }

    }
    
  }

  for (int i = 0; i < 12; i++) {
    if (i % 2 == 0) {
      // 16th place
      peerMacAddressHex[i / 2] = (uint8_t) 16 * peerMacAddressDecimal[i];
    } else {
      // 1s place
      peerMacAddressHex[(i-1) / 2] += (uint8_t) peerMacAddressDecimal[i];
    }
  }

  for (int i = 0; i < 6; i++) {
    receiverMacAddress[i] = peerMacAddressHex[i];
  }

}


void playBuzzer(boolean isDash) {
  int delayTime = isDash ? 200 : 50;
  digitalWrite(BUZZER_PIN, HIGH);
  delay(delayTime);
  digitalWrite(BUZZER_PIN, LOW);
}


void setGreenLED(boolean lightOn) {
  digitalWrite(GREEN_LED_PIN, lightOn ? HIGH : LOW);
}


void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  setGreenLED(false);

  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));

  if (incomingReadings.isSpace) {
    lcd.print(" ");
    return;
  }

  String messageMorseCode = "";

  for (int i = 0; i < incomingReadings.size; i++) {
    boolean isDash = incomingReadings.morseCode[i];
    messageMorseCode += isDash ? "1" : "0";
    
    playBuzzer(isDash);
    delay(100);
  }

  // Linear search for translation (should be fine for small amount of characters)
  boolean foundCharacter = false;
  for (int i = 0; i < AMOUNT_OF_TRANSLATABLE_CHARACTERS; i++) {
    if (morseCodeCharacters[i].equals(messageMorseCode)) {
      lcd.print(characters[i]);
      foundCharacter = true;
    }
  }

  if (!foundCharacter) {
    lcd.print("?");
  }

  setGreenLED(true);
}


void setup() {
  pinMode(BUTTON_1_PIN, INPUT);
  pinMode(BUTTON_2_PIN, INPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Set wifi mode: station
  WiFi.mode(WIFI_STA);
  
  // Initialize
  if (esp_now_init() != ESP_OK) {
    lcdClearAndWrite("Error initializing ESP-NOW");
    delay(100);
    return;
  }

  // Display MAC Address on LCD
  lcdClearAndWrite("MAC Address: " + WiFi.macAddress());
  while (waitForButtonPress() != 2) {} // wait until button pressed
  delay(500);

  // Determine peer MAC Address
  getPeerMacAddress();

  // Register peer
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    lcdClearAndWrite("Failed to add peer");
    delay(100);
    return;
  }

  // Register receive message
  esp_now_register_recv_cb(onDataRecv);

  lcd.clear();
  lcd.setCursor(LCD_COLS, 0);
  lcd.autoscroll();

}


boolean sendMessage(boolean morseCode[], int messageSize, boolean isSpace) {
  struct_message message;
  message.size = messageSize;

  for (int i = 0; i < messageSize; i++) {
    message.morseCode[i] = morseCode[i];
  }
  message.isSpace = isSpace;
  esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *) &message, sizeof(message));

  return result == ESP_OK;
}

void listenForTypeMessage() {

  int timeSinceLastPress = 0;
  boolean isPressed = false;

  boolean messagePresent = false;

  boolean currentCharacter[128];
  int size = 0;

  while (timeSinceLastPress < 1200) {

    if (isButtonPressed(1)) {
      setGreenLED(false);

      timeSinceLastPress = 0;

      isPressed = true;
      messagePresent = true;

      boolean isDash = false;
      delay(200);
      if (isButtonPressed(1)) {
        isDash = true;
      } else {
        isPressed = false;
      }
      currentCharacter[size] = isDash;
      size++;

      while (isPressed) {
        if (!isButtonPressed(1)) {
          isPressed = false;
        }
      }
    } else if (isButtonPressed(2)) {
      // Send space
      sendMessage({}, 0, true);
      delay(500);
      return;
    }
    timeSinceLastPress++;
    delay(1);

  }

  if (messagePresent) {
    sendMessage(currentCharacter, size, false);
  }

}


void loop() {

  setGreenLED(true);

  listenForTypeMessage();


}