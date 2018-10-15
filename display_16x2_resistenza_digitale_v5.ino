/* Created by Davide Caminati 2018 */
/* TODO
 *  improve linearization with some point in the middle (127) ??
 *  save in eprom the calibration
 *  temperature compensation
 *  current optimization (select nearest resistors values)
 */

#include <AnalogMultiButton.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <Print.h>
// select the pins used on the LCD panel
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

const int BUTTONS_PIN = A0;
const int BUTTONS_TOTAL = 5;
const int BUTTONS_VALUES[BUTTONS_TOTAL] = {0, 100, 250, 400, 650};
const int BUTTON_RIGHT = 0;
const int BUTTON_UP = 1;
const int BUTTON_DOWN = 2;
const int BUTTON_LEFT = 3;
const int BUTTON_SELECT = 4;
int DEVICE_R_min = 100;
int DEVICE_R_max = 4000;
float DEVICE_tollerance = 0.6;
bool DEVICE_optimize_for_current = false ;
int cursor_index = 0;

int R1_min_step_index = 0;  //# start step
int R1_max_step_index = 0;  //# NOTE R1_max_step_index depends on value of R2_min_step_index
int R2_min_step_index = 0;  //# 
int R2_max_step_index = 0;  //# NOTE R2_max_step_index depends on value of R1_min_step_index
float R_equiv = 4000.0;

AnalogMultiButton buttons(BUTTONS_PIN, BUTTONS_TOTAL, BUTTONS_VALUES);

byte address = 0x11; // B00010001
int CS1 = A1;
int CS2 = A2;
float MCP_1_min_val = 75.0;
float MCP_1_max_val = 8999.0;
float MCP_2_min_val = 175.0;
float MCP_2_max_val = 9120.0;
int valore1 = 0;
int valore2 = 0;

#define  MCP_steps  256
String tempo = "";
int Arr_MCP_1_R[MCP_steps];
int Arr_MCP_2_R[MCP_steps];

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  SPI.begin();
  pinMode (CS1, OUTPUT);
  pinMode (CS2, OUTPUT);
  SetSelectionCursor();
  set_value(DEVICE_R_max);
  // bug , set_value seems don't works on SETUP
  digitalPotWrite(203,253);
}


void loop() {
  buttons.update();
  // MOVE LEFT RIGHT - STEP VALUE 
  if(buttons.onPress(BUTTON_LEFT))
  {
    MoveCursor_Left();
  }
  if(buttons.onPress(BUTTON_RIGHT))
  {
    MoveCursor_Right();
  }

  if(buttons.onPressAfter(BUTTON_SELECT, 1000))
  {
    R_equiv = DEVICE_R_min;
    set_value(R_equiv);
    //avvio_ricerca(R_equiv);
  }
  
  // ROLL VALUE
  if(buttons.onPressAndAfter(BUTTON_UP, 1000, 500)) {
    int new_value = R_equiv;
    switch (cursor_index){
      case 0:
        new_value += 1000;
      break;
      case 1:
        new_value += 100;
      break;
      case 2:
        new_value += 10;
      break;
      case 3:
        new_value += 1;
      break;
    }
    if (new_value <= DEVICE_R_max){
      R_equiv = new_value;
      set_value(R_equiv);
    }
  }
    
  if(buttons.onPressAndAfter(BUTTON_DOWN, 1000, 500)) {
    int new_value = R_equiv;
    switch (cursor_index){
      case 0:
        new_value -= 1000;
      break;
      case 1:
        new_value -= 100;
      break;
      case 2:
        new_value -= 10;
      break;
      case 3:
        new_value -= 1;
      break;
    }
    if (new_value >= DEVICE_R_min){
      R_equiv = new_value;
      set_value(R_equiv);
      //avvio_ricerca(actual_value);
    }
  }
  delay(10);
}

void Popolate_array(int arr[],float R_min,float R_max){
    for (int v = 0; v < MCP_steps; v++){
        arr[v] = R_min + ((R_max-R_min)/(MCP_steps-1)) * (v);
    }
}

int get_min_index(int arr_MCP[]){
    for (int i = 255; i > 0; i--){
        if (arr_MCP[i] < R_equiv){
            return i+1;
        }
    }
    Serial.println("error get_min_index");
    return 0;
}

