#include <Arduino.h>
#include "Can485DisplayHelper.h"
#include <ASTCanLib.h>
#include <math.h>

#define BLOCK 255 //block character to build bar graphs
#define SPACE 32  //space character for start animation

#define DEBUG false

// CAN BUS OBJECTS //////////////////////////////////////////////

struct CanVariable
{
  int id;
  bool filled;
  byte data[8];
};

const byte CAN_MESSAGE_COUNT = 5;
CanVariable* allCan[CAN_MESSAGE_COUNT];
CanVariable can1512 = {1512, false, NULL};
CanVariable can1513 = {1513, false, NULL};
CanVariable can1514 = {1514, false, NULL};
CanVariable can1515 = {1515, false, NULL};
CanVariable can1516 = {1516, false, NULL};


// SET UP PINS //////////////////////////////////////////////////

//This version of AltSoftSerial hard-codes the pins to 9 (rx) and 5(tx)
//This also disables PWM on 6 and 7

const byte LED_ERR = LED_BUILTIN;   // 'check engine' light
const byte LED_SHIFT = 15;          // shift light
const byte GAUGE_PIN = 18;          // pushbutton to cycle through modes

// BUILD ENGINE VARIABLES ///////////////////////////////////////

#pragma region engine variables
struct EngineVariable
{
  char* shortLabel;
  float currentValue;
  float previousValue;
  float minimum;
  float maximum;
  byte decimalPlaces;
  unsigned long goodCount;
  unsigned long lowCount;
  unsigned long highCount;
};
const byte ENGINE_VARIABLE_COUNT = 20;
EngineVariable* allGauges[ENGINE_VARIABLE_COUNT];
EngineVariable engine_map   = {"MAP", 0.0, 0.0, 15.0, 250.0, 1, 0, 0, 0};     //manifold absolute pressure
EngineVariable engine_rpm   = {"RPM", 0.0, 0.0, 700.0, 6000.0, 0, 0, 0, 0};   //engine rpm
EngineVariable engine_clt   = {"CLT", 0.0, 0.0, 20.0, 240.0, 0, 0, 0, 0};     //coolant temp
EngineVariable engine_tps   = {"TPS", 0.0, 0.0, 0.0, 100.0, 0, 0, 0, 0};      //throttle position
EngineVariable engine_pw1   = {"PW1", 0.0, 0.0, 0.0, 20.0, 2, 0, 0, 0};       //injector pulse width bank 1
EngineVariable engine_pw2   = {"PW2", 0.0, 0.0, 0.0, 20.0, 2, 0, 0, 0};       //injector pulse width bank 2
EngineVariable engine_iat   = {"IAT", 0.0, 0.0, 40.0, 150.0, 0, 0, 0, 0};     //intake air temp aka 'mat'
EngineVariable engine_adv   = {"ADV", 0.0, 0.0, 10.0, 40.0, 1, 0, 0, 0};      //ignition advance
EngineVariable engine_tgt   = {"TGT", 0.0, 0.0, 10.0, 20.0, 1, 0, 0, 0};      //afr target
EngineVariable engine_afr   = {"AFR", 0.0, 0.0, 10.0, 20.0, 1, 0, 0, 0};      //air fuel ratio
EngineVariable engine_ego   = {"EGO", 0.0, 0.0, 70.0, 130.0, 0, 0, 0, 0};     //ego correction %
EngineVariable engine_egt   = {"EGT", 0.0, 0.0, 0.0, 2000.0, 0, 0, 0, 0};     //exhaust gas temp
EngineVariable engine_pws   = {"PWS", 0.0, 0.0, 0.0, 20.0, 2, 0, 0, 0};       //injector pulse width sequential
EngineVariable engine_bat   = {"BAT", 0.0, 0.0, 8.0, 18.0, 1, 0, 0, 0};       //battery voltage
EngineVariable engine_sr1   = {"SR1", 0.0, 0.0, 0.0, 999.0, 1, 0, 0, 0};      //generic sensor 1
EngineVariable engine_sr2   = {"SR2", 0.0, 0.0, 0.0, 999.0, 1, 0, 0, 0};      //generic sensor 2
EngineVariable engine_knk   = {"KNK", 0.0, 0.0, 0.0, 50.0, 1, 0, 0, 0};       //knock ignition retard
EngineVariable engine_vss   = {"VSS", 0.0, 0.0, 0.0, 160.0, 0, 0, 0, 0};      //vehicle speed
EngineVariable engine_tcr   = {"TCR", 0.0, 0.0, 0.0, 50.0, 1, 0, 0, 0};       //traction control ignition retard
EngineVariable engine_lct   = {"LCT", 0.0, 0.0, 0.0, 50.0, 1, 0, 0, 0};       //launch control timing
#pragma endregion

