#include <Arduino.h>
#include <EEPROM.h>
#include <Matter.h>
#include <MatterWindowCovering.h>

// Timing & configuration constants
const unsigned int STEP_DELAY_CALIBRATION = 500;  // µs pulse for calibration steps
const unsigned int STEP_DELAY_MATTER = 1000;        // µs pulse for Matter mode steps
const unsigned long MOTOR_DISABLE_INTERVAL = 1000;  // ms idle before disabling motor
const unsigned long AUTO_SAVE_INTERVAL = 300000;    // ms idle before auto-saving (5 min)
const unsigned long HOLD_TIMER_THRESHOLD = 3000;    // ms hold time to store boundary
const uint8_t STEPS_PER_PRESS = 5;                  // Steps per button press (calibration)

// Debounce constants
static const uint16_t DEBOUNCE_MS = 70;
#define BUTTON_PRESS_WINDOW_MS 300

// Global debounce arrays
unsigned long lastChangeTime[20] = {0};
bool lastRaw[20] = {false};
bool stable[20]  = {false};

// Pin definitions
#define MOTOR_ENABLE_PIN   D0
#define STEP_PIN           D1
#define DIRECTION_PIN      D2
#define MOVE_UP_BUTTON     D7
#define MOVE_DOWN_BUTTON   D8
#define MODE_SWITCH_PIN    D10
#define LED_RED            D5
#define LED_GREEN          D6
#define SLEEP_PIN          D9

// Matter and EEPROM constants
MatterWindowCovering matter_blinds;
#define EEPROM_MIN_POS_ADDR   0
#define EEPROM_MAX_POS_ADDR   2
#define EEPROM_CURR_POS_ADDR  4
#define EEPROM_FLAG_ADDR      6
#define EEPROM_FLAG_VALUE     0xABCD
#define DEFAULT_MIN_POS  0
#define DEFAULT_MAX_POS  1000

// Global variables for positions and state
uint16_t minPos = 0;
uint16_t maxPos = 0;
uint16_t currentPos = 0;  // Raw motor steps
uint16_t lastSaved = 0;
unsigned long lastMoveTime = 0;
bool eepromSaved = false;
unsigned long lastEepromSaveTime = 0;
bool calibratingMin = true;  // true: calibrate MIN next; false: calibrate MAX

// Forward declarations
void initialize_eeprom();
void load_positions_from_eeprom();
void run_calibration_mode();
void run_matter_mode();
bool is_button_pressed(int pin);
bool are_both_pressed();
bool are_both_released();
bool are_both_pressed_delayed();
void wakeDriver();
void disableMotor();
void sleepDriver();
void move_stepper_single_step(bool moveUp, bool ignoreLimits, unsigned int stepDelay);
void move_stepper_single_step(bool moveUp, bool ignoreLimits);
void move_stepper_to_position(uint16_t newPos);
void updateLEDs();
void updateSleepAndAutoSave();
void flash_led(int times);
void flash_both_leds(int times);

void setup() {
  Serial.begin(115200);
  Serial.println("Setup starting");

  // Disable motor and put driver to sleep initially
  pinMode(MOTOR_ENABLE_PIN, OUTPUT);
  disableMotor();
  pinMode(SLEEP_PIN, OUTPUT);
  sleepDriver();

  // Configure LED pins; turn RED on until commissioned
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, LOW);

  // Optional onboard LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Set up buttons and mode switch
  pinMode(MOVE_UP_BUTTON, INPUT_PULLUP);
  pinMode(MOVE_DOWN_BUTTON, INPUT_PULLUP);
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);

  // Initialize Matter
  Matter.begin();
  matter_blinds.begin();
  Serial.println("Matter window covering");

  // Commissioning process
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Matter not commissioned");
    Serial.printf("Manual code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR code: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }
  while (!Matter.isDeviceCommissioned()) { delay(200); }
  while (!Matter.isDeviceThreadConnected()) { delay(200); }
  while (!matter_blinds.is_online()) { delay(200); }
  Serial.println("Device is online");

  // Turn off RED LED once commissioned
  digitalWrite(LED_RED, LOW);

  // Load EEPROM values
  initialize_eeprom();
  load_positions_from_eeprom();
  lastSaved = currentPos;

  // Send initial position to Matter
  uint16_t initPercent = map(currentPos, minPos, maxPos, 0, 100);
  matter_blinds.set_actual_lift_position_percent(initPercent);
  matter_blinds.set_actual_lift_position_raw(currentPos);

  Serial.printf("Initial: minPos=%u, maxPos=%u, currentPos=%u\n", minPos, maxPos, currentPos);
  Serial.println("Setup complete");
}

