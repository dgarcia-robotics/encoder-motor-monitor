// David Garcia
// Lab 5.1 - Free Assignment
// Davgarci@uat.edu
//
// Purpose: Drive a DC motor through an L293D using a PWM "throttle" pot,
//          read a quadrature encoder (Channel A), compute/display RPM,
//          and give visual feedback with an RGB LED. LCD shows A/B levels and RPM/PWM.
//
// Note: Channel A edges are counted BOTH via external interrupt (D2/INT0) and by
//       polling in loop(). Tinkercad's simulator stops delivering the interrupt
//       while the parallel LCD is active, so the RPM math uses whichever counter
//       captured more edges in the sample window. On real hardware both counters
//       simply agree.

#include <LiquidCrystal.h>

// --------------------------- Pin assignments ---------------------------

const int encoderPinA = 2;     // Encoder Channel A -> D2 (INT0-capable)
const int encoderPinB = 3;     // Encoder Channel B -> D3
const int motorEnablePin = 9;  // L293D EN1,2 <- PWM sets motor speed
const int motorInput1Pin = A2; // L293D IN1, held HIGH (IN2 hard-wired to GND)
const int speedPotPin  = A0;   // Throttle pot wiper
const int redPin   = 5;        // RGB red   (PWM)
const int greenPin = 6;        // RGB green (PWM)
const int bluePin  = 10;       // RGB blue  (PWM)

// --------------------------- LCD wiring ---------------------------
// 4-bit mode, RW hard-wired to GND. Args: RS, E, D4, D5, D6, D7.
LiquidCrystal lcd(4, 7, 8, 12, 13, 11);

// --------------------------- Encoder/RPM state ---------------------------

volatile unsigned long pulseCount = 0;  // Edges counted by the ISR
unsigned long polledCount = 0;          // Edges counted by polling in loop()
int lastLevelA = -1;                    // Last polled level of Channel A
unsigned long lastMs = 0;               // Timestamp of last 500 ms update

// Counts-Per-Revolution: edges seen on Channel A (both edges) per motor rev.
// Calibrated against Tinkercad's motor badge: ~698 edges/0.5s at 184 RPM
// -> CPR = 698 * 120 / 184 ≈ 455.
// To recalibrate: at full throttle note serial "pulses/0.5s" (P) and the
// motor badge RPM (R), then set CPR = P * 120 / R.
const unsigned int CPR = 455;

int rpm = 0;                            // Latest computed RPM

// --------------------------- Interrupt Service Routine ---------------------------

void isrA() {
  pulseCount++;                         // Count one edge of Channel A
}

// --------------------------- Utility: set RGB LED color ---------------------------

void setColor(int r, int g, int b) {
  analogWrite(redPin,   r);
  analogWrite(greenPin, g);
  analogWrite(bluePin,  b);
}

// --------------------------- Arduino setup() ---------------------------

void setup() {
  Serial.begin(9600);

  pinMode(encoderPinA, INPUT_PULLUP);
  pinMode(encoderPinB, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(encoderPinA), isrA, CHANGE);

  pinMode(motorEnablePin, OUTPUT);
  pinMode(motorInput1Pin, OUTPUT);
  digitalWrite(motorInput1Pin, HIGH);   // Forward (IN2 tied to GND in hardware)

  pinMode(redPin,   OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin,  OUTPUT);

  lcd.begin(16, 2);
  delay(100);
  lcd.clear();
  lcd.print("A:-- B:--");
  lcd.setCursor(0, 1);
  lcd.print("RPM:---- PWM:---");
}

// --------------------------- Arduino loop() ---------------------------

void loop() {
  // ----- Poll Channel A for edges (backup counter for the ISR) -----
  int levelA = digitalRead(encoderPinA);
  if (levelA != lastLevelA) {
    polledCount++;
    lastLevelA = levelA;
  }

  // ----- Read throttle and apply to motor -----
  int pot = analogRead(speedPotPin);
  int pwm = map(pot, 0, 1023, 0, 255);
  analogWrite(motorEnablePin, pwm);

  // ----- Periodic RPM calculation (every 500 ms) -----
  unsigned long now = millis();
  if (now - lastMs >= 500) {
    noInterrupts();                     // Snapshot + reset both counters atomically
    unsigned long isrPulses = pulseCount;
    pulseCount = 0;
    interrupts();

    unsigned long polled = polledCount;
    polledCount = 0;

    // Trust whichever counter saw more edges this window (see note at top).
    unsigned long pulses = (isrPulses > polled) ? isrPulses : polled;

    // pulses / CPR = revs per 0.5 s; x2 = revs/s; x60 = RPM  ->  pulses*120/CPR
    rpm = (int)((pulses * 120UL) / (unsigned long)CPR);

    // ----- LCD first row: live A/B logic levels -----
    lcd.setCursor(0, 0);
    lcd.print("A:");
    lcd.print(levelA);
    lcd.print(" B:");
    lcd.print(digitalRead(encoderPinB));
    lcd.print("      ");

    // ----- LCD second row: exactly 16 chars -> "RPM:xxxx PWM:xxx" -----
    lcd.setCursor(0, 1);
    lcd.print("RPM:");
    if (rpm < 1000) lcd.print(' ');
    if (rpm < 100)  lcd.print(' ');
    if (rpm < 10)   lcd.print(' ');
    lcd.print(rpm);
    lcd.print(" PWM:");
    int pct = (pwm * 100) / 255;
    if (pct < 100) lcd.print(' ');
    if (pct < 10)  lcd.print(' ');
    lcd.print(pct);

    // ----- Serial debug -----
    Serial.print("A=");            Serial.print(levelA);
    Serial.print(" B=");           Serial.print(digitalRead(encoderPinB));
    Serial.print(" isr=");         Serial.print(isrPulses);
    Serial.print(" polled=");      Serial.print(polled);
    Serial.print(" rpm=");         Serial.print(rpm);
    Serial.print(" pwm=");         Serial.println(pwm);

    lastMs = now;
  }

  // ----- RGB LED speed feedback -----
  // Color by RPM bands; fall back to PWM value before first RPM sample arrives.
  int metric = (rpm > 0 ? rpm : pwm);
  if (metric < 50)        setColor(0,   0,   255);  // Blue   = stopped / very slow
  else if (metric < 120)  setColor(0,   255, 0);    // Green  = slow-medium
  else if (metric < 200)  setColor(255, 255, 0);    // Yellow = medium-fast
  else                    setColor(255, 0,   0);    // Red    = fast
}
