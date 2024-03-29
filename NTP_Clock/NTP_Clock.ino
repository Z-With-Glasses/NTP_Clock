#include <Wire.h>
#include <M5StickCPlus.h>
#include <Streaming.h>
#include <time.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>

RTC_TimeTypeDef RTC_TimeStruct;
RTC_DateTypeDef RTC_DateStruct;

const unsigned long eventInterval = 5000;//timer for screen timeout in miliseconds
static unsigned long millisPrevious = 0;
static unsigned long millisElapsed = 0;
uint8_t previousHours{};
static uint8_t previousMinutes{};
static uint8_t previousSeconds{};

static float voltage{};//AXP192 uses floats here, so I do as well
static float voltageIn{};//AXP192 uses floats here, so I do as well
static int batteryPercentInTens{};
static float voltageCurrent{};
uint32_t xtalCrystalFrequency{getXtalFrequencyMhz()};
uint32_t cpuClockRate{getCpuFrequencyMhz()};
uint32_t apbBusClockRate{getApbFrequency()};

int testingFunctionStatus{};
static bool displayChange = true;//starts true, set to false on screen change to clear screen
static bool screenStatus = true ;
bool chargingStatus = false;

float pitch = 0.0F;
float roll = 0.0F;
float yaw = 0.0F;//uesless without magnetometer but needed for getAhrsData()


const char* ssid      = "SSID";
const char* password  = "PASS";
const char* ntpServer = "time.nist.gov";
const long gmtOffset_sec = -28800;
const int daylightOffset_sec = 3600;

const int port = 38899;
const char * ipAddress = "10.0.0.227";
const char lightOn[] = "{\"method\":\"setState\",\"params\":{\"state\":true}}";
const char lightOff[] = "{\"method\":\"setState\",\"params\":{\"state\":false}}";
//const char lightOn[] = "{\"id\":1,\"method\":\"setPilot\",\"params\":{\"r\":0,\"g\":255,\"b\":0,\"dimming\":100}}";//blue