void loop() {
  updateLEDs();
  updateSleepAndAutoSave();

  // Mode switch: HIGH = calibration, LOW = Matter mode
  if (digitalRead(MODE_SWITCH_PIN) == HIGH) {
    run_calibration_mode();
  } else {
    run_matter_mode();
  }
}

// Wake driver: set SLEEP_PIN HIGH and enable motor (active LOW)
void wakeDriver() {
  digitalWrite(SLEEP_PIN, HIGH);
  digitalWrite(MOTOR_ENABLE_PIN, LOW);
  Serial.println("WakeDriver: SLEEP=HIGH, ENABLE=LOW");
}

// Disable motor (active LOW: HIGH disables)
void disableMotor() {
  digitalWrite(MOTOR_ENABLE_PIN, HIGH);
  Serial.println("Motor disabled");
}

// Put driver to sleep (set SLEEP_PIN LOW)
void sleepDriver() {
  digitalWrite(SLEEP_PIN, LOW);
  Serial.println("Driver asleep");
}

// Moves one step with a specified stepDelay and updates Matter
void move_stepper_single_step(bool moveUp, bool ignoreLimits, unsigned int stepDelay) {
  if (!ignoreLimits) {
    if (moveUp && currentPos >= maxPos) return;
    if (!moveUp && currentPos <= minPos) return;
  }
  wakeDriver();
  digitalWrite(DIRECTION_PIN, (moveUp ? HIGH : LOW));
  delayMicroseconds(2);
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(stepDelay);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(stepDelay);

  if (moveUp) currentPos++;
  else        currentPos--;

  lastMoveTime = millis();
  matter_blinds.set_actual_lift_position_raw(currentPos);
  uint16_t percent = map(currentPos, minPos, maxPos, 0, 100);
  matter_blinds.set_actual_lift_position_percent(percent);

  Serial.printf("Step %s: currentPos=%u (%u%%)\n", (moveUp ? "UP" : "DOWN"), currentPos, percent);
}

// Overloaded version that uses the default calibration delay
void move_stepper_single_step(bool moveUp, bool ignoreLimits) {
  move_stepper_single_step(moveUp, ignoreLimits, STEP_DELAY_CALIBRATION);
}

// Matter mode: move one step at a time until currentPos equals newPos
void move_stepper_to_position(uint16_t newPos) {
  wakeDriver();
  int dir = (currentPos < newPos) ? 1 : -1;
  digitalWrite(DIRECTION_PIN, (dir > 0 ? HIGH : LOW));
  Serial.printf("Matter move: from %u to %u\n", currentPos, newPos);

  while (currentPos != newPos) {
    digitalWrite(MOTOR_ENABLE_PIN, LOW);
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(STEP_DELAY_MATTER);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(STEP_DELAY_MATTER);
    currentPos += dir;
    lastMoveTime = millis();
    matter_blinds.set_actual_lift_position_raw(currentPos);
    Serial.printf("Matter step: currentPos=%u\n", currentPos);
  }
  Serial.printf("Matter reached: currentPos=%u (%d%%)\n", currentPos, map(currentPos, minPos, maxPos, 0, 100));
}

