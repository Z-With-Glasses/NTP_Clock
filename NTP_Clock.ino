#include "M5StickCPlus.h"
#include "Streaming.h"
#include "time.h"
#include "WiFi.h"

RTC_TimeTypeDef RTC_TimeStruct;
RTC_DateTypeDef RTC_DateStruct;


const unsigned long eventInterval = 15000;//timer for screen timeout
static unsigned long millisPrevious = 0;
static unsigned long millisElapsed = 0;
static bool screenStatus = true ;
bool chargingStatus = false;
static float voltage{};//AXP192 uses floats here, so I do as well
static float voltageIn{};//AXP192 uses floats here, so I do as well
static int batteryPercentInTens{};
static float voltageCurrent{};
int testingFunctionStatus{};
uint32_t xtalCrystalFrequency{getXtalFrequencyMhz()};
uint32_t cpuClockRate{getCpuFrequencyMhz()};
uint32_t apbBusClockRate{getApbFrequency()};
uint8_t previousHours{};
static uint8_t previousMinutes{};
static uint8_t previousSeconds{};
static bool displayChange{1};


void timeSync() {
    const char* ssid      = "SSID";
    const char* password  = "PASS";
    const char* ntpServer = "time.nist.gov";
    const long gmtOffset_sec     = -28800;
    const int daylightOffset_sec = 3600;
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("Connecting to %s...", ssid);
    WiFi.begin(ssid, password);  // Connect wifi and return connection status.
    while (WiFi.status() != WL_CONNECTED)// If the wifi connection fails.  
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
void turnScreenOn() {
    setCpuFrequencyMhz(40);
    millisPrevious=millisElapsed;
    Wire1.beginTransmission(0x34);
    Wire1.write(0x12);
    Wire1.write(0x4d); // Enable LDO2, aka OLED_VDD
    Wire1.endTransmission();
    screenStatus=true;
    displayChange=1;
}
void turnScreenOff() {
    Wire1.beginTransmission(0x34);
    Wire1.write(0x12);
    Wire1.write(0b01001011);  // LDO2, aka OLED_VDD, off
    Wire1.endTransmission();
    screenStatus=false;
    setCpuFrequencyMhz(10);
    M5.Axp.SetSleep();
}
void batteryStatus(){
    if (voltage >=4.17)
        batteryPercentInTens=10;//100%, only on charger
    if ((voltage >= 4.11) && (voltage <= 4.2))//90-99%
        batteryPercentInTens=9;
    if ((voltage >= 4.02) && (voltage <= 4.1099))//80-89%
        batteryPercentInTens=8;
    if ((voltage >= 3.95) && (voltage <= 4.0199))//70-79%
        batteryPercentInTens=7;
    if ((voltage >= 3.87) && (voltage <= 3.9499))//60-69%
        batteryPercentInTens=6;
    if ((voltage >= 3.84) && (voltage <= 3.8699))//50-59%
        batteryPercentInTens=5;
    if ((voltage >= 3.8) && (voltage <= 3.8399))//40-49%
        batteryPercentInTens=4;
    if ((voltage >= 3.77) && (voltage <= 3.799))//30-39%
        batteryPercentInTens=3;
    if ((voltage >= 3.73) && (voltage <= 3.7699))//20-29%
        batteryPercentInTens=2;
    if ((voltage >= 3.69) && (voltage <= 3.7299))//10-19%
        batteryPercentInTens=1;
    if (voltage <= 3.6899)//0-9%
        batteryPercentInTens=0;
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
    for (int it=0; it < batteryPercentInTens; it++)//prints > once for each 10% of charge in battery
      {
        delay(chargingDelay);
        M5.Lcd.print('>');
      }
}
void displayBattery() {  
    M5.Lcd.setCursor(12, 105);
    M5.Lcd.setTextSize(3);
    if (batteryPercentInTens==10)//90-100%
    {
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<<<<]");      
    }
    if (batteryPercentInTens==9)//80-89%
    {
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<<< ]");      
    }
    if (batteryPercentInTens==8)//70-79%
    {
        M5.Lcd.setTextColor(TFT_GREENYELLOW,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<<  ]");       
    }
    if (batteryPercentInTens==7)//60-69%
    {
        M5.Lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
        M5.Lcd.print("[<<<<<<<   ]");      
    }
    if (batteryPercentInTens==6)//50-59%
    {
        M5.Lcd.setTextColor(TFT_YELLOW,TFT_BLACK);
        M5.Lcd.print("[<<<<<<    ]");        
    }
    if (batteryPercentInTens==5)//40-49%
    {
        M5.Lcd.setTextColor(TFT_ORANGE,TFT_BLACK);
        M5.Lcd.print("[<<<<<     ]");         
    }
    if (batteryPercentInTens==4)//30-39%
    {
        M5.Lcd.setTextColor(TFT_ORANGE,TFT_BLACK);
        M5.Lcd.print("[<<<<      ]");         
    }
    if (batteryPercentInTens==3)//20-29%
    {
        M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
        M5.Lcd.print("[<<<       ]");         
    }
    if (batteryPercentInTens==2)//10-19%
    {
        M5.Lcd.setTextColor(TFT_RED,TFT_BLACK);
        M5.Lcd.print("[<<        ]");        
    }
    if (batteryPercentInTens==1)//0-9%
    {
        M5.Lcd.setTextColor(TFT_MAROON,TFT_BLACK);
        M5.Lcd.print("[<         ]");         
    }
}
void displayTimeAndDate() {
    if (displayChange)
    M5.Lcd.fillScreen(TFT_BLACK);//clear screen
    
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
    M5.Rtc.GetTime(&RTC_TimeStruct);
    M5.Rtc.GetData(&RTC_DateStruct);
    M5.Lcd.setCursor(27, 0);
    int dayOfWeekInt = (RTC_DateStruct.WeekDay);
    char dayOfWeek[10];
        switch(dayOfWeekInt)
        {
          case 0:{
          strcpy(dayOfWeek, "Sunday");}
          break;
          case 1:{
          strcpy(dayOfWeek, "Monday");}
          break;
          case 2:{
          strcpy(dayOfWeek, "Tuesday");}
          break;
          case 3:{
          strcpy(dayOfWeek, "Wednesday");}
          break;
          case 4:{
          strcpy(dayOfWeek, "Thursday");}
          break;
          case 5:{
          strcpy(dayOfWeek, "Friday");}
          break;
          case 6:{
          strcpy(dayOfWeek, "Saturday");}
          break;
        }
    M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
    M5.Lcd.setCursor(25, 0);
    M5.Lcd.printf("%04d-%02d-%02d", RTC_DateStruct.Year, RTC_DateStruct.Month, RTC_DateStruct.Date);
    M5.Lcd.setCursor(65, 30);
    M5.Lcd.printf("%s", dayOfWeek);//learn how to print c++ strings with M5 already!
    if (previousHours != RTC_TimeStruct.Hours || displayChange)//if hours has changed
    {
        M5.Lcd.setCursor(45, 60);
        M5.Lcd.setTextColor(TFT_BLACK,TFT_BLACK);
        M5.Lcd.printf("%02d:",previousHours);//print over previous hour with black
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.setCursor(45, 60);
        M5.Lcd.printf("%02d:",RTC_TimeStruct.Hours);//print new hour in green
        previousHours=RTC_TimeStruct.Hours;//assign current hour to previous hour
    }
    if (previousMinutes != RTC_TimeStruct.Minutes || displayChange);//if minutes has changed
    {
        M5.Lcd.setCursor(104, 60);
        M5.Lcd.setTextColor(TFT_BLACK,TFT_BLACK);
        M5.Lcd.printf("%02d:",previousMinutes);
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.setCursor(104, 60);
//int cursorPositionMinutes = m5.Lcd.getCursorX();//get minutes cursor position
//Serial << "minutes cursor-" << cursorPositionMinutes << '\n';//print minutes cursor postion to serial
        M5.Lcd.printf("%02d:",RTC_TimeStruct.Minutes);
        previousMinutes=RTC_TimeStruct.Minutes;
    }
    if (previousSeconds != RTC_TimeStruct.Seconds || displayChange);//if seconds has changed
    {
        M5.Lcd.setCursor(158, 60);
        M5.Lcd.setTextColor(TFT_BLACK,TFT_BLACK);
        M5.Lcd.printf("%02d",previousSeconds);
        M5.Lcd.setTextColor(TFT_GREEN,TFT_BLACK);
        M5.Lcd.setCursor(158, 60);
//int cursorPositionSeconds = m5.Lcd.getCursorX();//get seconds cursor position
//Serial << "seconds cursor-" << cursorPositionSeconds << '\n';//print seconds cursor postion to serial
        M5.Lcd.printf("%02d",RTC_TimeStruct.Seconds);    
        previousSeconds=RTC_TimeStruct.Seconds;
    }
}
void testingFunction(){      
    Serial << "voltage-" << voltage << '\n';//battery voltage
    Serial << "voltage input-" << voltageIn << '\n';//battery voltage input
    Serial << "previous millis-" <<  millisPrevious << '\n';
    Serial << "elapsed millis-" <<  millisElapsed << '\n';
    Serial << "charging status-" << chargingStatus << '\n';//0=not charging 1=charging
    Serial << "screen status-" << screenStatus << '\n';//0=off 1=on
    Serial << "battery percent rounded up to nearest ten-" << batteryPercentInTens << "0%" << '\n';
    Serial << "battery current-" << voltageCurrent << '\n';//negative value=discharge, positive=charge, 0= full charge
    Serial << "XTAL-" << xtalCrystalFrequency <<'\n';//XTAL frequency in MHz
    Serial << "CPU clock rate-" << cpuClockRate << '\n';//CPU clock rate in MHz
    Serial << "APB bus clock rate-" << apbBusClockRate << '\n';//APB bus clock rate in Hz 
    if (testingFunctionStatus == 2)
    {
        if (chargingStatus)
        delay(500);
        
        voltage = (M5.Axp.GetBatVoltage());
        batteryStatus();
        M5.Lcd.fillScreen(TFT_BLACK);//clear screen
        M5.Lcd.setTextSize(1); 
        M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
        M5.Lcd.setCursor(0,0);
        M5.Lcd.print("voltage- ");
        M5.Lcd.println(voltage);
        M5.Lcd.print("voltage input- ");
        M5.Lcd.println(voltageIn);
        M5.Lcd.print("previous millis- ");
        M5.Lcd.println(millisPrevious);
        M5.Lcd.print("elapsed millis- ");
        M5.Lcd.println(millisElapsed);
        M5.Lcd.print("charging status- ");
        M5.Lcd.println(chargingStatus);
        M5.Lcd.print("battery % in 10's- ");
        M5.Lcd.println(batteryPercentInTens);
        M5.Lcd.print("battery current- ");
        M5.Lcd.println(voltageCurrent);
        M5.Lcd.print("XTAL- ");
        M5.Lcd.println(xtalCrystalFrequency);
        M5.Lcd.print("CPU clock rate- ");
        M5.Lcd.println(cpuClockRate);
        M5.Lcd.print("APB bus clock rate- ");
        M5.Lcd.println(apbBusClockRate);
        //M5.Lcd.print("PIR input- ");
        //M5.Lcd.println(digitalRead(36));//0=no detection, 1=detection
    }
}
void setup() {
    M5.begin();
    Serial.begin(115200);
    M5.Lcd.setRotation(3);
    M5.Lcd.fillScreen(TFT_BLACK);//clear screen
    millisPrevious = millis();
    timeSync();//connect to wifi, get ntp time, set rtc to ntp time, disconnect wifi
    randomSeed(analogRead(0));
    //pinMode(36, INPUT_PULLUP);//PIR
}
void loop() {
    if (!chargingStatus)//if not charging update only every second, otherwise delay is handled by displayCharging()
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
    if ((M5.BtnA.isPressed()) || (chargingStatus))//main button is pressed, or it's charging turn screen on
    {
        if (!screenStatus)//checks if screen is already on
        turnScreenOn();
    }

    if (M5.BtnA.isPressed())//press A button to stop screen and serial printing
        testingFunctionStatus=0;
    if (M5.BtnB.pressedFor(1000))//hold B button for 1 second to start serial printing of testingFunction()
        testingFunctionStatus=1;
    if (M5.BtnB.pressedFor(3000))//hold B button for 3 seconds to start screen printing of testingFunction()
    {
        testingFunctionStatus=2;
        displayChange=1;
    }
    if (testingFunctionStatus > 0)//if serial(1) or screen(2) printing is enabled
    testingFunction();
    

    if (testingFunctionStatus<2)
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
            if(testingFunctionStatus==1)
            {
                M5.Lcd.setCursor(27, 90);
                M5.Lcd.setTextSize(2);
                M5.Lcd.print("Serial Printing");
                testingFunction();
            }
        }
        displayChange=0;
    }
}