// LOOP TIMER VARIABLES ///////////////////////////////

unsigned long currentMillis = 0;
byte displayInterval = 100;
unsigned long lastDisplayMillis = 0;
unsigned int diagnosticInterval = 5000;
unsigned long lastDiagnosticMillis = 0;

// MODE VARIABLES ////////////////////////////////////

enum Mode {
  single,
  dual,
  sensor_warmup,
  engine_warmup,
  diag
};
Mode currentMode;

bool dualModeReady = false;
bool diagModeReady = false;

bool inError = false;

bool currentGaugeButton = 1;
bool previousGaugeButton = 1;
unsigned long gaugeButtonMillis = 0;

const byte DEBOUNCE_DELAY = 250;
const byte SHIFT_LIGHT_FROM_REDLINE = 500;

// CAN VARIABLES /////////////////////////////////////

st_cmd_t canMsg;
uint8_t canBuffer[8] = {};

#define MESSAGE_PROTOCOL  0     // CAN protocol (0: CAN 2.0A, 1: CAN 2.0B)
#define MESSAGE_LENGTH    8     // Data length: 8 bytes
#define MESSAGE_RTR       0     // rtr bit

void setup() {

  DisplayInit();  

  pinMode(LED_ERR, OUTPUT);
  pinMode(LED_SHIFT, OUTPUT);
  pinMode(GAUGE_PIN, INPUT_PULLUP);
  
  allCan[0] = &can1512;
  allCan[1] = &can1513;
  allCan[2] = &can1514;
  allCan[3] = &can1515;
  allCan[4] = &can1516;

  #pragma region set allGauges
  allGauges[0] = &engine_map;
  allGauges[1] = &engine_rpm;
  allGauges[2] = &engine_clt;
  allGauges[3] = &engine_tps;
  allGauges[4] = &engine_pw1;
  allGauges[5] = &engine_pw2;
  allGauges[6] = &engine_iat;
  allGauges[7] = &engine_adv;
  allGauges[8] = &engine_tgt;
  allGauges[9] = &engine_afr;
  allGauges[10] = &engine_ego;
  allGauges[11] = &engine_egt;
  allGauges[12] = &engine_pws;
  allGauges[13] = &engine_bat;
  allGauges[14] = &engine_sr1;
  allGauges[15] = &engine_sr2;
  allGauges[16] = &engine_knk;
  allGauges[17] = &engine_vss;
  allGauges[18] = &engine_tcr;
  allGauges[19] = &engine_lct;
  #pragma endregion
  
  writeToDisplay("Waiting for ECU");
  canInit(500000);                        // Initialise CAN port - must be before Serial.begin
  Serial.begin(1000000);

  setCursorPosition(1,1);
  writeToDisplay("Connected to ECU");
  if(DEBUG) {
    Serial.println("Connected to ECU");
  }
  clearDisplay();
  delay(200);
  boot_animation();
}

void loop() {

  Serial.println(millis());
  load_from_can();
  Serial.println(millis());
  load_from_can();
  Serial.println(millis());
  load_from_can();
  Serial.println(millis());
  load_from_can();
  Serial.println(millis());
  load_from_can();



  //int canID = load_from_can();

  // bool got1512 = false;
  // bool got1513 = false;
  // bool got1514 = false;
  // bool got1515 = false;
  // bool got1516 = false;

  // while(got1512 == false || got1513 == false || got1514 == false || got1515 == false || got1516 == false)
  // {
  //   int canID = load_from_can();
  //   switch(canID) {
  //     case 1512:
  //       //if(got1512 == false) { Serial.println("1512"); }
  //       got1512 = true;
  //       break;
  //     case 1513:
  //       //if(got1513 == false) { Serial.println("1513"); }
  //       got1513 = true;
  //       break;
  //     case 1514:
  //       //if(got1514 == false) { Serial.println("1514"); }
  //       got1514 = true;
  //       break;
  //     case 1515:
  //       //if(got1515 == false) { Serial.println("1515"); }
  //       got1515 = true;
  //       break;
  //     case 1516:
  //       //if(got1516 == false) { Serial.println("1516"); }
  //       got1516 = true;
  //       break;
  //   }
  // }
  // Serial.println("LOOP DONE");
  


  currentMillis = millis();

  Serial.println(currentMillis);

  //draw display
  if(true) {
    if(currentMillis - lastDisplayMillis >= displayInterval && currentMillis > 500) {
      lastDisplayMillis = currentMillis;

        writeToDisplay("RPM:");
        writeToDisplay(engine_rpm.currentValue, engine_rpm.decimalPlaces, 1, 5);
        draw_bar(engine_rpm, 2, 1, 16);
    }
  }

  //check for errors
  if(false) {
    if(currentMillis - lastDiagnosticMillis >= diagnosticInterval && currentMillis > 500) {
      lastDiagnosticMillis = currentMillis;
      bool err = calculate_error_light();
      if(err != inError) {
        inError = err;
        digitalWrite(LED_ERR, err);  
      }
    }
  }
}

