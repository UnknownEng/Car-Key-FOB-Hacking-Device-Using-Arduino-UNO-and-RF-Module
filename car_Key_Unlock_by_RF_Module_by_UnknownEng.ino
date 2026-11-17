/*
 * RF (433 MHz nRF24) + IR Capture-&-Replay Demonstrator   --  EDUCATIONAL
 * -------------------------------------------------------------------------
 * Target  : Arduino UNO (ATmega328P)
 * Purpose : Teach how a *fixed-code* capture/replay works on an RF/IR link,
 *           and WHY it fails against modern rolling-code car fobs.
 *
 *  !!  LEGAL / ETHICAL NOTICE  !!
 *  Use ONLY on hardware you own or are explicitly authorised to test.
 *  This sketch performs a FIXED-code replay. Real vehicle key fobs use
 *  cryptographic rolling codes (KeeLoq, Hitag, etc.) whose counter advances
 *  on every press, so a captured frame will NOT re-open them. The code
 *  demonstrates the concept and its limitation -- it is not a theft tool.
 *
 * Libraries: IRremote 4.x , RF24 1.x
 * -------------------------------------------------------------------------
 *
 * ---- WIRING / PIN MAP (Arduino UNO) -- no UART or SPI conflicts ----
 *   IR receiver (TSOP) OUT ....... D7
 *   IR LED (via transistor) ...... D3   (IRremote carrier pin on AVR)
 *   nRF24L01  CE ................. D9
 *   nRF24L01  CSN ................ D10
 *   nRF24L01  SCK/MOSI/MISO ...... D13 / D11 / D12  (hardware SPI - reserved)
 *   Button CAPTURE ............... D4  -> GND  (uses internal pull-up)
 *   Button REPLAY  ............... D5  -> GND
 *   Button RESET   ............... D6  -> GND
 *   Status LED (heartbeat) ....... D8  (+220R)
 *   RX activity LED .............. A0  (+220R)
 *   TX activity LED .............. A1  (+220R)
 *   USB Serial (D0/D1) left free for the 9600-baud log.
 */

#define DECODE_NEC                 // enable only the NEC IR protocol (saves flash)
#include <IRremote.hpp>            // IRremote 4.x -- include exactly once
#include <SPI.h>
#include <RF24.h>                  // RF24 1.x

// ----------------------------- Pin map --------------------------------
constexpr uint8_t IR_RECEIVE_PIN = 7;
constexpr uint8_t IR_SEND_PIN    = 3;
constexpr uint8_t RF_CE_PIN      = 9;
constexpr uint8_t RF_CSN_PIN     = 10;

constexpr uint8_t BTN_CAPTURE    = 4;      // active-LOW, INPUT_PULLUP
constexpr uint8_t BTN_REPLAY     = 5;
constexpr uint8_t BTN_RESET      = 6;

constexpr uint8_t STATUS_LED     = 8;      // heartbeat (NOT D13 -- that is SPI SCK)
constexpr uint8_t RX_LED         = A0;     // activity LEDs kept off the UART pins
constexpr uint8_t TX_LED         = A1;

// Optional: run one capture->replay automatically ~1 s after boot, so the
// full flow is visible on the serial log without pressing a button (handy in
// a simulator). Leave 0 for normal button-driven operation; set 1 to demo.
#define DEMO_SELFTEST 0

// ----------------------------- RF config ------------------------------
RF24 radio(RF_CE_PIN, RF_CSN_PIN);         // ONE module = ONE object (CE, CSN differ)
const byte rfAddress[6]      = "CARKY";    // 5-byte pipe address
constexpr uint8_t RF_PAYLOAD = 8;          // bytes per frame
bool radioReady              = false;

// ------------------------ Demo "known" codes --------------------------
constexpr uint32_t DEMO_IR_UNLOCK = 0x00FFA25D;   // demo NEC frame (NOT a real fob)

// -------------------------- Capture buffer ----------------------------
byte capturedRF[RF_PAYLOAD] = {0};
bool hasRFCapture           = false;

// ---------------------------- Heartbeat -------------------------------
constexpr unsigned long HEARTBEAT_MS = 250;
unsigned long lastBeat               = 0;

// ------------------------ Button + debounce ---------------------------
struct Button {
  uint8_t       pin;
  bool          last;      // last stable level (HIGH = released)
  unsigned long tEdge;
};
Button bCapture{BTN_CAPTURE, HIGH, 0};
Button bReplay {BTN_REPLAY,  HIGH, 0};
Button bReset  {BTN_RESET,   HIGH, 0};
constexpr unsigned long DEBOUNCE_MS = 40;

// --------------------------- Prototypes -------------------------------
bool pressed(Button &b);
void heartbeat();
void captureRF();
void triggerReplay();
void resetState();
void pollIR();
void pollRF();
void printPayload(const __FlashStringHelper *tag, const byte *buf, uint8_t n);

