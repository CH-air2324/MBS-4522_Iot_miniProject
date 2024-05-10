#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RoxMux.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_PWMServoDriver.h>
#include <PubSubClient.h>

//setup servo
#define PCA9685_ADDR 0x40 // PCA9685 i2c ipadress
#define PWM_FREQ 60 // PWM frequency
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PCA9685_ADDR);
unsigned long previousTime = 0;  // 記錄上次轉動的時間


#define SCREEN_WIDTH 128 // 设置OLED宽度,单位:像素
#define SCREEN_HEIGHT 64 // 设置OLED高度,单位:像素
#define SS_PIN 5
#define RST_PIN 27
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;
MFRC522 rfid(SS_PIN, RST_PIN);

#define MUX_TOTAL 1
Rox74HC165 <MUX_TOTAL> mux;

#define PIN_DATA  36 // pin 9 on 74HC165 (DATA)                                                                                                      
#define PIN_LOAD  32 // pin 1 on 74HC165 (LOAD)
#define PIN_CLK   33 // pin 2 on 74HC165 (CLK))



// 自定义重置引脚,虽然教程未使用,但却是Adafruit_SSD1306库文件所必需的
#define OLED_RESET 4
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int Ps = 5; // 停車位數量
byte prID[5][10];
bool foundEmpty = false;

const char*ssid ="kotoha"; //网络名称
const char*password ="Jimmy1ac"; //网络密码
// MQTT broker
// MQTT broker ip
const char* mqtt_server = "test.mosquitto.org";	// //broker.emqx.io

// Initializes the espClient
WiFiClient espClient;
PubSubClient client(espClient);

const char*ntpServer = "time-a-g.nist.gov";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    display.println("Failed to obtain time");
    return;
  }
  display.println(&timeinfo, "%F\r\n%T\r\n%A" ); 
  
}
unsigned long CCTime = millis();

void readRFID() {
  
  int ang=90;
  int channel = 0;
  int offTime = map(ang,0,180,125,575);
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()&&millis()-CCTime>=2000) {
    CCTime = millis();
    Serial.print("RFID UID:");
    for (byte i = 0; i < 10; i++) {
      Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(rfid.uid.uidByte[i], HEX);
    }
    Serial.println();

    bool isDuplicate = false;
    byte duplicateIndex = 255;
    for (byte j = 0; j < 5; j++) {
      bool match = true;
      for (byte i = 0; i < 10; i++) {
        if (rfid.uid.uidByte[i] != prID[j][i]) {
          match = false;
          break;
        }
      }
      if (match) {
        isDuplicate = true;
        duplicateIndex = j;
        break;
      }
    }
    
    
    if (isDuplicate) {
      Ps++; // ID重複，表示車輛已離開，停車位數量加一
      pwm.setPWM(channel, 0, offTime);
      memset(prID[duplicateIndex], 0, sizeof(prID[duplicateIndex])); // 清除重複卡片的數據
    } else {
      for (byte j = 0; j < 5; j++) {
        bool empty = true;
        for (byte i = 0; i < 10; i++) {
          if (prID[j][i] != 0) {
            empty = false;
            break;
          }
        }
        if (empty) {
          foundEmpty = true;
          memcpy(prID[j], rfid.uid.uidByte, 4); // 新ID，存儲卡片序列號
          break;
        }
      }
      if (Ps > 0) {
        Ps--; // 新ID，停車位數量減一if(Ps>0)
        pwm.setPWM(channel, 0, offTime);
      }
      
      
    }

    Serial.print("Available Parking Spaces: ");
    Serial.println(Ps);
  }
  Serial.println(CCTime);
  Serial.println(millis());
  Serial.println(millis()- CCTime);
  if(millis()- CCTime >=2000){
      ang = 180;
      offTime = map(ang, 0, 180, 125, 575);
      pwm.setPWM(channel, 0, offTime);
  }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void reconnect() {
// Loop until we're reconnected
while (!client.connected()) {
Serial.print("Attempting MQTT connection...");
// Attempt to connect
  if (client.connect("Client")) {//MQTT_username, MQTT_password
    Serial.println("connected");
    // Subscribe or resubscribe to a topic
    }
    else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    }
  }
}
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Received message: ");
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  
  Serial.println();
}

bool pinState[MUX_TOTAL*8];