// Calibration mode: move 5 steps per button press and update Matter each step
void run_calibration_mode() {
  if (digitalRead(MODE_SWITCH_PIN) == LOW) {
    Serial.println("Exiting calibration mode");
    return;
  }
  static unsigned long holdTimer = 0;

  if (calibratingMin) {
    if (is_button_pressed(MOVE_UP_BUTTON) && !is_button_pressed(MOVE_DOWN_BUTTON)) {
      Serial.println("Calibration MIN: UP => 5 steps up");
      for (int i = 0; i < STEPS_PER_PRESS; i++) {
        move_stepper_single_step(true, true, STEP_DELAY_CALIBRATION);
      }
    }
    else if (is_button_pressed(MOVE_DOWN_BUTTON) && !is_button_pressed(MOVE_UP_BUTTON)) {
      Serial.println("Calibration MIN: DOWN => 5 steps down");
      for (int i = 0; i < STEPS_PER_PRESS; i++) {
        move_stepper_single_step(false, true, STEP_DELAY_CALIBRATION);
      }
    }
  } else {
    if (is_button_pressed(MOVE_UP_BUTTON) && !is_button_pressed(MOVE_DOWN_BUTTON)) {
      Serial.println("Calibration MAX: UP => 5 steps up");
      for (int i = 0; i < STEPS_PER_PRESS; i++) {
        move_stepper_single_step(true, true, STEP_DELAY_CALIBRATION);
      }
    }
    else if (is_button_pressed(MOVE_DOWN_BUTTON) && !is_button_pressed(MOVE_UP_BUTTON)) {
      Serial.println("Calibration MAX: DOWN => 5 steps down");
      for (int i = 0; i < STEPS_PER_PRESS; i++) {
        move_stepper_single_step(false, true, STEP_DELAY_CALIBRATION);
      }
    }
  }

  // Both buttons held for HOLD_TIMER_THRESHOLD => store boundary
  if (are_both_pressed()) {
    if (holdTimer == 0) {
      holdTimer = millis();
      Serial.println("Both buttons pressed, hold timer started");
    }
    if (millis() - holdTimer >= HOLD_TIMER_THRESHOLD) {
      if (calibratingMin) {
        minPos = currentPos;
        EEPROM.put(EEPROM_MIN_POS_ADDR, minPos);
        Serial.printf("Saved MIN boundary: %u\n", minPos);
        flash_both_leds(4);
        calibratingMin = false;
      } else {
        if (currentPos < minPos) {
          uint16_t tmp = minPos;
          minPos = currentPos;
          maxPos = tmp;
          Serial.printf("Swapped boundaries: new MIN=%u, new MAX=%u\n", minPos, maxPos);
        } else {
          maxPos = currentPos;
          Serial.printf("Saved MAX boundary: %u\n", maxPos);
        }
        EEPROM.put(EEPROM_MIN_POS_ADDR, minPos);
        EEPROM.put(EEPROM_MAX_POS_ADDR, maxPos);
        EEPROM.put(EEPROM_CURR_POS_ADDR, currentPos);
        flash_both_leds(3);
        calibratingMin = true;
      }
      holdTimer = 0;
    }
  } else {
    holdTimer = 0;
  }
}

// Matter mode: move one step at a time until target reached
void run_matter_mode() {
  static uint16_t lastReqLiftRaw = 0;
  uint16_t reqLiftRaw = matter_blinds.get_requested_lift_position_raw();
  if (reqLiftRaw == lastReqLiftRaw) return;

  int32_t reqPercent = matter_blinds.get_requested_lift_position_percent();
  uint16_t targetRaw = map(reqPercent, 0, 100, minPos, maxPos);
  Serial.printf("Matter command: %ld%%, target raw=%u\n", reqPercent, targetRaw);

  if (targetRaw > currentPos) {
    matter_blinds.set_current_operation(MatterWindowCovering::WINDOW_COVERING_OPENING);
    Serial.println("Matter mode: Opening");
  } else {
    matter_blinds.set_current_operation(MatterWindowCovering::WINDOW_COVERING_CLOSING);
    Serial.println("Matter mode: Closing");
  }

  digitalWrite(LED_RED, HIGH);
  move_stepper_to_position(targetRaw);
  digitalWrite(LED_RED, LOW);

  matter_blinds.set_actual_lift_position_raw(reqLiftRaw);
  matter_blinds.set_current_operation(MatterWindowCovering::WINDOW_COVERING_STOPPED);
  Serial.printf("Matter reached: currentPos=%u (%d%%)\n",
                currentPos, map(currentPos, minPos, maxPos, 0, 100));
  lastReqLiftRaw = reqLiftRaw;
}