// =============================== SETUP ================================
void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 2000) { /* wait for USB serial, max 2 s */ }
  Serial.println(F("\n=== RF/IR Capture-Replay Demonstrator (educational) ==="));

  pinMode(BTN_CAPTURE, INPUT_PULLUP);
  pinMode(BTN_REPLAY,  INPUT_PULLUP);
  pinMode(BTN_RESET,   INPUT_PULLUP);
  pinMode(STATUS_LED,  OUTPUT);
  pinMode(RX_LED,      OUTPUT);
  pinMode(TX_LED,      OUTPUT);

  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
  IrSender.begin(IR_SEND_PIN);
  Serial.println(F("[IR]  receiver + sender ready"));

  radioReady = radio.begin();
  if (radioReady) {
    radio.setPALevel(RF24_PA_LOW);
    radio.setPayloadSize(RF_PAYLOAD);
    radio.openWritingPipe(rfAddress);
    radio.openReadingPipe(1, rfAddress);
    radio.startListening();
    Serial.println(F("[RF]  nRF24L01 online, listening"));
  } else {
    Serial.println(F("[RF]  nRF24L01 NOT detected (simulation / no module) -> RF disabled"));
  }

  Serial.println(F("Buttons:  D4 = CAPTURE   D5 = REPLAY   D6 = RESET"));
  Serial.println(F("Ready.\n"));

#if DEMO_SELFTEST
  delay(1000);
  Serial.println(F("[SELFTEST] running one capture -> replay cycle ..."));
  captureRF();
  triggerReplay();
  Serial.println(F("[SELFTEST] done -- buttons remain live\n"));
#endif
}

// =============================== LOOP ================================
void loop() {
  heartbeat();

  if (pressed(bCapture)) captureRF();
  if (pressed(bReplay))  triggerReplay();
  if (pressed(bReset))   resetState();

  pollIR();
  pollRF();
}

// ============================= HELPERS ===============================

// Debounced falling-edge detector for an active-LOW (INPUT_PULLUP) button.
bool pressed(Button &b) {
  bool level = digitalRead(b.pin);                 // HIGH = released, LOW = pressed
  if (level != b.last && (millis() - b.tEdge) > DEBOUNCE_MS) {
    b.tEdge = millis();
    b.last  = level;
    if (level == LOW) return true;                 // fresh press
  }
  return false;
}

// Non-blocking status-LED heartbeat.
void heartbeat() {
  if (millis() - lastBeat >= HEARTBEAT_MS) {
    lastBeat = millis();
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
  }
}

// Capture: grab the next live RF frame; if none arrives (e.g. in simulation
// with no radio) store a clearly-labelled demo payload so the flow is visible.
void captureRF() {
  Serial.println(F("[CAPTURE] listening for an RF frame ..."));
  digitalWrite(RX_LED, HIGH);

  bool got = false;
  if (radioReady) {
    unsigned long t0 = millis();
    while (millis() - t0 < 1500) {                 // 1.5 s capture window
      if (radio.available()) {
        radio.read(capturedRF, RF_PAYLOAD);
        got = true;
        break;
      }
    }
  }

  if (!got) {
    for (uint8_t i = 0; i < RF_PAYLOAD; i++) capturedRF[i] = 0xA0 + i;
    Serial.println(F("[CAPTURE] no live frame -> stored DEMO payload"));
  } else {
    Serial.println(F("[CAPTURE] live RF frame stored"));
  }

  hasRFCapture = true;
  printPayload(F("captured"), capturedRF, RF_PAYLOAD);
  digitalWrite(RX_LED, LOW);
}

// Replay: re-transmit the exact captured bytes (fixed-code replay).
void triggerReplay() {
  if (!hasRFCapture) {
    Serial.println(F("[REPLAY] nothing captured yet - press CAPTURE first"));
    return;
  }
  Serial.println(F("[REPLAY] re-transmitting captured frame ..."));
  digitalWrite(TX_LED, HIGH);

  if (radioReady) {
    radio.stopListening();
    bool ok = radio.write(capturedRF, RF_PAYLOAD);
    radio.startListening();
    Serial.print(F("[REPLAY] RF write -> "));
    Serial.println(ok ? F("ACK ok") : F("no ACK"));
  } else {
    Serial.println(F("[REPLAY] (simulation) would transmit the bytes below"));
  }
  printPayload(F("replayed"), capturedRF, RF_PAYLOAD);

  // Mirror the action on the IR channel so the demo is visible both ways.
  IrSender.sendNECRaw(DEMO_IR_UNLOCK, 0);

  Serial.println(F("  NOTE: a real rolling-code fob would REJECT this replay --"));
  Serial.println(F("        the counter it expects has already advanced."));
  digitalWrite(TX_LED, LOW);
}

// Clear the capture buffer / state.
void resetState() {
  hasRFCapture = false;
  memset(capturedRF, 0, RF_PAYLOAD);
  Serial.println(F("[RESET] capture buffer cleared"));
}

// Poll the IR receiver; an incoming demo-unlock code triggers a replay.
void pollIR() {
  if (!IrReceiver.decode()) return;
  uint32_t code = IrReceiver.decodedIRData.decodedRawData;
  Serial.print(F("[IR] rx 0x"));
  Serial.println(code, HEX);
  if (code == DEMO_IR_UNLOCK) {
    Serial.println(F("[IR] demo unlock code -> replay"));
    triggerReplay();
  }
  IrReceiver.resume();
}

// Poll the RF receiver and log any inbound frame.
void pollRF() {
  if (!radioReady || !radio.available()) return;
  byte frame[RF_PAYLOAD];
  radio.read(frame, RF_PAYLOAD);
  printPayload(F("[RF] rx"), frame, RF_PAYLOAD);
}

// Pretty-print a byte buffer as hex to the serial log.
void printPayload(const __FlashStringHelper *tag, const byte *buf, uint8_t n) {
  Serial.print(F("  "));
  Serial.print(tag);
  Serial.print(F(" ["));
  Serial.print(n);
  Serial.print(F("B]:"));
  for (uint8_t i = 0; i < n; i++) {
    Serial.print(' ');
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
  }
  Serial.println();
}