void resetCanVariables() {
  for(int i = 0; i < CAN_MESSAGE_COUNT; i++) 
  {
    allCan[i]->filled = false;
  }
}

int load_from_can() {
  clearBuffer(&canBuffer[0]);
  canMsg.cmd      = CMD_RX_DATA;
  canMsg.pt_data  = &canBuffer[0];
  canMsg.ctrl.ide = MESSAGE_PROTOCOL; 
  canMsg.id.std   = 0;
  canMsg.id.ext   = 0;
  canMsg.dlc      = MESSAGE_LENGTH;
  canMsg.ctrl.rtr = MESSAGE_RTR;


  

  // Wait for the command to be accepted by the controller
  while(can_cmd(&canMsg) != CAN_CMD_ACCEPTED);
  // Wait for command to finish executing
  while(can_get_status(&canMsg) == CAN_STATUS_NOT_COMPLETED);
  // Data is now available in the message object
  

  //serialPrintData(&canMsg);

  if(true)
  {
    switch(canMsg.id.std) {
      case 1512:
        engine_map.previousValue = engine_map.currentValue;
        engine_map.currentValue = ((canMsg.pt_data[0] * 256) + canMsg.pt_data[1]) / 10.0;
        increment_counter(&engine_map);

        engine_rpm.previousValue = engine_rpm.currentValue;
        //engine_rpm.currentValue = buf[2] * 256 + buf[3];
        //round rpm to nearest 10
        engine_rpm.currentValue = round((canMsg.pt_data[2] * 256 + canMsg.pt_data[3]) / 10.0) * 10.0;
        increment_counter(&engine_rpm);
        
        engine_clt.previousValue = engine_clt.currentValue;
        engine_clt.currentValue = (canMsg.pt_data[4] * 256 + canMsg.pt_data[5]) / 10.0;
        increment_counter(&engine_clt);
        
        engine_tps.previousValue = engine_tps.currentValue;
        engine_tps.currentValue = (canMsg.pt_data[6] * 256 + canMsg.pt_data[7]) / 10.0;
        increment_counter(&engine_tps);
        
        break;
      case 1513:
        engine_pw1.previousValue = engine_pw1.currentValue;
        engine_pw1.currentValue = (canMsg.pt_data[0] * 256 + canMsg.pt_data[1]) / 1000.0;
        increment_counter(&engine_pw1);

        engine_pw2.previousValue = engine_pw2.currentValue;
        engine_pw2.currentValue = (canMsg.pt_data[2] * 256 + canMsg.pt_data[3]) / 1000.0;
        increment_counter(&engine_pw2);

        engine_iat.previousValue = engine_iat.currentValue;
        engine_iat.currentValue = (canMsg.pt_data[4] * 256 + canMsg.pt_data[5]) / 10.0;
        increment_counter(&engine_iat);

        engine_adv.previousValue = engine_adv.currentValue;
        engine_adv.currentValue = (canMsg.pt_data[6] * 256 + canMsg.pt_data[7]) / 10.0;
        increment_counter(&engine_adv);
        
        break;
      case 1514:
        engine_tgt.previousValue = engine_tgt.currentValue;
        engine_tgt.currentValue = (double)canMsg.pt_data[0] / 10.0;
        increment_counter(&engine_tgt);

        engine_afr.previousValue = engine_afr.currentValue;
        engine_afr.currentValue = (double)canMsg.pt_data[1] / 10.0;
        increment_counter(&engine_afr);

        engine_ego.previousValue = engine_ego.currentValue;
        engine_ego.currentValue = (canMsg.pt_data[2] * 256 + canMsg.pt_data[3]) / 10.0;
        increment_counter(&engine_ego);

        engine_egt.previousValue = engine_egt.currentValue;
        engine_egt.currentValue = (canMsg.pt_data[4] * 256 + canMsg.pt_data[5]) / 10.0;
        increment_counter(&engine_egt);

        engine_pws.previousValue = engine_pws.currentValue;
        engine_pws.currentValue = (canMsg.pt_data[6] * 256 + canMsg.pt_data[7]) / 1000.0;
        increment_counter(&engine_pws);
        
        break;
      case 1515:
        engine_bat.previousValue = engine_bat.currentValue;
        engine_bat.currentValue = (canMsg.pt_data[0] * 256 + canMsg.pt_data[1]) / 10.0;
        increment_counter(&engine_bat);

        //not tested
        engine_sr1.previousValue = engine_sr1.currentValue;
        engine_sr1.currentValue = (canMsg.pt_data[2] * 256 + canMsg.pt_data[3]) / 10.0;
        increment_counter(&engine_sr1);

        //not tested
        engine_sr2.previousValue = engine_sr2.currentValue;
        engine_sr2.currentValue = (canMsg.pt_data[4] * 256 + canMsg.pt_data[5]) / 10.0;
        increment_counter(&engine_sr2);

        //not tested
        engine_knk.previousValue = engine_knk.currentValue;
        engine_knk.currentValue = (canMsg.pt_data[6] * 256) / 10.0;
        increment_counter(&engine_knk);

        break;
      case 1516:
        //not tested
        engine_vss.previousValue = engine_vss.currentValue;
        engine_vss.currentValue = (canMsg.pt_data[0] * 256 + canMsg.pt_data[1]) / 10.0;
        increment_counter(&engine_vss);

        //not tested
        engine_tcr.previousValue = engine_tcr.currentValue;
        engine_tcr.currentValue = (canMsg.pt_data[2] * 256 + canMsg.pt_data[3]) / 10.0;
        increment_counter(&engine_tcr);

        engine_lct.previousValue = engine_lct.currentValue;
        engine_lct.previousValue = (canMsg.pt_data[4] * 256 + canMsg.pt_data[5]) / 10.0;
        increment_counter(&engine_lct);
        
        break;
      default:
        //do nothing
        break;
    }
  }
  return canMsg.id.std;
}