// Update LED states based on mode
void updateLEDs() {
  if (digitalRead(MODE_SWITCH_PIN) == HIGH) {
    if (calibratingMin) {
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_RED, LOW);
    } else {
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_RED, HIGH);
    }
  } else {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, LOW);
  }
}

// Update sleep and auto-save: disable motor if idle and sleep driver after long idle
void updateSleepAndAutoSave() {
  static bool motorDisabled = false;
  if (millis() - lastMoveTime >= MOTOR_DISABLE_INTERVAL) {
    if (!motorDisabled) {
      disableMotor();
      motorDisabled = true;
    }
  } else {
    motorDisabled = false;
  }
  if (millis() - lastMoveTime >= AUTO_SAVE_INTERVAL) {
    if (!eepromSaved && (millis() - lastEepromSaveTime >= AUTO_SAVE_INTERVAL)) {
      if (lastSaved != currentPos) {
        EEPROM.put(EEPROM_CURR_POS_ADDR, currentPos);
        Serial.printf("Auto-saved: currentPos=%u\n", currentPos);
        lastSaved = currentPos;
        eepromSaved = true;
        lastEepromSaveTime = millis();
      }
    }
    sleepDriver();
  } else {
    digitalWrite(SLEEP_PIN, HIGH);
  }
}

// LED flash functions
void flash_led(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_GREEN, HIGH);
    delay(200);
    digitalWrite(LED_GREEN, LOW);
    delay(200);
  }
}

void flash_both_leds(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, HIGH);
    delay(200);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, LOW);
    delay(200);
  }
}

// EEPROM functions
void initialize_eeprom() {
  uint16_t flagVal;
  EEPROM.get(EEPROM_FLAG_ADDR, flagVal);
  if (flagVal != EEPROM_FLAG_VALUE) {
    EEPROM.put(EEPROM_MIN_POS_ADDR, (uint16_t)DEFAULT_MIN_POS);
    EEPROM.put(EEPROM_MAX_POS_ADDR, (uint16_t)DEFAULT_MAX_POS);
    uint16_t mid = (DEFAULT_MIN_POS + DEFAULT_MAX_POS) / 2;
    EEPROM.put(EEPROM_CURR_POS_ADDR, mid);
    EEPROM.put(EEPROM_FLAG_ADDR, (uint16_t)EEPROM_FLAG_VALUE);
    Serial.println("EEPROM not inited, wrote defaults");
  }
}

void load_positions_from_eeprom() {
  EEPROM.get(EEPROM_MIN_POS_ADDR, minPos);
  EEPROM.get(EEPROM_MAX_POS_ADDR, maxPos);
  EEPROM.get(EEPROM_CURR_POS_ADDR, currentPos);
  Serial.printf("EEPROM loaded: minPos=%u, maxPos=%u, currentPos=%u\n", minPos, maxPos, currentPos);
}

// Debounce functions
bool is_button_pressed(int pin) {
  // If in Matter mode (MODE_SWITCH_PIN LOW), ignore button input.
  if (digitalRead(MODE_SWITCH_PIN) == LOW) return false;

  bool raw = (digitalRead(pin) == LOW);
  unsigned long now = millis();
  if (raw != lastRaw[pin]) {
    lastRaw[pin] = raw;
    lastChangeTime[pin] = now;
  }
  if ((now - lastChangeTime[pin]) > DEBOUNCE_MS) {
    stable[pin] = raw;
  }
  return stable[pin];
}


bool are_both_pressed() {
  return (is_button_pressed(MOVE_UP_BUTTON) && is_button_pressed(MOVE_DOWN_BUTTON));
}

bool are_both_released() {
  return (!is_button_pressed(MOVE_UP_BUTTON) && !is_button_pressed(MOVE_DOWN_BUTTON));
}

bool are_both_pressed_delayed() {
  unsigned long start = millis();
  while (millis() - start < BUTTON_PRESS_WINDOW_MS) {
    if (are_both_pressed()) return true;
  }
  return false;
}
