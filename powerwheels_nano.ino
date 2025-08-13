#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

// --- Pin Definitions for Arduino Nano ---
const int MOTOR_LEAD1_ADC_PIN = A0;
const int MOTOR_LEAD2_ADC_PIN = A1;
const float VOLTAGE_DIVIDER_R1 = 330000.0;
const float VOLTAGE_DIVIDER_R2 = 68000.0;
const float ADC_MAX_READING = 1023.0;
const float ADC_REFERENCE_VOLTAGE = 5.0;
const int SELECTED_VOLTAGE_ADC_PIN = A2;

#define LED_PIN_REV 6
#define NUM_LEDS_REV 4
Adafruit_NeoPixel strip_rev(NUM_LEDS_REV, LED_PIN_REV, NEO_GRB + NEO_KHZ800);

#define LED_PIN 7
#define NUM_LEDS 14

const int LED_MAN_TOGGLE_PIN = 8;
const int BRIGHTNESS_POT_PIN = A3;

LiquidCrystal_I2C lcd(0x27, 16, 2);
const int MEGAPHONE_BUTTON_PIN = 5;
const int EFFECT_BUTTON_PIN = 4;

// --- Global Variables ---
float currentMotorVoltage = 0.0;
String currentMotorDirection = "STO";
String currentSelectedVoltage = "Unknown";
int manualLEDBrightness = 0;
bool manualLEDStatus = false;

// --- Variables for Multi-Screen Display ---
byte screenMode = 0;
unsigned long lastScreenButtonPressTime = 0;
const long debounceDelay = 200;

unsigned long lastEffectButtonPressTime = 0;

#define EEPROM_EFFECT_ADDRESS 0

// Variables to fix pot flickering issue
int prevPotValue = -1;
const int POT_TOLERANCE = 5; // Only update brightness if pot value changes by this much

// --- Enum for LED Effect Types ---
enum EffectType {
  EFFECT_ALL_WHITE,
  EFFECT_ALL_BLUE,
  EFFECT_HALF_BLUE_HALF_RED,
  EFFECT_STROBE,
  EFFECT_BACK_AND_FORTH,
  EFFECT_BREATHING, // Renamed for clarity, was FADE_IN_OUT
  EFFECT_RANDOM_COLOR_EVERY_2_LEDS,
  EFFECT_THEATER_CHASE,
  EFFECT_TWINKLE
};

// --- Struct for LED Effect Definition ---
struct LEDEffect {
  EffectType type;
  uint32_t primaryColor;
  uint32_t secondaryColor;
  uint16_t speedDelay;
};

// --- LEDController Class Declaration and Implementation ---
class LEDController {
public:
  LEDController(uint16_t numPixels, uint8_t pin, uint8_t type)
    : _pixels(numPixels, pin, type), _currentBrightness(255) {
    // Initialize the effects array within the constructor
    _effects[0] = {EFFECT_ALL_WHITE, _pixels.Color(255, 255, 255), 0, 0};
    _effects[1] = {EFFECT_ALL_BLUE, _pixels.Color(0, 0, 255), 0, 0};
    _effects[2] = {EFFECT_HALF_BLUE_HALF_RED, _pixels.Color(0, 0, 255), _pixels.Color(255, 0, 0), 100}; // Police light flash speed
    _effects[3] = {EFFECT_STROBE, _pixels.Color(255, 255, 255), 0, 50};
    _effects[4] = {EFFECT_BACK_AND_FORTH, _pixels.Color(0, 255, 0), 0, 75};
    _effects[5] = {EFFECT_BREATHING, _pixels.Color(128, 0, 128), 0, 10};
    _effects[6] = {EFFECT_RANDOM_COLOR_EVERY_2_LEDS, 0, 0, 200};
    _effects[7] = {EFFECT_THEATER_CHASE, _pixels.Color(255, 0, 0), 0, 75}; // New effect: Theater Chase (Knight Rider)
    _effects[8] = {EFFECT_TWINKLE, _pixels.Color(255, 255, 255), 0, 50}; // New effect: Twinkle/Sparkle
    _numEffects = 9;
    _currentEffectIndex = 0;
    _isInitialized = false;
  }

