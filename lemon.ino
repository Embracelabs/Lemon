#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include "RTClib.h"
#include <SD.h>
#include <LiquidCrystal.h>
#include <avr/sleep.h>

#define SD_SELECT              10
#define VBUS_EN_PIN            A0
#define LCD_POWER_PIN          A1      
#define BUZZER                 A2
#define RESET                  A3
#define RTC_INT_PIN            0    //PIN2
#define SWITCH_INT_PIN         1    //PIN3
#define ADC_FAULT              32000
#define LOG_INTERVAL           4    //1 less than the required interval
#define LCD_VIEW_INTERVAL      7
#define AMB_VIEW_INTERVAL      3
#define LCD_ON_DURATION        (LCDViewCount<LCD_VIEW_INTERVAL)
#define AMB_ON_DURATION        (LCDAmbTempCount<AMB_VIEW_INTERVAL)
#define VBUS_EN                digitalWrite(VBUS_EN_PIN, LOW);
#define VBUS_DIS               digitalWrite(VBUS_EN_PIN, HIGH);
#define LCD_EN                 digitalWrite(LCD_POWER_PIN, LOW);
#define LCD_DIS                digitalWrite(LCD_POWER_PIN, HIGH);
#define PASS                   1
#define FAIL                   0
#define AVERAGING              30

Adafruit_ADS1115 ads1115(0x48);
RTC_DS1307 rtc;
Sd2Card card;
LiquidCrystal lcd(8,9,4,5,6,7);
DateTime now,rtcStart;
volatile char sleepCount=0, LCDViewCount=0, LCDAmbTempCount=AMB_VIEW_INTERVAL;
volatile boolean rtcInterrupt=LOW, switchInterrupt=LOW;

const unsigned int lookupTableBaby[] = {14494,14218,13938,13663,13390,13115,12844,12573,12307,12037,11775};
//                                         32,   33,   34,   35,   36,   37,   38,   39,   40,   41,   42

void checkSD(){
  boolean sdState = PASS;
  while(!card.init(SPI_QUARTER_SPEED, SD_SELECT)) {
    digitalWrite(BUZZER, HIGH); delay(100); digitalWrite(BUZZER, LOW); sdState = FAIL;
  }
  sdState?digitalWrite(RESET,HIGH):digitalWrite(RESET,LOW);
}

boolean checkBabySensor(){return((getBabyAdc()>ADC_FAULT) ? FAIL : PASS);}
boolean checkAmbSensor(){return(getAmbAdc()>ADC_FAULT ? FAIL : PASS);}
unsigned int getBabyAdc(){return ads1115.readADC_SingleEnded(0);}
unsigned int getAmbAdc(){return map(ads1115.readADC_SingleEnded(1),0,26666,0,1024);}
unsigned int getBatVolAdc(){return ads1115.readADC_SingleEnded(2);}
  
String getBabyTemp(){
  unsigned int intBabyTemp=0, adcBaby=getBabyAdc(), i=0, index=0;
  int temp=0;
  for(i=0;i<AVERAGING;i++) {temp += (getBabyAdc()-adcBaby);} adcBaby = adcBaby + temp/AVERAGING;
  if(adcBaby<=14494 && adcBaby>=11775){
    while(lookupTableBaby[++index] > adcBaby);
    intBabyTemp = (3200+index*100 - ((adcBaby-lookupTableBaby[index])*100/(lookupTableBaby[index-1]-lookupTableBaby[index])));
    return(((intBabyTemp/100<10)?"0"+String(intBabyTemp/100):String(intBabyTemp/100)) + "." + ((intBabyTemp%100<10)?"0"+String(intBabyTemp%100):String(intBabyTemp%100)));
  }
  else return ("-----");
}

String getAmbTemp(){
  float rTemp=0.0, sensorTemp=0.0;
  unsigned int intAmbTemp, adcAmb=0, i=0;
  for(i=0;i<AVERAGING;i++) {adcAmb += getAmbAdc();} adcAmb /= AVERAGING;
  rTemp=(1024.0/adcAmb)-1.0;
  rTemp=100000.0/rTemp;
  sensorTemp=log(rTemp/50750.0);
  sensorTemp=(3953.0*310.15)/((310.15*sensorTemp)+3953.0);
  sensorTemp=sensorTemp-273.15;	
  intAmbTemp = sensorTemp*10;
  return(String(intAmbTemp/10) + "." + String(intAmbTemp%10));
}

String d(DateTime val){return((val.day()<10)?"0"+String(val.day()):String(val.day()));}
String m(DateTime val){return((val.month()<10)?"0"+String(val.month()):String(val.month()));}
String y(DateTime val){return((val.year()%100<10)?"0"+String(val.year()%100):String(val.year()%100));}
String h(DateTime val){return((val.hour()<10)?"0"+String(val.hour()):String(val.hour()));}
String i(DateTime val){return((val.minute()<10)?"0"+String(val.minute()):String(val.minute()));}
String s(DateTime val){return((val.second()<10)?"0"+String(val.second()):String(val.second()));}

