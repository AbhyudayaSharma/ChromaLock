#include <LiquidCrystal.h>

// uncomment the line below to run tests
// #define TESTING

#ifndef TESTING
#include <Servo.h>
#else // Mock for Servo motor
class Servo {
public:
  int pin;
  int value;

  void attach(int pin) {
    this->pin = pin;
  }

  void write(int value) {
    this->value = value;
  }
};
#endif

Servo myservo;
constexpr int maxLength = 10;
int passcodeLength = 4;
int enteredPasscodeLength = 0;
const int CLOCKFREQUENCY = 4 * 1000 * 1000;
volatile int timerInterruptCount = 0;
const int redLedPin = 0;
const int greenLedPin = A3;
const int blueLedPin = A4;

// entered passcode
char enteredPasscode[maxLength + 1] = {
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

// current passcode
char currentPasscode[maxLength + 1] = {
  '0',
  '0',
  '0',
  '0',
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

// button status -- high or low
int buttonStatuses[] = {
  LOW,  // 0
  LOW,  // 1
  LOW,  // 2
  LOW,  // 3
  LOW,  // 4
  LOW,  // 5
  LOW,  // 6
  LOW,  // 7
  LOW,  // 8
  LOW,  // 9
  LOW,  // undo button --> if locked: undo; if unlocked: reset combination; if in reset mode: submits the new combination
  LOW,  // lock/unlock button --> if locked: submit; if unlocked: lock using old passcode -- A6
};

// Button pressed status -- true only when a button was released
boolean buttonPressed[] = {
  false,
  false,
  false,
  false,
  false,
  false,
  false,
  false,
  false,
  false,
  false,
  false,
};

boolean lockedState = true;

// States for the FSM
enum State {
  Init,
  Locked,
  WaitForButton,
  Unlocked,
  ResetPasscode,
};

const int autoLockLimit = 30000;
const int autoResetLimit = 30000;
int lastUnlockedTime = millis();
int lastResetTime = millis();

constexpr int button0Pin = 0;
constexpr int button1Pin = 1;
constexpr int button2Pin = 2;
constexpr int button3Pin = 3;
constexpr int button4Pin = 4;
constexpr int button5Pin = 5;
constexpr int button6Pin = 6;
constexpr int button7Pin = 7;
constexpr int button8Pin = 8;
constexpr int button9Pin = 9;
constexpr int buttonLockUnlockPin = 10;
constexpr int buttonUndoButtonPin = 11;

constexpr int rs = A0, en = A1, d4 = A2, d5 = 12, d6 = 13, d7 = 14;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

const int numTests = 20;

/*
 * A struct to keep all three state inputs in one place
 */
typedef struct {
  bool buttonPressed[12];
  int buttonStatuses[12];
} state_inputs;

/*
 * A struct to keep all 9 state variables in one place
 */
typedef struct {
  int passcodeLength;
  int enteredPasscodeLength;
  int timerInterruptCount;
  char currentPasscode[maxLength + 1];
  char enteredPasscode[maxLength + 1];
} state_vars;

// This function setups the TC3 timer
// Inputs: none
// Outputs: none
// Side effects: setups the TC3 timer
void setupTimer() {
#ifndef TESTING
  GCLK->GENDIV.reg = GCLK_GENDIV_DIV(0) | GCLK_GENDIV_ID(4);  // do not divide gclk 4

  while (GCLK->STATUS.bit.SYNCBUSY)
    ;

  // use GCLK->GENCTRL.reg and GCLK->CLKCTRL.reg
  GCLK->GENCTRL.reg = GCLK_GENCTRL_GENEN | GCLK_GENCTRL_ID(4) | GCLK_GENCTRL_IDC | GCLK_GENCTRL_SRC(6);
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN(4) | GCLK_CLKCTRL_ID_TCC2_TC3;
  while (GCLK->STATUS.bit.SYNCBUSY)
    ;  // write-synchronized

  TC3->COUNT16.CTRLA.reg &= ~(TC_CTRLA_ENABLE);
  TC3->COUNT16.INTENCLR.reg |= TC_INTENCLR_MC0;
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
    ;  // write-synchronized

  NVIC_SetPriority(TC3_IRQn, 0);
  NVIC_EnableIRQ(TC3_IRQn);
#endif
}

// This function setups the Watchdog timer.
// Inputs: none
// Outputs: none
// Side effects: setups the watchdog timer
void setupWatchdogTimer() {
#ifndef TESTING
  // Clear and enable WDT
  NVIC_DisableIRQ(WDT_IRQn);
  NVIC_ClearPendingIRQ(WDT_IRQn);
  NVIC_SetPriority(WDT_IRQn, 0);
  NVIC_EnableIRQ(WDT_IRQn);

  // Configure and enable WDT GCLK:
  GCLK->GENDIV.reg = GCLK_GENDIV_DIV(4) | GCLK_GENDIV_ID(5);
  while (GCLK->STATUS.bit.SYNCBUSY)
    ;
  // set GCLK->GENCTRL.reg and GCLK->CLKCTRL.reg
  GCLK->GENCTRL.reg = GCLK_GENCTRL_DIVSEL | GCLK_GENCTRL_GENEN | GCLK_GENCTRL_ID(0x5) | GCLK_GENCTRL_SRC(3);
  while (GCLK->STATUS.bit.SYNCBUSY)
    ;

  WDT->CONFIG.reg = WDT_CONFIG_PER(0xA);
  while (WDT->STATUS.bit.SYNCBUSY)
    ;

  WDT->EWCTRL.reg = WDT_EWCTRL_EWOFFSET(0x9);

  WDT->CTRL.reg = WDT_CTRL_ENABLE;
  while (WDT->STATUS.bit.SYNCBUSY)
    ;

  WDT->INTENSET.reg = WDT_INTENSET_EW;
#endif
}

#ifdef TESTING
// Runs tests
// Inputs: none
// Outputs: none
// Side effects: none
const State testStatesIn[numTests] = { (State)0, (State)1, (State)2, (State)2, (State)2, (State)2, (State)2, (State)2, (State)2, (State)3, (State)3, (State)3, (State)3, (State)3, (State)4, (State)4, (State)4, (State)4, (State)4, (State)4 };
const State testStatesOut[numTests] = { (State)1, (State)2, (State)2, (State)1, (State)2, (State)2, (State)2, (State)2, (State)3, (State)3, (State)1, (State)1, (State)3, (State)4, (State)4, (State)3, (State)3, (State)4, (State)4, (State)1 };
const state_inputs testInputs[numTests] = {
  { { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
};

const state_vars testVarsIn[numTests] = {
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '1', '2', '3', 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 3, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '0', '0', '0', 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '0', 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 1, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '0', 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 2, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '0', '0', 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 4, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 4, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 3, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 3, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 3, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 4, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 3, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 2, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 2, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 1, 2, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '1', 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
};

const state_vars testVarsOut[numTests] = {
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '1', '2', '3', 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 1, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '0', 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 1, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '0', 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 3, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 0, 0, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 4, 1, 2, { '0', '0', '0', '0', 0, 0, 0, 0, 0, 0 }, { '1', 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
  { 1, 0, 0, { '1', 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
};

void runTests() {
  bool allTestsPassed = true;
  for (int i = 0; i < numTests; i++) {
    Serial.print("Running test ");
    Serial.println(i);
    if (!testTransition(testStatesIn[i], testStatesOut[i], testInputs[i], testVarsIn[i], testVarsOut[i], false)) {
      allTestsPassed = false;
      // break;
    }
    Serial.println();
  }
  if (allTestsPassed) {
    Serial.println("All tests passed!");
  } else {
    Serial.println("Uh oh!");
  }
}
#endif

// This function sets every pin to the correct mode, setups Watchdog and TC3 timers,
// servo motor, and the LCD display.
// Inputs: none
// Outputs: none
// Side effects: sets up pin modes, lcd display and timers
void setup() {
  // put your setup code here, to run once:
  pinMode(0, OUTPUT);
  pinMode(1, INPUT);
  pinMode(2, INPUT);
  pinMode(3, INPUT);
  pinMode(4, INPUT);
  pinMode(5, INPUT);
  pinMode(6, INPUT);
  pinMode(7, INPUT);
  pinMode(8, INPUT);
  pinMode(9, INPUT);
  pinMode(10, INPUT);
  pinMode(A6, INPUT);
  pinMode(A5, INPUT);
  pinMode(A3, OUTPUT);
  pinMode(A4, OUTPUT);

  myservo.attach(11);
  Serial.begin(9600);
  while (!Serial)
    ;

  lcd.begin(16, 2);

  setupTimer();
  setupWatchdogTimer();

#ifdef TESTING
  runTests();
#endif
}

// This function is the ISR for the TC3 timer
// Inputs: none
// Outputs: timerInterruptCount is incremented by one
// Side effects: intflag is reset
void TC3_Handler() {
#ifndef TESTING
  // Clear interrupt register flag
  // (use register TC3->COUNT16.registername.reg)
  TC3->COUNT16.INTFLAG.reg |= TC_INTFLAG_MC0;
  Serial.println("Timer interrupt");
  timerInterruptCount++;
#endif
}

// This function reads button statuses using digitalRead and updates buttonStatuses.
// Inputs: button statuses (high or low)
// Outputs: none
// Side effects: buttonStatuses is updated with values read using digitalRead
void updateInputs() {
  int newStatuses[12] = {
    digitalRead(A5),
    digitalRead(1),
    digitalRead(2),
    digitalRead(3),
    digitalRead(4),
    digitalRead(5),
    digitalRead(6),
    digitalRead(7),
    digitalRead(8),
    digitalRead(9),
    digitalRead(10),
    digitalRead(A6),
  };

  for (int i = 0; i <= 11; i++) {
    int newStatus = newStatuses[i];
    buttonPressed[i] = false;

    if (newStatus != buttonStatuses[i]) {
      buttonStatuses[i] = newStatus;
      Serial.print("Button ");
      Serial.print(i);
      Serial.println(newStatus == HIGH ? " pressed" : " released");
      buttonPressed[i] = newStatus == LOW;
    }
  }
}

// This function sets the watchdog.
// Inputs: none
// Outputs: none
// Side effects: the watchdog is petted
void petWatchdog() {
#ifndef TESTING
  WDT->CLEAR.reg = WDT_CLEAR_CLEAR(0xA5);
#endif
}

// This passcode shows the initial passcode.
// Inputs: currentPasscode
// Outputs: none
// Side effects: display is set to show the initial passcode
void displayInitPasscode() {
  Serial.print("Initial passcode: ");
  Serial.println((const char*)currentPasscode);

  lcd.setCursor(0, 0);
  lcd.print("INITIAL PASSCODE");
  lcd.setCursor(0, 1);
  lcd.print((const char*)currentPasscode);

  for (int i = 0; i < 10; i++) {
    delay(50);
    petWatchdog();
  }
}

// This function displays that the lock was auto-locked.
// Inputs: none
// Outputs: none
// Side effects: display is set to show that the lock was auto-locked.
void displayAutoLocked() {
  Serial.println("AUTOLOCKING TIMEOUT");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AUTO-LOCKING...");
}

// This function displays that the lock was auto-reset.
// Inputs: none
// Outputs: none
// Side effects: display is set to show that the lock was auto-reset.
void displayAutoReset() {
  Serial.println("AUTORESET TIMEOUT");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AUTO-RESETTING...");
}

// This function display that the lock is locked.
// Inputs: none
// Outputs: none
// Side effects: display is set to show that the lock is locked.
void displayLocked() {
  Serial.println("LOCKED");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LOCKED!");
}

// This function displays that the lock is unlocked.
// Inputs: none
// Outputs: none
// Side effects: display is set to show that the lock is unlocked.
void displayUnlocked() {
  Serial.println("UNLOCKED");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UNLOCKED!");
}

// This function displays that the user has entered a wrong passcode.
// Inputs: none
// Outputs: none
// Side effects: display is set to show that the user entered a wrong passcode.
void displayWrongPasscode() {
  Serial.println("WRONG PASSCODE");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WRONG PASSCODE!");

  for (int i = 0; i < 10; i++) {
    delay(100);
    petWatchdog();
  }
}

// This function displays the passcode users are entering in a masked manner
// so that onlookers cannot see what password they entered.
// Inputs: none
// Outputs: none
// Side effects: display is set to show the masked passcode.
void displayMaskedPasscode() {
  char* i = enteredPasscode;
  Serial.print("Entered Passcode: ");
  while (*i) {
    Serial.print('*');
    i++;
  }
  Serial.println();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PASSCODE:");
  lcd.setCursor(0, 1);
  char* j = enteredPasscode;
  int counter = 0;
  while (*j) {
    lcd.print("*");
    j++;
    counter++;
  }
}

// This function displays to the user that the passcode was changed.
// Inputs: none
// Outputs: none
// Side effects: display is set to show that the passcode was changed.
void displayPasswordChanged() {
  Serial.print("Password changed!");
  Serial.println((const char*)currentPasscode);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PASSWORD CHANGED");

  for (int i = 0; i < 10; i++) {
    delay(100);
    petWatchdog();
  }
}

// This function displays the new passcode the user is setting
// in plaintext on the LCD display and also to the serial monitor.
// Inputs: none
// Outputs: none
// Side effects: display is set to show the new password being entered.
void displayEnterNewPasscode() {
  Serial.print("New passcode: ");
  Serial.println((const char*)enteredPasscode);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NEW PASSCODE:");

  lcd.setCursor(0, 1);
  char* j = enteredPasscode;
  int counter = 0;
  while (*j) {
    lcd.print(*j);
    j++;
    counter++;
  }
}

// This function dispslays an error message when the user tries to reset
// the passcode to an empty string
// Inputs: none
// Outputs: none
// Side effects: display is set to show that new passcode cannot be empty
void displayNewPasswordEmptyError() {
  Serial.print("New passcode cannot be empty!");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NEW PASSCODE");
  lcd.setCursor(0, 1);
  lcd.print("CANNOT BE EMPTY");
  for (int i = 0; i < 10; i++) {
    delay(100);
    petWatchdog();
  }
}

// This function looks at the button statuses and modifies
// the entered passcode with the buttons the user had pressed.
// Inputs: enteredPasscode, enteredPasscodeLength, buttonPressed
// Outputs: enteredPasscode is updated with button pressed
// Side effects: none
void updateEnteredPassword() {
  // Check if the user can still enter buttons
  if (enteredPasscodeLength < maxLength) {
    // Iterate over all buttons and check if they are pressed
    for (int i = 0; i <= 9; i++) {
      if (buttonPressed[i]) {
        enteredPasscode[enteredPasscodeLength] = i + '0';
        enteredPasscodeLength++;
        break;
      }
    }
  }
}

// This function enables the timer interrupt caused by TC3
// Inputs: timerInterruptCount
// Outputs: timerInterruptCount is set to zero
// Side effects: enables timer interrupts
void enableTimeoutTimer() {
#ifndef TESTING
  timerInterruptCount = 0;
  TC3->COUNT16.INTENCLR.reg |= TC_INTENCLR_MC0;
  TC3->COUNT16.CC[0].reg = 0xffffffff;
  TC3->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN(1) | TC_CTRLA_PRESCALER(7) | TC_CTRLA_PRESCSYNC(1);
  TC3->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
  TC3->COUNT16.INTENSET.reg |= TC_INTENSET_MC0;
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY)
    ;  // write-synchronized
#endif
#ifdef TESTING
  timerInterruptCount = 0;
#endif
}


// This function disables the timer interrupt caused by TC3.
// Inputs: timerInterruptCount
// Outputs: timerInterruptCount is set to zero
// Side effects: interrupts are disabled
void disableTimeoutTimer() {
#ifndef TESTING
  TC3->COUNT16.INTENCLR.reg |= TC_INTENCLR_MC0;
  timerInterruptCount = 0;
#endif
#ifdef TESTING
  timerInterruptCount = 0;
#endif
}

// This function uses PWM to set the LED colour
// Inputs: red, green, and blue values for PWM between 0 and 255
// Outputs: none
// Side effects: LED colour is changed
void setLedColour(int red, int green, int blue) {
  // analogWrite(redLedPin, red);
  analogWrite(blueLedPin, blue);
  analogWrite(greenLedPin, green);
}

// This function is called by loop() to update the FSM state.
// This function uses global variables buttonPressed and buttonStatuses and the old state
// And returns the new state.
// Inputs: oldState, buttonStatuses, buttonPressed, currentPasscode, enteredPasscode, oldState
// Outputs: returns the new state
// Side effects: outputs all functions from the FSM
State updateFSM(State oldState) {
  switch (oldState) {
    case State::Init:
      // Turn Servo Motor to lock it
      setLedColour(0, 0, 255);
      myservo.write(0);
      displayInitPasscode();
      return State::Locked;  // transition 1-2
    case State::Locked:
      // Turn Servo Motor to lock it
      setLedColour(0, 0, 255);
      myservo.write(0);
      lastUnlockedTime = millis();
      lastResetTime = millis();
      displayLocked();
      return State::WaitForButton;  // transition 2-3
    case State::Unlocked:
      setLedColour(0, 255, 0);
      // Turn Servo Motor
      myservo.write(180);
      if (buttonPressed[buttonLockUnlockPin]) {
        // User presses lock button
        // transition 4-2(b)
        lockedState = true;
        displayLocked();
        disableTimeoutTimer();
        return State::Locked;
      } else if (buttonPressed[buttonUndoButtonPin]) {
        // User presses undo --> enters reset state
        // transition 4-5
        lastResetTime = millis();
        disableTimeoutTimer();
        enableTimeoutTimer();
        return State::ResetPasscode;
      } else if (timerInterruptCount >= 4) {
        // Autolock after ~ (8.5*4) = 34 seconds of inactivity
        // transition 4-2(a)
        lockedState = true;
        displayAutoLocked();
        displayLocked();
        disableTimeoutTimer();
        return State::Locked;
      }

      // transition 4-4
      return State::Unlocked;
    case State::ResetPasscode:
      // User can enter digits for the new passcode
      updateEnteredPassword();
      displayEnterNewPasscode();
      setLedColour(0, 128, 255);

      lastUnlockedTime = millis();
      if (buttonPressed[buttonLockUnlockPin]) {
        // User can submit the passcode
        if (enteredPasscodeLength > 0) {  // transition 5-2
          // Allow this to be the new passcode
          // Copy the entered passcode to the current passcode
          strcpy(currentPasscode, enteredPasscode);
          passcodeLength = strlen(currentPasscode);
          memset(enteredPasscode, 0, maxLength + 1);
          enteredPasscodeLength = 0;
          // Reset the entered passcode
          displayPasswordChanged();
          disableTimeoutTimer();
          return State::Locked;
        } else {  // transition 5-5(a)
          displayNewPasswordEmptyError();
          enableTimeoutTimer();
          return State::ResetPasscode;
        }
      } else if (buttonPressed[buttonUndoButtonPin]) {
        // Transition 5-4(b)
        // User can press undo button to exit reset passcode state --> goes to unlocked
        disableTimeoutTimer();
        enteredPasscodeLength = 0;
        memset(enteredPasscode, 0, maxLength + 1);
        lastResetTime = millis();
        displayUnlocked();
        enableTimeoutTimer();
        return State::Unlocked;
      } else if (timerInterruptCount >= 4) {
        // Transition 5-4(a)
        disableTimeoutTimer();
        enteredPasscodeLength = 0;
        memset(enteredPasscode, 0, maxLength + 1);
        displayAutoReset();
        displayUnlocked();
        enableTimeoutTimer();
        return State::Unlocked;
      }

      // transition 5-5(b)
      return State::ResetPasscode;
    case State::WaitForButton:
      updateEnteredPassword();
      if (buttonPressed[buttonLockUnlockPin]) {
        // User submits the combination
        bool correctCombination = !strcmp(currentPasscode, enteredPasscode);
        // Always set the current passcode to 0's
        enteredPasscodeLength = 0;
        memset(enteredPasscode, 0, maxLength + 1);
        // Check if combination is correct
        if (correctCombination) {
          // Transition 3-4
          lockedState = false;
          lastUnlockedTime = millis();
          displayUnlocked();
          enableTimeoutTimer();
          myservo.write(180);
          setLedColour(0, 255, 0);
          return State::Unlocked;
        }
        // Incorrect combination
        // Transition 3-2
        displayWrongPasscode();
        setLedColour(0, 0, 255);
        return State::Locked;
      } else if (buttonPressed[buttonUndoButtonPin]) {
        // Check if there is a passcode entered
        // Transition 3-3(d)
        if (enteredPasscodeLength > 0) {
          // Remove last entry
          enteredPasscodeLength--;
          enteredPasscode[enteredPasscodeLength] = 0;
        }
      } else if (enteredPasscodeLength == 0) {
        // Transition 3-3(c)
        setLedColour(0, 0, 255);
        displayLocked();
      } else if (enteredPasscodeLength > 0) {
        // Transition 3-3(b)
        setLedColour(255, 255, 255);
        displayMaskedPasscode();
      } else {
        // Transition 3-3(a)
      }

      if (lockedState) {
        lastUnlockedTime = millis();
      }

      return State::WaitForButton;
    default:
      Serial.println("ERROR: Unknown state");
      exit(500);
  }
}

// This function runs repeatedly, updates the inputs and then runs one
// iteration of the FSM to get the new state of the FSM every 50 ms. The
// function also pets the watchdog to make sure that the watchdog reset
// never happens when the code is working properly.
// Inputs: none
// Outputs: none
// Side effects: updates FSM to the state returned by updateState.
void loop() {
#ifndef TESTING
  static State state = State::Init;
  petWatchdog();
  updateInputs();
  state = updateFSM(state);
  delay(50);
#endif
}

// This function is invoked for the Watchdog Timer early warning.
// Outputs a warning to the console that the watchdog reset is about to occur.
// Inputs: none
// Outputs: none
// Side effects: clears early warning interrupt, prints warning to serial console
void WDT_Handler() {
  // Clear interrupt register flag
  // (reference register with WDT->registername.reg)
  WDT->INTFLAG.bit.EW = 1;

  // Warn user that a watchdog reset may happen
  Serial.println("Warning: Watchdog reset may occur.");
}

/*        
 * Helper function for printing states
 */
char* s2str(State s) {
  switch (s) {
    case Init:
      return "(1) INIT";
    case Locked:
      return "(2) LOCKED";
    case WaitForButton:
      return "(3) WAIT_FOR_BUTTON";
    case Unlocked:
      return "(4) UNLOCKED";
    case ResetPasscode:
      return "(5) RESET_PASSCODE";
    default:
      return "???";
  }
}

/*
 * Function to test an individual state transition
*/
bool testTransition(State startState,
                    State endState,
                    state_inputs testStateInputs,
                    state_vars startStateVars,
                    state_vars endStateVars,
                    bool verbose) {

  memcpy(buttonPressed, testStateInputs.buttonPressed, 12);
  memcpy(buttonStatuses, testStateInputs.buttonStatuses, 12 * sizeof(*buttonStatuses));

  passcodeLength = startStateVars.passcodeLength;
  enteredPasscodeLength = startStateVars.enteredPasscodeLength;
  timerInterruptCount = startStateVars.timerInterruptCount;
  memcpy(currentPasscode, startStateVars.currentPasscode, maxLength + 1);
  memcpy(enteredPasscode, startStateVars.enteredPasscode, maxLength + 1);

  State resultState = updateFSM(startState);

  bool passedTest = (endState == resultState and passcodeLength == endStateVars.passcodeLength and enteredPasscodeLength == endStateVars.enteredPasscodeLength and timerInterruptCount == endStateVars.timerInterruptCount and !strcmp(currentPasscode, endStateVars.currentPasscode) and !strcmp(enteredPasscode, endStateVars.enteredPasscode));

  // Verbose logs for testing
  if (verbose) {
    Serial.println(endState == resultState);
    Serial.println(enteredPasscodeLength == endStateVars.enteredPasscodeLength);
    Serial.println(timerInterruptCount == endStateVars.timerInterruptCount);
    Serial.println(!strcmp(currentPasscode, endStateVars.currentPasscode));
    Serial.println(!strcmp(enteredPasscode, endStateVars.enteredPasscode));

    Serial.println("End State: ");
    Serial.println(endState);
    Serial.println(resultState);

    Serial.println("Entered Passcode Length: ");
    Serial.println(enteredPasscodeLength);
    Serial.println(endStateVars.enteredPasscodeLength);

    Serial.println("Timer Interrupt Count: ");
    Serial.println(timerInterruptCount);
    Serial.println(endStateVars.timerInterruptCount);

    Serial.println("Current Passcode: ");
    Serial.println(currentPasscode);
    Serial.println(endStateVars.currentPasscode);

    Serial.println("Entered Passcode: ");
    Serial.println(enteredPasscode);
    Serial.println(endStateVars.enteredPasscode);
  }

  char sToPrint[200];
  if (passedTest) {
    sprintf(sToPrint, "Test from %s to %s PASSED", s2str(startState), s2str(endState));
    Serial.println(sToPrint);
    return true;
  } else {
    sprintf(sToPrint, "Test from %s to %s FAILED", s2str(startState), s2str(endState));
    Serial.println(sToPrint);
    return false;
  }
}