void serialPrintData(st_cmd_t *msg){
  char textBuffer[50] = {0};
  if (msg->ctrl.ide>0){
    sprintf(textBuffer,"id %d ",msg->id.ext);
  }
  else
  {
    sprintf(textBuffer,"id %04x ",msg->id.std);
  }
  Serial.print(textBuffer);
  
  //  IDE
  sprintf(textBuffer,"ide %d ",msg->ctrl.ide);
  Serial.print(textBuffer);
  //  RTR
  sprintf(textBuffer,"rtr %d ",msg->ctrl.rtr);
  Serial.print(textBuffer);
  //  DLC
  sprintf(textBuffer,"dlc %d ",msg->dlc);
  Serial.print(textBuffer);
  //  Data
  sprintf(textBuffer,"data ");
  Serial.print(textBuffer);
  
  for (int i =0; i<msg->dlc; i++){
    sprintf(textBuffer,"%02X ",msg->pt_data[i]);
    Serial.print(textBuffer);
  }
  Serial.print("\r\n");
}

void increment_counter(EngineVariable* engine) {
  if(engine->currentValue > engine->maximum) {
    engine->highCount++;
  }
  else if(engine->currentValue < engine->minimum) {
    engine->lowCount++;
  }
  else {
    engine->goodCount++;
  }  
}

//note: using log10 would work but this is faster
bool is_current_value_shorter(EngineVariable engine) {
  int roundedCurrent = round(engine.currentValue);
  int roundedPrevious = round(engine.previousValue);
  byte currentLength;
  if(roundedCurrent >= 10000) {
    currentLength = 5;
  }
  else if(roundedCurrent >= 1000) {
    currentLength = 4;
  }
  else if(roundedCurrent >= 100) {
    currentLength = 3;
  }
  else if(roundedCurrent >= 10) {
    currentLength = 2;
  }
  else {
    currentLength = 1;
  }
  if(engine.decimalPlaces > 0) {
    currentLength++;
    currentLength+= engine.decimalPlaces;
  }

  byte previousLength;
  if(roundedPrevious >= 10000) {
    previousLength = 5;
  }
  else if(roundedPrevious >= 1000) {
    previousLength = 4;
  }
  else if(roundedPrevious >= 100) {
    previousLength = 3;
  }
  else if(roundedPrevious >= 10) {
    previousLength = 2;
  }
  else {
    previousLength = 1;
  }
  if(engine.decimalPlaces > 0) {
    previousLength++;
    previousLength += engine.decimalPlaces;
  }

  return currentLength < previousLength;
}

