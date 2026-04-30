#include <Servo.h>

Servo servo1;
Servo servo2;

const int SERVO1_PIN = 6;      // D6
const int SERVO2_PIN = 7;      // D7
const int LASER_PIN  = 5;      // choose your laser control pin

const int MIN_US = 600;
const int MAX_US = 2400;

bool laserActive = false;
unsigned long laserOffTime = 0;

void setup() {
  Serial.begin(115200);

  // Avoid getting stuck forever on MKR if Serial isn't "ready"
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) { }

  servo1.attach(SERVO1_PIN, MIN_US, MAX_US);
  servo2.attach(SERVO2_PIN, MIN_US, MAX_US);

  servo1.writeMicroseconds(1500);
  servo2.writeMicroseconds(1500);

  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);

  Serial.println("Ready. Commands:");
  Serial.println("  1 <us>       (e.g. 1 1500)");
  Serial.println("  2 <us>       (e.g. 2 1500)");
  Serial.println("  B <us1> <us2> (e.g. B 1500 1600)");
  Serial.println("  L            (turn laser on for 5 seconds)");
}

static bool inRange(int us) {
  return us >= MIN_US && us <= MAX_US;
}

void loop() {
  // Turn laser off when time expires
  if (laserActive && millis() >= laserOffTime) {
    digitalWrite(LASER_PIN, LOW);
    laserActive = false;
    Serial.println("Laser OFF");
  }

  if (!Serial.available()) return;

  char cmd = Serial.peek();

  // If line starts with a letter, read it as a command token
  if ((cmd >= 'A' && cmd <= 'Z') || (cmd >= 'a' && cmd <= 'z')) {
    cmd = (char)Serial.read();   // consume the letter

    if (cmd == 'B' || cmd == 'b') {
      int us1 = Serial.parseInt();
      int us2 = Serial.parseInt();

      if (inRange(us1) && inRange(us2)) {
        servo1.writeMicroseconds(us1);
        servo2.writeMicroseconds(us2);
        Serial.print("OK B us1=");
        Serial.print(us1);
        Serial.print(" us2=");
        Serial.println(us2);
      } else {
        Serial.print("Out of range. Use ");
        Serial.print(MIN_US);
        Serial.print("..");
        Serial.println(MAX_US);
      }
    }
    else if (cmd == 'L' || cmd == 'l') {
      digitalWrite(LASER_PIN, HIGH);
      laserActive = true;
      laserOffTime = millis() + 5000;
      Serial.println("Laser ON for 5 seconds");
    }
    else {
      Serial.println("Unknown command. Use 1 <us>, 2 <us>, B <us1> <us2>, or L.");
    }
  } else {
    // Otherwise interpret as: <servoIndex> <us>
    int which = Serial.parseInt();  // 1 or 2
    int us    = Serial.parseInt();

    if (!inRange(us)) {
      Serial.print("Out of range. Use ");
      Serial.print(MIN_US);
      Serial.print("..");
      Serial.println(MAX_US);
    } else if (which == 1) {
      servo1.writeMicroseconds(us);
      Serial.print("OK s=1 us=");
      Serial.println(us);
    } else if (which == 2) {
      servo2.writeMicroseconds(us);
      Serial.print("OK s=2 us=");
      Serial.println(us);
    } else {
      Serial.println("Bad servo index. Use 1 or 2.");
    }
  }

  // clear rest of line
  while (Serial.available()) Serial.read();
}