void timeSync(){
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("Connecting to %s...", ssid);
    WiFi.begin(ssid, password);  // Connect wifi and return connection status.
    millisPrevious = millisElapsed;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        M5.Lcd.print(".");
        millisElapsed = millis();
        if (millisElapsed - millisPrevious >= 10000)
        {
            M5.Lcd.fillScreen(TFT_BLACK);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.print("Failed to connect.");
            WiFi.disconnect(true);  // Disconnect wifi.  
            WiFi.mode(WIFI_OFF);  // Set the wifi mode to off.
            delay (1000);
            return;
        }
    }
    M5.Lcd.println("CONNECTED!");
    M5.Lcd.println("Syncing time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // init and get the time.   
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) //fail if unable to obtain
    {
        M5.Lcd.println("Failed to obtain time.");
        delay (1000);
        return;
    }
    RTC_TimeTypeDef TimeStruct;
    TimeStruct.Hours   = timeInfo.tm_hour;
    TimeStruct.Minutes = timeInfo.tm_min;
    TimeStruct.Seconds = timeInfo.tm_sec;
    M5.Rtc.SetTime(&TimeStruct);///set time to rtc so wifi can be disabled
    RTC_DateTypeDef DateStruct;
    DateStruct.WeekDay = timeInfo.tm_wday;
    DateStruct.Month = timeInfo.tm_mon + 1;
    DateStruct.Date = timeInfo.tm_mday;
    DateStruct.Year = timeInfo.tm_year + 1900;
    M5.Rtc.SetData(&DateStruct);
    M5.Lcd.println("Done!");
    M5.Lcd.println("Disconnecting Wifi.");
    delay(1000);
    WiFi.disconnect(true);  // Disconnect wifi.  
    WiFi.mode(WIFI_OFF);  // Set the wifi mode to off.
}
void turnScreenOn(){
    setCpuFrequencyMhz(80);
    millisPrevious = millisElapsed;
    Wire1.beginTransmission(0x34);
    Wire1.write(0x12);
    Wire1.write(0x4d); // Enable LDO2, aka OLED_VDD
    Wire1.endTransmission();
    screenStatus = true;
    displayChange = true;
}
void turnScreenOff(){
    setCpuFrequencyMhz(10);
    Wire1.beginTransmission(0x34);
    Wire1.write(0x12);
    Wire1.write(0x4B);  // Disable LDO2, aka OLED_VDD
    Wire1.endTransmission();
    screenStatus = false;
    M5.Axp.SetSleep();
}
void batteryStatus(){
    if ((voltage >= 4.11) && (voltage <= 4.2))//90-100%
        batteryPercentInTens = 10;
    if ((voltage >= 4.02) && (voltage <= 4.1099))//80-89%
        batteryPercentInTens = 9;
    if ((voltage >= 3.95) && (voltage <= 4.0199))//70-79%
        batteryPercentInTens = 8;
    if ((voltage >= 3.87) && (voltage <= 3.9499))//60-69%
        batteryPercentInTens = 7;
    if ((voltage >= 3.84) && (voltage <= 3.8699))//50-59%
        batteryPercentInTens = 6;
    if ((voltage >= 3.8) && (voltage <= 3.8399))//40-49%
        batteryPercentInTens = 5;
    if ((voltage >= 3.77) && (voltage <= 3.799))//30-39%
        batteryPercentInTens = 4;
    if ((voltage >= 3.73) && (voltage <= 3.7699))//20-29%
        batteryPercentInTens = 3;
    if ((voltage >= 3.69) && (voltage <= 3.7299))//10-19%
        batteryPercentInTens = 2;
    if (voltage <= 3.6899)//0-9%
        batteryPercentInTens = 1;
}
void displayCharging(){
    M5.Lcd.setTextSize(3);
    if (displayChange)
    {
        M5.Lcd.setTextColor(TFT_GREEN);
        M5.Lcd.setCursor(12, 105); 
        M5.Lcd.print("[          ]");
    }
    M5.Lcd.setTextColor(TFT_BLACK);
    M5.Lcd.setCursor(30, 105);
    M5.Lcd.print("<<<<<<<<<<");//clear battery display from bar
    M5.Lcd.setCursor(32, 105);
    M5.Lcd.print(">>>>>>>>>>");//clear charging display from bar
    M5.Lcd.setTextColor(TFT_GREEN);
    M5.Lcd.setCursor(32, 105);
    float chargingDelay = ((1.0/batteryPercentInTens)*1000);
    for (int it = 0; it < batteryPercentInTens; it++)//prints > once for each 10% of charge in battery
      {
        delay(chargingDelay);
        M5.Lcd.print('>');
      }
}
void displayBattery(){  
    M5.Lcd.setCursor(12, 105);
    M5.Lcd.setTextSize(3);
    if (batteryPercentInTens == 10)//90-100%
    {
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<<<<]");      
    }
    if (batteryPercentInTens == 9)//80-89%
    {
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<<< ]");      
    }
    if (batteryPercentInTens == 8)//70-79%
    {
        M5.Lcd.setTextColor(TFT_GREENYELLOW,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<<  ]");       
    }
    if (batteryPercentInTens == 7)//60-69%
    {
        M5.Lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<   ]");      
    }
    if (batteryPercentInTens == 6)//50-59%
    {
        M5.Lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
        M5.Lcd.print("[<<<<<<    ]");        
    }
    if (batteryPercentInTens == 5)//40-49%
    {
        M5.Lcd.setTextColor(TFT_ORANGE,TFT_BLACK);
        M5.Lcd.print("[<<<<<     ]");         
    }
    if (batteryPercentInTens == 4)//30-39%
    {
        M5.Lcd.setTextColor(TFT_ORANGE,TFT_BLACK);
        M5.Lcd.print("[<<<<      ]");         
    }
    if (batteryPercentInTens == 3)//20-29%
    {
        M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
        M5.Lcd.print("[<<<       ]");         
    }
    if (batteryPercentInTens == 2)//10-19%
    {
        M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
        M5.Lcd.print("[<<        ]");        
    }
    if (batteryPercentInTens == 1)//0-9%
    {
        M5.Lcd.setTextColor(TFT_MAROON,TFT_BLACK);
        M5.Lcd.print("[<         ]");         
    }
}
void displayTimeAndDate(){
    if (displayChange)
    M5.Lcd.fillScreen(TFT_BLACK);//clear screen    
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
    M5.Rtc.GetTime(&RTC_TimeStruct);
    M5.Rtc.GetData(&RTC_DateStruct);
    String date;
    String time;
    date += RTC_DateStruct.Year; date += '-'; date += RTC_DateStruct.Month; date += '-'; date += RTC_DateStruct.Date;
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.drawString(date, ((M5.Lcd.width()/2)-(M5.Lcd.textWidth(date)/2)), 0, 1);
    //M5.Lcd.printf("%04d-%02d-%02d", RTC_DateStruct.Year, RTC_DateStruct.Month, RTC_DateStruct.Date);
    //M5.Lcd.setTextDatum(TC_DATUM);
    int dayOfWeekInt = (RTC_DateStruct.WeekDay);
    String dayOfWeek = "";
    switch(dayOfWeekInt)
    {
        case 0:{
        dayOfWeek = "Sunday";}
        break;
        case 1:{
        dayOfWeek = "Monday";}
        break;
        case 2:{
        dayOfWeek = "Tuesday";}
        break;
        case 3:{
        dayOfWeek = "Wednesday";}
        break;
        case 4:{
        dayOfWeek = "Thursday";}
        break;
        case 5:{
        dayOfWeek = "Friday";}
        break;
        case 6:{
        dayOfWeek = "Saturday";}
        break;
    }
    if (previousHours != RTC_TimeStruct.Hours || displayChange)//if hours has changed
    {
        M5.Lcd.setCursor(48, 30);
        M5.Lcd.setTextColor(TFT_BLACK,TFT_BLACK);
        M5.Lcd.printf("%02d:",previousHours);//print over previous hour with black
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.setCursor(48, 30);
        M5.Lcd.printf("%02d:",RTC_TimeStruct.Hours);//print new hour in green
        previousHours=RTC_TimeStruct.Hours;//assign current hour to previous hour
    }
    if (previousMinutes != RTC_TimeStruct.Minutes || displayChange);//if minutes has changed
    {
        M5.Lcd.setCursor(102, 30);
        M5.Lcd.setTextColor(TFT_BLACK,TFT_BLACK);
        M5.Lcd.printf("%02d:",previousMinutes);
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.setCursor(102, 29);
//int cursorPositionMinutes = m5.Lcd.getCursorX();//get minutes cursor position
//Serial << "minutes cursor-" << cursorPositionMinutes << '\n';//print minutes cursor postion to serial
        M5.Lcd.printf("%02d:",RTC_TimeStruct.Minutes);
        previousMinutes=RTC_TimeStruct.Minutes;
    }
    if (previousSeconds != RTC_TimeStruct.Seconds || displayChange);//if seconds has changed
    {
        M5.Lcd.setCursor(156, 30);
        M5.Lcd.setTextColor(TFT_BLACK,TFT_BLACK);
        M5.Lcd.printf("%02d",previousSeconds);
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.setCursor(156, 30);
//int cursorPositionSeconds = m5.Lcd.getCursorX();//get seconds cursor position
//Serial << "seconds cursor-" << cursorPositionSeconds << '\n';//print seconds cursor postion to serial
        M5.Lcd.printf("%02d",RTC_TimeStruct.Seconds);    
        previousSeconds=RTC_TimeStruct.Seconds;
    }
    M5.Lcd.drawString(dayOfWeek, ((M5.Lcd.width()/2)-(M5.Lcd.textWidth(dayOfWeek)/2)), 60, 1);
}
WiFiUDP Udp;
void lightControl(bool onOff)
{
    millisPrevious = millisElapsed;
    M5.Lcd.println("Connecting to WiFi.");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) 
    {
      delay(250);
      M5.Lcd.print(".");
      millisElapsed = millis();
      if (millisElapsed - millisPrevious >= 10000)
      {
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("Failed to connect.");
        WiFi.disconnect(true);  // Disconnect wifi.  
        WiFi.mode(WIFI_OFF);  // Set the wifi mode to off.
        delay (1000);
        return;
       }
    }
    M5.Lcd.println("\nSending command.");
    Udp.beginPacket(ipAddress, port);
    if(onOff == true){
      for (int i = 0; i <= strlen(lightOn); i++)
        {
        Udp.write(lightOn[i]);
        }
    }else{
      for (int i = 0; i <= strlen(lightOff); i++)
        {
        Udp.write(lightOff[i]);
        }
    }
    Udp.endPacket();
    M5.Lcd.println("Command sent.");
    delay(3000);
    M5.Lcd.println("Disconnecting...");
    WiFi.disconnect(true);  // Disconnect wifi.  
    WiFi.mode(WIFI_OFF);  // Set the wifi mode to off.
    delay(3000);
}
void lightControlScreen()
{
    M5.update(); //updates button status
    bool isRunning = true;
    M5.Lcd.fillScreen(TFT_BLACK);//clear screen
    M5.Lcd.setTextSize(2); 
    M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Lcd.setCursor(0,0);
    M5.Lcd.println("Light On/Off");
    while(isRunning)
    {
      M5.update(); //updates button status
      if (M5.BtnA.isPressed())//light on
      {
        lightControl(true);
        isRunning = false;
      }
      if (M5.BtnB.isPressed())//light off
      {
        lightControl(false);
        isRunning = false;
      }
      if (M5.BtnB.pressedFor(2000))
      {
        isRunning=false;
        testingFunctionStatus = 0;
      }
    }
}
void testingFunction(){      
    Serial << "voltage: " << voltage << '\n';//battery voltage
    Serial << "voltage input: " << voltageIn << '\n';//battery voltage input
    Serial << "previous millis: " <<  millisPrevious << '\n';
    Serial << "elapsed millis: " <<  millisElapsed << '\n';
    Serial << "charging status: " << chargingStatus << '\n';//0=not charging 1=charging
    Serial << "screen status:" << screenStatus << '\n';//0=off 1=on
    Serial << "battery percent rounded up to nearest ten: " << batteryPercentInTens << "0%" << '\n';
    Serial << "battery current: " << voltageCurrent << '\n';//negative value=discharge, positive=charge, 0= full charge
    Serial << "XTAL: " << xtalCrystalFrequency <<'\n';//XTAL frequency in MHz
    Serial << "CPU clock rate: " << cpuClockRate << '\n';//CPU clock rate in MHz
    Serial << "APB bus clock rate: " << apbBusClockRate << '\n';//APB bus clock rate in Hz 
    if (testingFunctionStatus == 2)//if screen printing is enabled
    {
        delay(100);
        voltage = (M5.Axp.GetBatVoltage());
        batteryStatus();
        M5.Lcd.fillScreen(TFT_BLACK);//clear screen
        M5.Lcd.setTextSize(1); 
        M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
        M5.Lcd.setCursor(0,0);
        M5.Lcd.print("voltage: ");
        M5.Lcd.println(voltage);
        M5.Lcd.print("voltage input: ");
        M5.Lcd.println(voltageIn);
        M5.Lcd.print("previous millis: ");
        M5.Lcd.println(millisPrevious);
        M5.Lcd.print("elapsed millis: ");
        M5.Lcd.println(millisElapsed);
        M5.Lcd.print("charging status: ");
        M5.Lcd.println(chargingStatus);
        M5.Lcd.print("battery % in 10's: ");
        M5.Lcd.println(batteryPercentInTens);
        M5.Lcd.print("battery current: ");
        M5.Lcd.println(voltageCurrent);
        M5.Lcd.print("XTAL: ");
        M5.Lcd.println(xtalCrystalFrequency);
        M5.Lcd.print("CPU clock rate: ");
        M5.Lcd.println(cpuClockRate);
        M5.Lcd.print("APB bus clock rate: ");
        M5.Lcd.println(apbBusClockRate);
        M5.Lcd.printf("Pitch: %5.2f    Roll: %5.2f", pitch, roll);
        //M5.Lcd.print("PIR input- ");
        //M5.Lcd.println(digitalRead(36));//0=no detection, 1=detection
    }
}
bool checkAhrsData(){
  M5.IMU.getAhrsData(&pitch, &roll, &yaw);
  if ((pitch > 20 && pitch < 50) && (roll < 5 && roll > -20) && !screenStatus)// [M5Stick] ^̌pitch<>roll 
  {return 1;}else{return 0;}                  
}
void setup(){
    M5.begin();
    M5.Imu.Init(); 
    Serial.begin(115200);
    M5.Lcd.setRotation(3);
    M5.Lcd.fillScreen(TFT_BLACK);//clear screen
    millisPrevious = millis();
    timeSync();//connect to wifi, get ntp time, set rtc to ntp time, disconnect wifi
    randomSeed(analogRead(0));
    //pinMode(36, INPUT_PULLUP);//PIR
}
void loop(){
    if (!chargingStatus && screenStatus)//if not charging update only every second, otherwise delay is handled by displayCharging(). if screen is off, no delay to quickly refresh accelerometer
        delay(1000);
    millisElapsed = millis();//timer
    voltageIn = (M5.Axp.GetVBusVoltage());
    voltageCurrent = (M5.Axp.GetBatCurrent());
    M5.update(); //updates button status
    if (voltageIn > 4.1){//if receiving charging voltage, set chargingStatus true
        chargingStatus = true;
    }else{
        chargingStatus = false;
    }
    if ((M5.BtnA.isPressed()) || checkAhrsData() || chargingStatus && !screenStatus)
        turnScreenOn();//if main button is pressed or it's charging, turn screen on if it isn't already on

    if (M5.BtnA.isPressed())//press A button to stop screen and serial printing
        testingFunctionStatus = 0;
    if (M5.BtnB.pressedFor(1000))//hold B button for 1 second to start serial printing of testingFunction()
        testingFunctionStatus = 1;
    if (M5.BtnB.pressedFor(3000))//hold B button for 3 seconds to start screen printing of testingFunction()
    {
        testingFunctionStatus = 2;
        displayChange = true;
    }
    if (M5.BtnA.pressedFor(3000) || testingFunctionStatus == 3)//swap to light control screen
    {
      testingFunctionStatus = 3;
      displayChange = true;
      lightControlScreen();
    }
    if (testingFunctionStatus > 0)//if serial(1) or screen(2) printing is enabled
    testingFunction();
    

    if (testingFunctionStatus < 2)//if not on LCD status printing screen(screen printing)
    {
        if ((millisElapsed - millisPrevious >= eventInterval) && (!chargingStatus))
            turnScreenOff();   //if 15000 ms have passed since button press and it's not charging
        if(screenStatus)//if screen is on, display clock, display battery or charging status
        {
            displayTimeAndDate();
            voltage = (M5.Axp.GetBatVoltage());
            batteryStatus();
            if(!chargingStatus)//if not charging, display battery level
            {
                displayBattery();
            }else{
                    displayCharging();//else display charging status
                 }
            if(testingFunctionStatus == 1)
            {
                M5.Lcd.setCursor(27, 88);
                M5.Lcd.setTextSize(2);
                M5.Lcd.print("Serial Printing");
                testingFunction();
            }
        }
        displayChange = false;
    }
}