  void begin() {
    _pixels.begin();
    _pixels.show();
    loadEffectIndexFromEEPROM();
    _pixels.setBrightness(_currentBrightness);
    _isInitialized = true;
    Serial.print("Loaded initial effect index: ");
    Serial.println(_currentEffectIndex);
  }

  void runCurrentEffect() {
    if (!_isInitialized) return;
    
    if (_currentEffectIndex >= 0 && _currentEffectIndex < _numEffects) {
      _dispatchEffect(_effects[_currentEffectIndex]);
    } else {
      Serial.println("Invalid effect index. Resetting to 0.");
      _currentEffectIndex = 0;
      _dispatchEffect(_effects[_currentEffectIndex]);
    }
    _pixels.show();
  }

  void nextEffect() {
    _currentEffectIndex = (_currentEffectIndex + 1) % _numEffects;
    _resetEffectState();
    _pixels.clear();
    saveEffectIndexToEEPROM();
  }

  void setBrightness(uint8_t brightness) {
    if (!_isInitialized) return;
    _currentBrightness = brightness;
    _pixels.setBrightness(_currentBrightness);
  }

  int getCurrentEffectIndex() const {
    return _currentEffectIndex;
  }

  void clearLeds() {
    if (!_isInitialized) return;
    _pixels.clear();
    _pixels.show();
  }

  // New public method to set a specific effect index, overriding EEPROM
  void setInitialEffect(int index) {
    _currentEffectIndex = index;
    // We don't save to EEPROM here so that it can be changed later
  }

private:
  Adafruit_NeoPixel _pixels;
  LEDEffect _effects[9];
  int _numEffects;
  int _currentEffectIndex;
  uint8_t _currentBrightness;
  bool _isInitialized;

  // State variables for effects
  unsigned long _prevMillis = 0;
  int _backAndForthIndex = 0;
  int _backAndForthDirection = 1;
  bool _strobeOn = false;
  uint8_t _fadeBrightness = 0;
  int _fadeDirection = 1;
  bool _policeFlashState = false;
  int _theaterChaseIndex = 0;

  void _dispatchEffect(const LEDEffect& effect) {
    switch (effect.type) {
      case EFFECT_ALL_WHITE:
      case EFFECT_ALL_BLUE:
        _showStaticColor(effect);
        break;
      case EFFECT_HALF_BLUE_HALF_RED:
        _runPoliceLights(effect);
        break;
      case EFFECT_STROBE:
        _runStrobe(effect);
        break;
      case EFFECT_BACK_AND_FORTH:
        _runBackAndForth(effect);
        break;
      case EFFECT_BREATHING:
        _runBreathing(effect);
        break;
      case EFFECT_RANDOM_COLOR_EVERY_2_LEDS:
        _runRandomColorEvery2Leds(effect);
        break;
      case EFFECT_THEATER_CHASE:
        _runTheaterChase(effect);
        break;
      case EFFECT_TWINKLE:
        _runTwinkle(effect);
        break;
      default:
        _pixels.clear();
        break;
    }
  }

  void _resetEffectState() {
    _prevMillis = 0;
    _backAndForthIndex = 0;
    _backAndForthDirection = 1;
    _strobeOn = false;
    _fadeBrightness = 0;
    _fadeDirection = 1;
    _policeFlashState = false;
    _theaterChaseIndex = 0;
  }

  void saveEffectIndexToEEPROM() {
    EEPROM.update(EEPROM_EFFECT_ADDRESS, (uint8_t)_currentEffectIndex);
    Serial.print("Saved effect index ");
    Serial.print(_currentEffectIndex);
    Serial.println(" to EEPROM.");
  }

  void loadEffectIndexFromEEPROM() {
    _currentEffectIndex = EEPROM.read(EEPROM_EFFECT_ADDRESS);
    if (_currentEffectIndex < 0 || _currentEffectIndex >= _numEffects) {
      Serial.println("Invalid effect index loaded from EEPROM. Defaulting to 0.");
      _currentEffectIndex = 0;
      saveEffectIndexToEEPROM();
    }
  }

  void _showStaticColor(const LEDEffect& effect) {
    _pixels.fill(effect.primaryColor);
  }