String date(DateTime val){return(d(val) + "/" + m(val) + "/" + y(val));}
String time(DateTime val){return(h(val) + ":" + i(val) + ":" + s(val));}
String fileName(DateTime val){return(h(val) + i(val) + s(val) + ".csv");}
String folderName(DateTime val){return(y(val) + m(val) + d(val));}
String path(DateTime val){return(folderName(val) + "/" + fileName(val));}

void logData(){
  now = rtc.now();
  File dataFile;
  char temp[18];
  path(rtcStart).toCharArray(temp, 18);
  dataFile = SD.open(temp, FILE_WRITE);
  if(dataFile){
    dataFile.println(date(now)+","+time(now)+","+getBabyTemp()+","+getAmbTemp());
    dataFile.close();
    Serial.println(date(now)+","+time(now)+","+getBabyTemp()+","+getAmbTemp());
  }
  else {checkSD(); Serial.println("Unable to open file");}
  sleepCount = 0;
}

void createFolder(DateTime val){
  char temp[7];
  folderName(val).toCharArray(temp, 7);
  SD.mkdir(temp);
}

void displayLCD(DateTime val, boolean b){
  LCD_EN;
  //delay(10);
  lcd.setCursor(0,0); lcd.print(time(val));
  lcd.setCursor(0,1);
  if(b == 0){lcd.print(F("Baby Temp ")); lcd.print(getBabyTemp());}
  else if(b == 1) {lcd.print(F("Ambt Temp ")); lcd.print(getAmbTemp());lcd.setCursor(14,1);lcd.print(" ");}
  
  lcd.setCursor(15,0);
  int unsigned temp = getBatVolAdc();
  if(temp >= 614){lcd.write(4);}
  else if(temp >= 512){lcd.write(3);}
  else if(temp >= 409){lcd.write(2);}
  else if(temp >= 307){lcd.write(1);}
}

void sleepNow(){
  VBUS_DIS;
  delay(10);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);   // sleep mode is set here
  sleep_enable();          // enables the sleep bit in the mcucr register
  sleep_mode();            // here the device is actually put to sleep!!
  sleep_disable();         // first thing after waking from sleep
}

void wakeUpNow(){
  rtcInterrupt = HIGH;
  sleepCount++; 
  if(LCD_ON_DURATION) LCDViewCount++;
  if(AMB_ON_DURATION) LCDAmbTempCount++;
}
void sw(){
  switchInterrupt = HIGH;
  if(LCD_ON_DURATION) LCDAmbTempCount = 0;
  LCDViewCount = 0;
}

void setup()
{
  pinMode(SD_SELECT, OUTPUT);
  pinMode(VBUS_EN_PIN, OUTPUT);
  pinMode(LCD_POWER_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RESET, OUTPUT);
  attachInterrupt(RTC_INT_PIN, wakeUpNow, RISING);
  attachInterrupt(SWITCH_INT_PIN, sw, RISING);
 

  VBUS_EN;
  LCD_EN;
  digitalWrite(RESET, HIGH);
  digitalWrite(BUZZER, LOW);
  
    Serial.begin(57600);
  Wire.begin();
  rtc.begin();
  lcd.begin(16,2); lcd.clear();
  SD.begin(SD_SELECT);

  
  byte bat4[8] = {B00100,B11111,B11111,B11111,B11111,B11111,B11111};
  byte bat3[8] = {B00100,B11111,B10001,B11111,B11111,B11111,B11111};
  byte bat2[8] = {B00100,B11111,B10001,B10001,B11111,B11111,B11111};
  byte bat1[8] = {B00100,B11111,B10001,B10001,B10001,B11111,B11111};
  lcd.createChar(4, bat4);
  lcd.createChar(3, bat3);
  lcd.createChar(2, bat2);
  lcd.createChar(1, bat1);
  
  rtcStart = rtc.now();
  createFolder(rtcStart);
  logData();
}

void loop(void) {
  now = rtc.now();
  
  if(rtcInterrupt == HIGH){
    rtcInterrupt = LOW;
    VBUS_EN;
    if(sleepCount == LOG_INTERVAL) logData();
    if(LCD_ON_DURATION && AMB_ON_DURATION) displayLCD(now,1);
    if(LCD_ON_DURATION && !AMB_ON_DURATION) displayLCD(now,0);
    if(!LCD_ON_DURATION && !AMB_ON_DURATION) {LCD_DIS; sleepNow();}
    //if(switchInterrupt == HIGH){switchInterrupt = LOW; lcd.clear();}
  }
}
