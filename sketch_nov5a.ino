#include <Servo.h>

Servo myservo;

const int autoLockTimeout = 30;
bool autoLockInterrupt = false;

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

char currentPasscode[11] = {
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
  LOW,  // lock/unlock button --> if locked: submit; if unlocked: lock using old passcode
  LOW,  // undo button --> if locked: undo; if unlocked: reset combination; if in reset mode: submits the new combination
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
  pinMode(11, INPUT);

  // myservo.attach(5);
  Serial.begin(9600);
  while (!Serial)
    ;
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
    digitalRead(11),
  };

  for (int i = 0; i <= 11; i++) {
    int newStatus = newStatuses[i];
    buttonPressed[i] = false;

    if (newStatus != buttonStatuses[i]) {
      buttonStatuses[i] = newStatus;
      Serial.print("Button ");
      Serial.print(i);
      Serial.println(newStatus == HIGH ? " pressed" : "released");
      buttonPressed[i] = newStatus == LOW;
    }
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
}

void displayInitPasscode() {
  Serial.print("Initial passcode: ");
  Serial.println((const char*)currentPasscode);
}

void displayLocked() {
  Serial.println("LOCKED");
}


void updateEnteredPassword() {
  if (enteredPasscodeLength < maxLength) {
    for (int i = 0; i <= 9; i++) {
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
        disableTimeoutTimer();
        return State::ResetPasscode;
      } else if (autoLockInterrupt) {
        // Autolock after 30 seconds of inactivity
        disableTimeoutTimer();
        return State::Locked;
      }
      // TODO: autolock after 30 seconds of inactivity
      return State::Unlocked;
    case State::ResetPasscode:
      break;
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
          Serial.println("UNLOCKED");
          enableTimeoutTimer();
          return State::Unlocked;
        }
        // Incorrect combination
        Serial.println("INCORRECT PASSCODE");
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
      return State::WaitForButton;
    default:
      Serial.println("ERROR: Unknown state");
      exit(500);
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  static State state = State::Init;

  updateInputs();
  state = updateFSM(state);

  // myservo.write(isButtonPressed == HIGH ? 180 : 0);
  // TODO pet watchdog
  delay(100);
}