  void _runPoliceLights(const LEDEffect& effect) {
    unsigned long currentMillis = millis();
    if (currentMillis - _prevMillis >= effect.speedDelay) {
      _prevMillis = currentMillis;
      _policeFlashState = !_policeFlashState;
      if (_policeFlashState) {
        for (int i = 0; i < _pixels.numPixels() / 2; i++) {
          _pixels.setPixelColor(i, effect.primaryColor);
        }
        for (int i = _pixels.numPixels() / 2; i < _pixels.numPixels(); i++) {
          _pixels.setPixelColor(i, effect.secondaryColor);
        }
      } else {
        for (int i = 0; i < _pixels.numPixels() / 2; i++) {
          _pixels.setPixelColor(i, effect.secondaryColor);
        }
        for (int i = _pixels.numPixels() / 2; i < _pixels.numPixels(); i++) {
          _pixels.setPixelColor(i, effect.primaryColor);
        }
      }
    }
  }

  void _runBackAndForth(const LEDEffect& effect) {
    unsigned long currentMillis = millis();
    if (currentMillis - _prevMillis >= effect.speedDelay) {
      _prevMillis = currentMillis;
      _pixels.clear();
      _pixels.setPixelColor(_backAndForthIndex, effect.primaryColor);

      _backAndForthIndex += _backAndForthDirection;
      if (_backAndForthIndex >= _pixels.numPixels() || _backAndForthIndex < 0) {
        _backAndForthDirection *= -1;
        _backAndForthIndex += _backAndForthDirection;
      }
    }
  }

  void _runStrobe(const LEDEffect& effect) {
    unsigned long currentMillis = millis();
    if (currentMillis - _prevMillis >= effect.speedDelay) {
      _prevMillis = currentMillis;
      _strobeOn = !_strobeOn;
      if (_strobeOn) {
        _pixels.fill(effect.primaryColor);
      } else {
        _pixels.clear();
      }
    }
  }

  void _runBreathing(const LEDEffect& effect) {
    unsigned long currentMillis = millis();
    if (currentMillis - _prevMillis >= effect.speedDelay) {
      _prevMillis = currentMillis;
      _fadeBrightness += _fadeDirection;

      if (_fadeBrightness == 255) {
        _fadeDirection = -1;
      } else if (_fadeBrightness == 0) {
        _fadeDirection = 1;
      }

      for (int i = 0; i < _pixels.numPixels(); i++) {
        _pixels.setPixelColor(i, _pixels.Color(_fadeBrightness, _fadeBrightness, _fadeBrightness));
      }
    }
  }

  void _runRandomColorEvery2Leds(const LEDEffect& effect) {
    unsigned long currentMillis = millis();
    if (currentMillis - _prevMillis >= effect.speedDelay) {
      _prevMillis = currentMillis;
      randomSeed(analogRead(0));
      for (int i = 0; i < _pixels.numPixels(); i += 2) {
        uint32_t randomColor = _pixels.Color(random(256), random(256), random(256));
        _pixels.setPixelColor(i, randomColor);
        if (i + 1 < _pixels.numPixels()) {
          _pixels.setPixelColor(i + 1, randomColor);
        }
      }
    }
  }

  // --- New Effect Functions ---

  void _runTheaterChase(const LEDEffect& effect) {
    unsigned long currentMillis = millis();
    if (currentMillis - _prevMillis >= effect.speedDelay) {
      _prevMillis = currentMillis;
      _pixels.clear(); // Clear all pixels before drawing the new chase pattern

      for (int i = 0; i < _pixels.numPixels(); i++) {
        if ((i + _theaterChaseIndex) % 3 == 0) {
          _pixels.setPixelColor(i, effect.primaryColor);
        }
      }

      _theaterChaseIndex++;
      if (_theaterChaseIndex > 2) {
        _theaterChaseIndex = 0;
      }
    }
  }

  void _runTwinkle(const LEDEffect& effect) {
    unsigned long currentMillis = millis();
    if (currentMillis - _prevMillis >= effect.speedDelay) {
      _prevMillis = currentMillis;
      // Fade all pixels slightly to create a trail effect
      for(int i=0; i<_pixels.numPixels(); i++) {
        uint32_t color = _pixels.getPixelColor(i);
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        _pixels.setPixelColor(i, _pixels.Color(r*0.9, g*0.9, b*0.9));
      }
      // Randomly turn on a few new pixels
      randomSeed(analogRead(0));
      if (random(10) < 3) { // Adjust this value to change the density of the twinkles
        int pixel = random(_pixels.numPixels());
        _pixels.setPixelColor(pixel, _pixels.Color(random(256), random(256), random(256)));
      }
    }
  }
};