void draw_bar(EngineVariable engineVar, int row, int column, int maxLength) {
  
  int length = map(engineVar.currentValue, engineVar.minimum, engineVar.maximum, 0, maxLength);

  for(int i = 0; i < maxLength; i++) {
    if(i > length) {
      writeSpecialToDisplay(SPACE, row, column+i);
    }
    else {
      writeSpecialToDisplay(BLOCK, row, column+i);
    }
  }
}

bool calculate_error_light() {
  byte len = 20; //sizeof(allGauges);
  unsigned long badCount;
  unsigned long totalCount;
  byte percent;
  bool inError = false;
  
  for(byte i = 0; i < len; i++) {
    badCount = allGauges[i]->lowCount + allGauges[i]->highCount;
    totalCount = badCount + allGauges[i]->goodCount;
    percent = badCount * 100 / totalCount;
    if(percent > 4) {
      inError = true;
    }
    if(totalCount > 1000) {
      allGauges[i]->lowCount = allGauges[i]->lowCount * 0.8;
      allGauges[i]->highCount = allGauges[i]->highCount * 0.8;
      allGauges[i]->goodCount = allGauges[i]->goodCount * 0.8;
    }
  }
  return inError;
}

void boot_animation() {

  int smallDelay = 30;
  int bigDelay = 200;

  writeSpecialToDisplay(BLOCK, 1, 1);
  writeSpecialToDisplay(BLOCK, 2, 16);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 2);
  writeSpecialToDisplay(BLOCK, 2, 15);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 3);
  writeSpecialToDisplay(BLOCK, 2, 14);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 4);
  writeSpecialToDisplay(BLOCK, 2, 13);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 5);
  writeSpecialToDisplay(BLOCK, 2, 12);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 6);
  writeSpecialToDisplay(BLOCK, 2, 11);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 7);
  writeSpecialToDisplay(BLOCK, 2, 10);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 8);
  writeSpecialToDisplay(BLOCK, 2, 9);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 9);
  writeSpecialToDisplay(BLOCK, 2, 8);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 10);
  writeSpecialToDisplay(BLOCK, 2, 7);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 11);
  writeSpecialToDisplay(BLOCK, 2, 6);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 12);
  writeSpecialToDisplay(BLOCK, 2, 5);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 13);
  writeSpecialToDisplay(BLOCK, 2, 4);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 14);
  writeSpecialToDisplay(BLOCK, 2, 3);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 15);
  writeSpecialToDisplay(BLOCK, 2, 2);
  delay(smallDelay);
  writeSpecialToDisplay(BLOCK, 1, 16);
  writeSpecialToDisplay(BLOCK, 2, 1);

  delay(bigDelay);

  writeSpecialToDisplay(SPACE, 1, 1);
  writeSpecialToDisplay(SPACE, 2, 16);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 2);
  writeSpecialToDisplay(SPACE, 2, 15);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 3);
  writeSpecialToDisplay(SPACE, 2, 14);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 4);
  writeSpecialToDisplay(SPACE, 2, 13);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 5);
  writeSpecialToDisplay(SPACE, 2, 12);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 6);
  writeSpecialToDisplay(SPACE, 2, 11);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 7);
  writeSpecialToDisplay(SPACE, 2, 10);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 8);
  writeSpecialToDisplay(SPACE, 2, 9);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 9);
  writeSpecialToDisplay(SPACE, 2, 8);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 10);
  writeSpecialToDisplay(SPACE, 2, 7);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 11);
  writeSpecialToDisplay(SPACE, 2, 6);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 12);
  writeSpecialToDisplay(SPACE, 2, 5);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 13);
  writeSpecialToDisplay(SPACE, 2, 4);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 14);
  writeSpecialToDisplay(SPACE, 2, 3);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 15);
  writeSpecialToDisplay(SPACE, 2, 2);
  delay(smallDelay);
  writeSpecialToDisplay(SPACE, 1, 16);
  writeSpecialToDisplay(SPACE, 2, 1);

}