void setup()
{
  client.setServer(mqtt_server, 1883); // 使用默认端口1883连接到MQTT代理

  Serial.begin(115200);
  mux.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  //设置I2C的两个引脚SDA和SCL，这里用到的是17作为SDA，16作为SCL
  //Wire.setPins(SDA/21/SCL/22);

//PCA9685 setting
  Wire.begin(); // 初始化I2C通訊
 while (!pwm.begin()) // 初始化PCA9685)
  {
    Serial.println("faild");
  }
  
  pwm.setPWMFreq(PWM_FREQ); // 設定PWM頻率


  //初始化OLED并设置其IIC地址为 0x3C
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  //清除屏幕
  display.clearDisplay();
  //设置字体颜色,白色可见
  display.setTextColor(WHITE);
  SPI.begin();
  rfid.PCD_Init();
  rfid.PCD_DumpVersionToSerial();


  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    
    display.print(".");
  }
  display.println("WiFi connected!");

  // 从网络时间服务器上获取并设置时间
  // 获取成功后芯片会使用RTC时钟保持时间的更新
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  display.println("WiFi disconnected!");
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  display.display();
}



void loop()
{
  if (Ps>0){
    client.publish("pARking_SpACe", String(Ps).c_str());
  }
  else{
    client.publish("pARking_SpACe", "No available parking space!");
  }
  // put your main code here, to run repeatedly:、
  if (WiFi.status() != WL_CONNECTED) {
  Serial.println("WiFi connection lost. Reconnecting...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting to WiFi...");
    }
    Serial.println("Reconnected to WiFi!");
  }
  if (!client.connected()) {
    reconnect();
  }
  mux.update();
  for(uint8_t i=0, n=mux.getLength(); i < n ; i++){
    bool data = mux.read(i);
    if(data != pinState[i]){
      pinState[i] = data;
      // Serial.print("Pin ");
      // Serial.print(i);
      // Serial.print(": ");
    }
  }
  readRFID();
 
  //清除屏幕
  display.clearDisplay();
  //设置光标位置
  display.setCursor(0, 0);
  display.setTextSize(1);
  printLocalTime();
  if (Ps==0) {
    display.println("No available parking space!");
  }
  else{
    display.print("Parking Spaces: ");
    display.println(Ps);
  }
  int FULL = Ps==0;  
  if (FULL)
  {
    display.setCursor(20, 30);
    display.setTextSize(4);
    display.println("FULL");
  }
  else {
    if(pinState[0]==1)
    {
      display.drawRoundRect(0, 35, 20, 25, 2, WHITE);
      client.publish("pARkInG_StatE01", String(0).c_str());
    }
    else{
      display.drawRoundRect(0, 35, 20, 25, 2, WHITE);
      display.fillRoundRect(2, 37, 16, 21, 2, WHITE);
      client.publish("pARkInG_StatE01", String(1).c_str());   
    }
    if(pinState[1]==1)
    {
      display.drawRoundRect(27, 35, 20, 25, 2, WHITE);
      client.publish("pARkInG_StatE02", String(0).c_str());
    }
    else{
      display.drawRoundRect(27, 35, 20, 25, 2, WHITE);
      display.fillRoundRect(29, 37, 16, 21, 2, WHITE);
      client.publish("pARkInG_StatE02", String(1).c_str());
    }
    if(pinState[2]==1)
    {
      display.drawRoundRect(54, 35, 20, 25, 2, WHITE);
      client.publish("pARkInG_StatE03", String(0).c_str());
    }
    else{
      display.drawRoundRect(54, 35, 20, 25, 2, WHITE);
      display.fillRoundRect(56, 37, 16, 21, 2, WHITE);
      client.publish("pARkInG_StatE03", String(1).c_str());
    }
    if(pinState[3]==1)
    {
      display.drawRoundRect(81, 35, 20, 25, 2, WHITE); 
      client.publish("pARkInG_StatE04", String(0).c_str());
    }
    else{
      display.drawRoundRect(81, 35, 20, 25, 2, WHITE); 
      display.fillRoundRect(83, 37, 16, 21, 2, WHITE);
      client.publish("pARkInG_StatE04", String(1).c_str());
    }
    if(pinState[4]==1)
    {
      display.drawRoundRect(108, 35, 20, 25, 2, WHITE);
      client.publish("pARkInG_StatE05", String(0).c_str());
    }
    else{
      display.drawRoundRect(108, 35, 20, 25, 2, WHITE);
      display.fillRoundRect(110, 37, 16, 21, 2, WHITE);
      client.publish("pARkInG_StatE05", String(1).c_str());
    }
  }
  display.display();
}