// Global instance of the LEDController class.
LEDController ledController(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// --- Function Prototypes ---
float readVoltage(int adcPin);
void updateLCD();

unsigned long startMillis;

void setup() {
  Serial.begin(115200);

  pinMode(MEGAPHONE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(EFFECT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_MAN_TOGGLE_PIN, INPUT_PULLUP);

  strip_rev.begin();
  strip_rev.clear();
  strip_rev.show();

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Booting...");
  lcd.setCursor(0, 1);
  lcd.print("Nano Ready!");

  startMillis = millis();

  delay(2000);

  ledController.setInitialEffect(0);
  ledController.begin();
  Serial.println("NeoPixel LED Effects Demo (Single File)");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // --- 1. Read Motor Voltage & Control 4-LED Strip (Reverse) ---
  float voltage1 = readVoltage(MOTOR_LEAD1_ADC_PIN);
  float voltage2 = readVoltage(MOTOR_LEAD2_ADC_PIN);

  float absMotorVoltage = 0.0;
  currentMotorDirection = "STO";

  const float VOLTAGE_THRESHOLD = 3.0;
  if (voltage1 > VOLTAGE_THRESHOLD && voltage2 < VOLTAGE_THRESHOLD) {
    absMotorVoltage = voltage1;
    currentMotorDirection = "FOR";
    strip_rev.clear();
  } else if (voltage2 > VOLTAGE_THRESHOLD && voltage1 < VOLTAGE_THRESHOLD) {
    absMotorVoltage = voltage2;
    currentMotorDirection = "REV";
    for (int i = 0; i < NUM_LEDS_REV; i++) {
      strip_rev.setPixelColor(i, strip_rev.Color(255, 255, 255));
    }
  } else {
    absMotorVoltage = (voltage1 + voltage2) / 2.0;
    currentMotorDirection = "STO";
    strip_rev.clear();
  }
  strip_rev.show();
  currentMotorVoltage = absMotorVoltage;

  // --- 2. Indicate Selected Voltage (from switch output) ---
  float selectedVoltage = readVoltage(SELECTED_VOLTAGE_ADC_PIN);
  if (selectedVoltage > 18.0) {
    currentSelectedVoltage = "24V";
  } else if (selectedVoltage > 8.0) {
    currentSelectedVoltage = "12V";
  } else {
    currentSelectedVoltage = "Unknown";
  }

  // --- 3. Control 14-LED Strip (Manual) ---
  int manualLEDSwitchState = digitalRead(LED_MAN_TOGGLE_PIN);
  int potValue = analogRead(BRIGHTNESS_POT_PIN);
  
  if (abs(potValue - prevPotValue) > POT_TOLERANCE) {
    manualLEDBrightness = map(potValue, 0, ADC_MAX_READING, 0, 255);
    ledController.setBrightness(manualLEDBrightness);
    prevPotValue = potValue;
  }

  if (manualLEDSwitchState == LOW) {
    manualLEDStatus = true;
    ledController.runCurrentEffect();
  } else {
    manualLEDStatus = false;
    ledController.clearLeds();
  }

  // --- Megaphone Button Logic for Screen Cycling ---
  int screenButtonState = digitalRead(MEGAPHONE_BUTTON_PIN);
  if (screenButtonState == LOW) {
    if (currentMillis - lastScreenButtonPressTime > debounceDelay) {
      screenMode++;
      if (screenMode > 3) {
        screenMode = 0;
      }
      lastScreenButtonPressTime = currentMillis;
    }
  }

  // --- LED Effect Cycle Button Logic ---
  int effectButtonState = digitalRead(EFFECT_BUTTON_PIN);
  if (effectButtonState == LOW) {
    if (currentMillis - lastEffectButtonPressTime > debounceDelay) {
      ledController.nextEffect();
      lastEffectButtonPressTime = currentMillis;
    }
  }

  // --- LCD Display Logic ---
  updateLCD();

  delay(10);
}

void updateLCD() {
  static byte prevScreenMode = 255; // Initial value to force a redraw on first loop
  static int prevBrightnessPercent = -1;
  static int prevEffectIndex = -1;
  static unsigned long lastUptimeUpdate = 0;

  unsigned long currentMillis = millis();

  // If the screen mode changes, force a complete redraw
  if (screenMode != prevScreenMode) {
    lcd.clear();
    prevScreenMode = screenMode;
    // Also reset dynamic variables to force a full content refresh
    prevBrightnessPercent = -1;
    prevEffectIndex = -1;
  }

  // Update the content based on the current screen mode
  switch (screenMode) {
    case 0: // Motor screen
      // Always redraw this screen to prevent shifting issues
      lcd.setCursor(0, 0);
      lcd.print("Motor: ");
      lcd.setCursor(7, 0);
      if (currentMotorDirection == "STO") {
        lcd.print("STOPPED   ");
      } else if (currentMotorVoltage > 18.0) {
        lcd.print("HIGH      ");
      } else if (currentMotorVoltage > 8.0) {
        lcd.print("LOW       ");
      } else {
        lcd.print("Unknown   ");
      }

      lcd.setCursor(0, 1);
      lcd.print("Volt: ");
      lcd.setCursor(6, 1);
      lcd.print(currentMotorVoltage, 1);
      lcd.print("V ");
      lcd.setCursor(12, 1);
      lcd.print(currentMotorDirection);
      if (manualLEDStatus) {
        lcd.print(" ON");
      } else {
        lcd.print("   ");
      }
      break;

    case 1: // Input Voltage screen
      // This screen is mostly static, but we'll redraw it completely for consistency.
      lcd.setCursor(0, 0);
      lcd.print("Input Voltage:  ");
      lcd.setCursor(0, 1);
      lcd.print("    ");
      lcd.print(currentSelectedVoltage);
      lcd.print("    ");
      break;

    case 2: { // LED Effects screen
      // Update this screen only if content changes or it's the first time on this screen.
      int brightnessPercent = map(analogRead(BRIGHTNESS_POT_PIN), 0, ADC_MAX_READING, 0, 100);
      int currentEffectIndex = ledController.getCurrentEffectIndex();

      if (brightnessPercent != prevBrightnessPercent || currentEffectIndex != prevEffectIndex) {
        lcd.clear(); // Clear before redrawing to prevent old text artifacts
        lcd.setCursor(0, 0);
        lcd.print("LED Brightness: ");
        lcd.setCursor(0, 1);

        lcd.print(brightnessPercent);
        lcd.print("% ");

        String lcdEffectNames[] = { "All White", "All Blue", "Police", "Strobe", "Back/Forth", "Breathing", "Random", "Theater", "Twinkle" };
        if (currentEffectIndex >= 0 && currentEffectIndex < 9) {
          lcd.print(lcdEffectNames[currentEffectIndex]);
        } else {
          lcd.print("Error");
        }

        prevBrightnessPercent = brightnessPercent;
        prevEffectIndex = currentEffectIndex;
      }
      break;
    }

    case 3: // Uptime screen
      // Only update once per second to prevent flickering.
      if (currentMillis - lastUptimeUpdate >= 1000) {
        lcd.setCursor(0, 0);
        lcd.print("System Uptime:  ");

        lcd.setCursor(0, 1);
        unsigned long uptime = (currentMillis - startMillis) / 1000;
        unsigned long hours = uptime / 3600;
        unsigned long minutes = (uptime % 3600) / 60;
        unsigned long seconds = uptime % 60;

        if (hours < 10) lcd.print("0");
        lcd.print(hours);
        lcd.print(":");
        if (minutes < 10) lcd.print("0");
        lcd.print(minutes);
        lcd.print(":");
        if (seconds < 10) lcd.print("0");
        lcd.print(seconds);
        lcd.print("      "); // Add spaces to clear any old numbers

        lastUptimeUpdate = currentMillis;
      }
      break;
  }
}

float readVoltage(int adcPin) {
  int rawADC = analogRead(adcPin);
  float voltageAtADCPin = rawADC * (ADC_REFERENCE_VOLTAGE / ADC_MAX_READING);
  float actualVoltage = voltageAtADCPin * (VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2) / VOLTAGE_DIVIDER_R2;
  return actualVoltage;
}
