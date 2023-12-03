#include <Servo.h>
#include <LiquidCrystal.h>

Servo myservo;

constexpr int maxLength = 10;
int passcodeLength = 4;
int enteredPasscodeLength = 0;


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

constexpr int rs = A0, en = A1, d4 = A2, d5 = A3, d6 = A4, d7 = A5;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


void setup() {
  // put your setup code here, to run once:
  pinMode(0, INPUT);
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

  // myservo.attach(5);
  Serial.begin(9600);
  while (!Serial)
    ;

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

  lcd.begin(16,2);
}


void resetAllInputs() {
  memset(enteredPasscode, 0, maxLength + 1);
  enteredPasscodeLength = 0;

  memset(currentPasscode, 0, maxLength + 1);

  for (int i = 0; i < 4; i++) {
    currentPasscode[i] = '0';
  }
  passcodeLength = 4;

  memset(buttonPressed, false, 12);
  memset(buttonStatuses, LOW, 12);
  lockedState = true;
}

void updateInputs() {
  int newStatuses[12] = {
    digitalRead(0),
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

void petWatchdog() {
  WDT->CLEAR.reg = WDT_CLEAR_CLEAR(0xA5);
}

void displayInitPasscode() {
  Serial.print("Initial passcode: ");
  Serial.println((const char*)currentPasscode);

  lcd.setCursor(0,0);
  lcd.print("INITIAL PASSCODE");
  lcd.setCursor(0,1);
  lcd.print((const char*) currentPasscode);
  
  for (int i = 0; i < 10; i++) {
    delay(100);
    petWatchdog();
  }
}

void displayAutoLocked() {
  Serial.println("AUTOLOCKING TIMEOUT");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("AUTO-LOCKING...");
}

void displayAutoReset() {
  Serial.println("AUTORESET TIMEOUT");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("AUTO-RESETTING...");
}


void displayLocked() {
  Serial.println("LOCKED");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("LOCKED!");
}

void displayUnlocked() {
  Serial.println("UNLOCKED");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("UNLOCKED!");
}

void displayWrongPasscode() {
  Serial.println("WRONG PASSCODE");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WRONG PASSCODE!");

  for (int i = 0; i < 10; i++) {
    delay(100);
    petWatchdog();
  }
}

void displayMaskedPasscode() {
  char* i = enteredPasscode;
  Serial.print("Entered Passcode: ");
  while (*i) {
    Serial.print('*');
    i++;
  }
  Serial.println();

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("PASSCODE:");
  lcd.setCursor(0,1);
  char* j = enteredPasscode;
  int counter = 0;
  while (*j) {
    lcd.print("*");
    j++;
    counter ++;
  }
}

void displayPasswordChanged() {
  Serial.print("Password changed!");
  Serial.println((const char*)currentPasscode);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("PASSWORD CHANGED");

  for (int i = 0; i < 10; i++) {
    delay(100);
    petWatchdog();
  }
}

void displayEnterNewPasscode() {
  Serial.print("New passcode: ");
  Serial.println((const char*)enteredPasscode);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("NEW PASSCODE:");

  lcd.setCursor(0,1);
  char* j = enteredPasscode;
  int counter = 0;
  while (*j) {
    lcd.print(*j);
    j++;
    counter++;
  }
}

void displayNewPasswordEmptyError() {
  Serial.print("New passcode cannot be empty!");

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("NEW PASSCODE");
  lcd.setCursor(0,1);
  lcd.print("CANNOT BE EMPTY");
  for (int i = 0; i < 10; i++) {
    delay(100);
    petWatchdog();
  }
}

void updateEnteredPassword() {
  // Check if the user can still enter buttons
  if (enteredPasscodeLength < maxLength) {
    // Iterate over all buttons and check if they are pressed
    for (int i = 0; i <= 9; i++) {
      if (i == 5) {
        continue;
      }

      if (buttonPressed[i]) {
        enteredPasscode[enteredPasscodeLength] = i + '0';
        enteredPasscodeLength++;
        break;
      }
    }
  }
}

void enableTimeoutTimer() {
}

void disableTimeoutTimer() {
}

State updateFSM(State oldState) {
  switch (oldState) {
    case State::Init:
      displayInitPasscode();
      return State::Locked;
    case State::Locked:
      lastUnlockedTime = millis();
      lastResetTime = millis();
      displayLocked();
      return State::WaitForButton;
    case State::Unlocked:
      if (buttonPressed[buttonLockUnlockPin]) {
        // User presses lock button
        lockedState = true;
        displayLocked();
        disableTimeoutTimer();
        return State::Locked;
      } else if (buttonPressed[buttonUndoButtonPin]) {
        // User presses undo --> enters reset state
        lastResetTime = millis();
        disableTimeoutTimer();
        return State::ResetPasscode;
      } else if (millis() - lastUnlockedTime > autoLockLimit) {
        // Autolock after 30 seconds of inactivity
        lockedState = true;
        displayAutoLocked();
        displayLocked();
        disableTimeoutTimer();
        return State::Locked;
      }
      // TODO: autolock after 30 seconds of inactivity
      return State::Unlocked;
    case State::ResetPasscode:
      // User can enter digits for the new passcode
      updateEnteredPassword();
      displayEnterNewPasscode();

      lastUnlockedTime = millis();

      if (buttonPressed[buttonLockUnlockPin]) {
        // User can submit the passcode
        if (enteredPasscodeLength > 0) {
          // Allow this to be the new passcode
          // Copy the entered passcode to the current passcode
          strcpy(currentPasscode, enteredPasscode);
          passcodeLength = strlen(currentPasscode);
          memset(enteredPasscode, 0, maxLength + 1);
          enteredPasscodeLength = 0;
          // Reset the entered passcode
          displayPasswordChanged();
          return State::Locked;
        } else {
          displayNewPasswordEmptyError();
          return State::ResetPasscode;
        }
      } else if (buttonPressed[buttonUndoButtonPin]) {
        // User can press undo button to exit reset passcode state --> goes to unlocked
        enteredPasscodeLength = 0;
        memset(enteredPasscode, 0, maxLength + 1);
        lastResetTime = millis();
        displayUnlocked();
        return State::Unlocked;
      } else if (millis() - lastResetTime > autoResetLimit) {
        resetAllInputs();
        displayAutoReset();
        displayInitPasscode();
        return State::Init;
      }
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
          lockedState = false;
          lastUnlockedTime = millis();
          displayUnlocked();
          enableTimeoutTimer();
          return State::Unlocked;
        }
        // Incorrect combination
        displayWrongPasscode();
        return State::Locked;
      } else if (buttonPressed[buttonUndoButtonPin]) {
        // Check if there is a passcode entered
        if (enteredPasscodeLength > 0) {
          // Remove last entry
          enteredPasscodeLength--;
          enteredPasscode[enteredPasscodeLength] = 0;
        }
      } else if (enteredPasscodeLength == 0) {
        displayLocked();
      } else if (enteredPasscodeLength > 0) {
        displayMaskedPasscode();
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

void loop() {
  // put your main code here, to run repeatedly:
  static State state = State::Init;
  petWatchdog();
  updateInputs();
  state = updateFSM(state);
  delay(100);
}

void WDT_Handler() {
  // Clear interrupt register flag
  // (reference register with WDT->registername.reg)
  WDT->INTFLAG.bit.EW = 1;

  // Warn user that a watchdog reset may happen
  Serial.println("Warning: Watchdog reset may occur.");
}