int get_max_index(int this_resistor_arr[],int other_resistor_arr_MCP[],int other_resistor_min_step_index){
    float r_fix = other_resistor_arr_MCP[other_resistor_min_step_index]; // other resistor value
    float max_value_1 = (R_equiv+1) * r_fix;
    float max_value_2 = abs((R_equiv+1) - r_fix);
    double max_value = ((R_equiv+1) * r_fix) / abs(((R_equiv+1) - r_fix)); //Note R_equiv+1 to take some margin
    for (int i = 0;i < MCP_steps; i++){
        if (this_resistor_arr[i] > max_value){
            return i;
        }
    }
    Serial.println("error get_max_index");
    return MCP_steps-1;
}

void converge(float resistors_pair_index[],int arr_MCP_A[],int min_step_index_A,int max_step_index_A,int arr_MCP_B[],int min_step_index_B,int max_step_index_B){
    int _min_step_index_A = min_step_index_A;
    int _max_step_index_A = max_step_index_A;
    bool esci = false;
    for ( int v_A = _min_step_index_A;v_A < _max_step_index_A;v_A++){
       float val_A = arr_MCP_A[v_A];
      if (esci == true){
        break;
      }
      for (int  v_B = min_step_index_B; v_B < max_step_index_B; v_B++){
        float  val_B = arr_MCP_B[v_B];
        float moltiplicazione = val_A*val_B;
        
        float somma = val_A+val_B;
        float test_val = moltiplicazione/somma;
        if (abs(R_equiv-test_val) < DEVICE_tollerance){
          resistors_pair_index[0] = v_A;
          resistors_pair_index[1] = v_B;
          resistors_pair_index[2] = test_val;
          esci = true;
          break;
        }
      }
    }
}
    
void set_value(int val){
// DISPLAY
    String valStr = "";
    char *result = malloc(5);
    sprintf(result, "%04d", val);
    lcd.setCursor(0,0);
    lcd.print(result);
    R_equiv = val;
//MCP
  R1_min_step_index = get_min_index(Arr_MCP_1_R);
  R2_min_step_index = get_min_index(Arr_MCP_2_R);
  R1_max_step_index = get_max_index(Arr_MCP_1_R,Arr_MCP_2_R,R2_min_step_index);
  R2_max_step_index = get_max_index(Arr_MCP_2_R,Arr_MCP_1_R,R1_min_step_index);
  Popolate_array(Arr_MCP_1_R,MCP_1_min_val,MCP_1_max_val);
  Popolate_array(Arr_MCP_2_R,MCP_2_min_val,MCP_2_max_val);
  float p[4];
  converge(p,Arr_MCP_1_R,R1_min_step_index,R1_max_step_index,Arr_MCP_2_R,R2_min_step_index,R2_max_step_index);
  int a = p[0];
  int b = p[1];

  digitalPotWrite(p[0],p[1]);
  lcd.setCursor(6,0);
  lcd.print("     ");
  lcd.setCursor(6,0);
  lcd.print(Arr_MCP_1_R[a]);
  lcd.setCursor(6,1);
  lcd.print("     ");
  lcd.setCursor(6,1);
  lcd.print(Arr_MCP_2_R[b]);
  }


void  digitalPotWrite(int value1,int value2)
{
  digitalWrite(CS1, 0);
  SPI.transfer(address);
  SPI.transfer(value1);
  digitalWrite(CS1, 255);
  delay(10);
  digitalWrite(CS2, 0);
  SPI.transfer(address);
  SPI.transfer(value2);
  digitalWrite(CS2, 255);
  delay(10);
}


void MoveCursor_Left(){
  if (cursor_index > 0){
    cursor_index -=1;
  }
  SetSelectionCursor();
}

void MoveCursor_Right(){
  if (cursor_index < 3){
    cursor_index +=1;
  }
  SetSelectionCursor();
}

void SetSelectionCursor(){
  String arrowStr = "";
  for (int i = 0;i < 5;i++){
    if (i == cursor_index){
      arrowStr += "^";
    }
    else{
      arrowStr += " ";
    }
    lcd.setCursor(0,1);
    lcd.print(arrowStr);
  }
}
