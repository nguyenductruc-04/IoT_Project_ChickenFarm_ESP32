#include "secrets.h"  // Khai báo file KEY
#include <WiFiClientSecure.h> // Thư viện cho phép ESP32 kết nối bảo mật (SSL/TLS) với AWS IoT Core qua WIFI
#include <MQTTClient.h> // Thư viện dùng giao thức MQTT để publish/subscribe dữ liệu
#include <ArduinoJson.h>  //Thư viện xử lý dữ liệu JSON 
#include "WiFi.h" // Thư viện WIFI
#include <DHT.h>  // Thư viện dùng cho cảm biến nhiệt độ - độ ẩm DHT22
#include "time.h" // Thư viện cập nhật thời gian
#include <Wire.h> 
#include <Adafruit_INA219.h>  // Thư viện cảm biến dòng
#include "HX711.h"  // Thư viện module cảm biến LoadCell
#include <Arduino.h>

// Khai báo TOPIC, chân kết nối
#define DHT22_PIN 23  // GPIO đọc cảm biến nhiệt độ - độ ẩm
#define WATER_SENSOR_PIN 36  // GPIO đọc cảm biến mực nước
#define RELAY_PIN_LED 16 // Khai báo chân kết nối Relay điều khiển LED sưởi
#define RELAY_PIN_FAN 17 // Khai báo chân kết nối Relay điều khiển FAN
#define RELAY_PIN_MOTOR 4 // Khai báo chân kết nối Relay điều khiển Motor thức ăn
#define RELAY_PIN_PUMP 5 // Khai báo chân kết nối Relay điều khiển Bơm nước
#define LOADCELL_DOUT_PIN 18  // Chân DT LoadCell
#define LOADCELL_SCK_PIN 19 // Chân SCK LoadCell

#define AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_RELAY "esp32/request/relay" // Topic nhận tín hiệu gửi Data khi khởi động App
#define AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_AUTOMODE "esp32/request/autoMode" // Topic nhận tín hiệu gửi Data khi khởi động App

#define AWS_IOT_PUBLISH_TOPIC_TEMP "esp32/esp32-to-aws-temp"  // Khai báo Topic gửi nhiệt độ lên server
#define AWS_IOT_PUBLISH_TOPIC_HUM "esp32/esp32-to-aws-hum"  // Khai báo Topic gửi độ ẩm lên server
#define AWS_IOT_PUBLISH_TOPIC_WATER_LEVEL "esp32/esp32-to-aws-water-level"  // Khai báo Topic gửi độ ẩm lên server
#define AWS_IOT_PUBLISH_TOPIC_CELL "esp32/esp32-to-aws-cell"  // Khai báo Topic gửi độ ẩm lên server

#define AWS_IOT_PUBLISH_TOPIC_DATA "esp32/esp32-to-aws-data"  // Khai báo Topic gửi data lên server

#define AWS_IOT_SUBSCRIBE_TOPIC_LED "esp32/led/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi
#define AWS_IOT_SUBSCRIBE_TOPIC_FAN "esp32/fan/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi
#define AWS_IOT_SUBSCRIBE_TOPIC_MOTOR "esp32/motor/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi
#define AWS_IOT_SUBSCRIBE_TOPIC_PUMP "esp32/pump/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi

// Topic cho chế độ AutoMode
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_LED "device/automode/led" 
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_FAN "device/automode/fan"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_MOTOR "device/automode/motor"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_PUMP "device/automode/pump"

// Topic cho ngưỡng điều khiển AutoMode
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_LED "device/automode/threshold/led"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_FAN "device/automode/threshold/fan"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_MOTOR "device/automode/threshold/motor"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_PUMP "device/automode/threshold/pump"

// Topic confirm AutoMode
#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED "device/automode/confirm/led"
#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN "device/automode/confirm/fan"
#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR "device/automode/confirm/motor"
#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP "device/automode/confirm/pump"

// Topic gửi trạng thái chân của device (High or Low)
#define AWS_IOT_PUBLISH_TOPIC_LED   "device/status/led"
#define AWS_IOT_PUBLISH_TOPIC_FAN   "device/status/fan"
#define AWS_IOT_PUBLISH_TOPIC_MOTOR "device/status/motor"
#define AWS_IOT_PUBLISH_TOPIC_PUMP  "device/status/pump"

// Topic gửi trạng thái thực của device (Được đo bằng dòng điện)
#define AWS_IOT_PUBLISH_TOPIC_REAL_LED "device/status/real/led"
#define AWS_IOT_PUBLISH_TOPIC_REAL_FAN "device/status/real/fan"
#define AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR "device/status/real/motor"
#define AWS_IOT_PUBLISH_TOPIC_REAL_PUMP "device/status/real/pump"

#define PUBLISH_INTERVAL1 5000  // Khai báo thời gian publish lên server


HX711 scale;
DHT dht22(DHT22_PIN, DHT22);  // Khai báo đối tượng cảm biến DHT22
WiFiClientSecure net = WiFiClientSecure();  // Client bảo mật (TLS)
MQTTClient client = MQTTClient(256);  // Đối tượng MQTT, buffer 256 byte

float calibration_factor = -930.7047;  // ← DÁN SỐ TÍNH ĐƯỢC!

int minValue = 0;      // khi cảm biến khô
int maxValue = 2300;   // khi ngập hoàn toàn (chỉnh theo thực tế)

float temp = 0;
float hum = 0;
int sensorValue = 0;
int levelPercent = 0; 
String sendLevel;
float average_reading;  // Khai báo biến lưu giá trị LoadCell

// NTP config Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;   // múi giờ VN = GMT+7
const int daylightOffset_sec = 0;

unsigned long lastPublishTime1 = 0;  // Khai báo biến dùng để lưu thời điểm cuối cùng ESP32 gửi dữ liệu lên server

bool autoModeLed = false;
bool autoModeFan = false;
bool autoModeMotor = false;
bool autoModePump = false;

// Biến lưu ngưỡng điều khiển thiết bị AutoMode
float tempThreshold = 0;
float humThreshold = 0;
String cellThreshold;
String waterThreshold;

void setup() {
  Serial.begin(9600); // Khởi động serial monitor để debug
  dht22.begin();  // Khởi động DHT22
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN); // Khởi động LoadCell
  scale.set_scale(calibration_factor);  // Giá trị điều chỉnh độ chính xác LoadCell

  // Set RELAY Control Device
  pinMode(RELAY_PIN_LED, OUTPUT); 
  pinMode(RELAY_PIN_FAN, OUTPUT); 
  pinMode(RELAY_PIN_MOTOR, OUTPUT); 
  pinMode(RELAY_PIN_PUMP, OUTPUT); 

  // Set ADC nhận điện áp tối đa 3.3V tránh sai kết quả vì defaut ADC ở mức 1.1V
  analogSetAttenuation(ADC_11db); 
  
  // Connect WiFi
  WiFi.mode(WIFI_STA);  // Mode kết nối WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Kết nối WIFI
  Serial.println("ESP32 connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }// Loading trong khi connect WIFI
  Serial.println();
  
  // Gọi hàm kết nối server
  connectToAWS();

  // Cấu hình thời gian
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Syncing time...");
  delay(2000);
  printLocalTime();

  // Gửi lại dữ liệu về thông tin device cho App khi khởi động ESP32
  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED);
  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR);
  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP);

  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_LED, RELAY_PIN_LED);
  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_FAN, RELAY_PIN_FAN); 
  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
  sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);

  sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED, autoModeLed, String(tempThreshold, 1));
  sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN, autoModeFan, String(humThreshold, 1));
  sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR, autoModeMotor, cellThreshold);
  sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP, autoModePump, waterThreshold);
}

void loop() {

  // Kiểm tra kết nối WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(WiFi.status() == WL_CONNECTED ? "✅ Reconnected!" : "❌ Failed reconnect");
  }

  // Kiểm tra kết nối MQTT 
  if (!client.connected()) {
    Serial.println("MQTT disconnected! Reconnecting...");
    connectToAWS();
  }

  minValue = 0;      // khi cảm biến khô
  maxValue = 2300;   // khi ngập hoàn toàn (chỉnh theo thực tế)

  // Xử lý cảm biến Nhiệt độ, Độ ẩm
  temp = dht22.readTemperature();
  hum = dht22.readHumidity();
  delay(10);
  // In ra để kiểm tra kết quả hoặc báo lỗi
  if ( isnan(temp) || isnan(hum)) {
    Serial.println("Failed to read from DHT22 sensor!");
  } 

  // Xử lý cảm biến LoadCell
  average_reading = -scale.get_units(10);  // 10 lần trung bình

  // Xử lý cảm biến Mực nước
  sensorValue = analogRead(WATER_SENSOR_PIN);
  delay(2);
  sensorValue = constrain(sensorValue, minValue, maxValue);   // Giới hạn trong phạm vi hợp lệ
  levelPercent = map(sensorValue, minValue, maxValue, 0, 100);  // Chuyển sang phần trăm mực nước
  if (levelPercent < 30) {
    sendLevel = "Thấp";
  } 
  else if (levelPercent < 70) {
    sendLevel = "Trung bình";
  } 
  else {
    sendLevel = "Cao";
  }
  
  // Gọi hàm xử lý AutoMode
  handleAutoMode(temp, hum, average_reading, levelPercent);

// millis(): số mili giây đã trôi qua từ lúc ESP32 khởi động. Cụm code này có ý nghĩa: so sánh thời gian để ESP gửi dữ liệu mỗi 4 giây một lần
  if (millis() - lastPublishTime1 > PUBLISH_INTERVAL1 || millis() < lastPublishTime1) { 
    sendToAWS();  // Function gửi dữ liệu lên server
    lastPublishTime1 = millis(); 
  }
  
  client.loop();  // Duy trì kết nối server
  delay(5); 
}

void connectToAWS() {
  // Cấu hình WiFiClientSecure để sử dụng thông tin đăng nhập server
  // Nạp giấy chứng nhận để kết nối bảo mật TLS
  net.setCACert(AWS_CERT_CA); 
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.begin(AWS_IOT_ENDPOINT, 8883, net);  // Khai báo kết nối endpoint, port, net: kênh WIFI bảo mật (TLS) được cấu hình để giao tiếp MQTT bảo mật. Thông tin được truyền qua kênh này

  // Khi client nhận được tin nhắn từ MQTT thì gọi hàm messageHandler(topic, payload) để xử lý
  client.onMessage(messageHandler);

  Serial.print("ESP32 connecting to AWS IOT");

// Kết nối đến server bằng THINGNAME
  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  if (!client.connected()) {
    Serial.println("ESP32 - AWS IoT Timeout!");
    return;
  }

  // Subscribe dữ liệu vào topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_RELAY);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_AUTOMODE);

  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_LED);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_FAN);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_MOTOR);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_PUMP);

  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_LED);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_FAN);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_MOTOR);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_PUMP);
   
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_LED);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_FAN);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_MOTOR);
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_PUMP);

  Serial.println("ESP32  - AWS IoT Connected!");
}

void sendToAWS() {
  struct tm timeinfo;
  char timeString[50];  

  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);

  // Tạo JSON object nhiệt độ, độ ẩm
  StaticJsonDocument<200> messageTemp;
  StaticJsonDocument<200> messageHum;
  StaticJsonDocument<200> messageWaterLevel;
  StaticJsonDocument<200> messageCell;
  StaticJsonDocument<200> messageData;

  // Ghi dữ liệu nhiệt độ từ sensor
  messageTemp["data_TempC"] = temp;
  // Ghi dữ liệu độ ẩm từ sensor
  messageHum["data_Hum"] = hum;
  // Ghi dữ liệu mực nước từ sensor
  messageWaterLevel["data_WaterLevel"] = sendLevel;
  messageCell["data_Cell"] = average_reading;
  
  // Ghi dữ liệu nhiệt độ , độ ẩm từ sensor để lưu trữ
  messageData["deviceId"] = "esp32";
  messageData["timestamp"] = timeString;
  messageData["temperature"] = temp;
  messageData["humidity"] = hum;
  
  char messageBufferTemp[512];
  char messageBufferHum[512];
  char messageBufferWaterLevel[512];
  char messageBufferCell[512];
  char messageBufferData[512];
  // Chuyển JSON thành string
  serializeJson(messageTemp, messageBufferTemp); 
  serializeJson(messageHum, messageBufferHum);  
  serializeJson(messageWaterLevel, messageBufferWaterLevel);  
  serializeJson(messageCell, messageBufferCell);  
  serializeJson(messageData, messageBufferData);
  // Publish lên topic server
  client.publish(AWS_IOT_PUBLISH_TOPIC_TEMP, messageBufferTemp);
  client.publish(AWS_IOT_PUBLISH_TOPIC_HUM, messageBufferHum);
  client.publish(AWS_IOT_PUBLISH_TOPIC_WATER_LEVEL, messageBufferWaterLevel);
  client.publish(AWS_IOT_PUBLISH_TOPIC_CELL, messageBufferCell);
  client.publish(AWS_IOT_PUBLISH_TOPIC_DATA, messageBufferData);
  // print để Debug
  Serial.println("sent:");
  Serial.print("- topic: ");
  Serial.println(AWS_IOT_PUBLISH_TOPIC_TEMP);
  Serial.print("- payload:");
  Serial.println(messageBufferTemp);
  Serial.println(AWS_IOT_PUBLISH_TOPIC_HUM);
  Serial.print("- payload:");
  Serial.println(messageBufferHum);
  Serial.println(AWS_IOT_PUBLISH_TOPIC_WATER_LEVEL);
  Serial.print("- payload:");
  Serial.println(messageBufferWaterLevel);
}

// Hàm gửi trạng thái chân Device (High or Low)
void sendToAwsRelayStatus(const char* topic, int pin) {
  int state = digitalRead(pin); // Đọc trạng thái thực tế của relay
  delay(50);
  StaticJsonDocument<50> messageStatus;
  messageStatus["status"] = (state == HIGH) ? "ON" : "OFF";
  char buffer[100];
  serializeJson( messageStatus, buffer);
  client.publish(topic, buffer);
}

// Hàm xác nhận trạng thái AutoMode gửi về App
void sendConfirmAutomode(const char* topic, bool autoMode, String threshold ) {
  // Gửi lại trạng thái
  StaticJsonDocument<150> doc;
  doc["autoMode"] = autoMode;
  doc["selectedThreshold"] = threshold;
  char buffer[150];
  serializeJson(doc, buffer);
  client.publish(topic, buffer);
}

// Xử lý dữ liệu nhận được từ topic
void messageHandler(String &topic, String &payload) {
  Serial.println("received:");
  Serial.println("- topic: " + topic);
  Serial.println("- payload:");
  Serial.println(payload);

  // Chuyển đổi chuỗi JSON thành cấu trúc như Object để truy cập như biến
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload);
  // Thêm kiểm tra JSON đúng chuẩn 
  if (error) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(error.c_str()); // In ra lỗi của JSON (.c_str() -> chuyển error thành chuỗi String từ kiểu DeserializationError)
    Serial.println("⚠️ Payload lỗi, không xử lý tiếp!");
    return;
  }
  // Kiểm tra Key của JSON -> Nếu JSON không chứa cả hay Key thì đây không phải là Json mong muốn -> bỏ qua
  if (!doc.containsKey("status") && !doc.containsKey("threshold")) {
    Serial.println("⚠️ JSON không có key hợp lệ!");
    return;
  }

  const char* messageStatus = doc["status"] | "";     // fallback rỗng để tránh null
  const char* messageThreshold = doc["threshold"] | "";

  // Debug
  Serial.print("📥 status: ");
  Serial.println(messageStatus);
  Serial.print("📥 threshold: ");
  Serial.println(messageThreshold);
  // strcmp: hàm so sánh chuỗi trả về một số nguyên: giống =0; khác !=0; không compare string vì ta lấy message lưu vào con trỏ để giảm tối ưu bộ nhớ
// ------------------- TRẠNG THÁI BẬT -------------------
  if (strcmp(messageStatus, "ON") == 0) {
    if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_RELAY)) {
    Serial.println("NHAN REQUEST RELAY TU APP");
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED);
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR);
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP);
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_LED, RELAY_PIN_LED);
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_FAN, RELAY_PIN_FAN); 
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_AUTOMODE)) {
    Serial.println("NHAN REQUEST AUTOMODE TU APP");
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED, autoModeLed, String(tempThreshold, 1));
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN, autoModeFan, String(humThreshold, 1));
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR, autoModeMotor, cellThreshold);
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP, autoModePump, waterThreshold);

    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_LED)) {
      digitalWrite(RELAY_PIN_LED, HIGH);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_LED, RELAY_PIN_LED);
      Serial.println("LED : ON");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_FAN)) {
      digitalWrite(RELAY_PIN_FAN, HIGH);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_FAN, RELAY_PIN_FAN); 
      Serial.println("FAN : ON");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_MOTOR)) {
      digitalWrite(RELAY_PIN_MOTOR, HIGH);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
      Serial.println("MOTOR : ON");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_PUMP)) {
      digitalWrite(RELAY_PIN_PUMP, HIGH);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);
      Serial.println("PUMP : ON");
    }

    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_LED)) {
      autoModeLed = true;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED, autoModeLed, String(tempThreshold, 1));
      Serial.println("AUTO MODE LED: ON");
      
    }

    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_FAN)) {
      autoModeFan = true;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN, autoModeFan, String(humThreshold, 1));
      Serial.println("AUTO MODE FAN: ON");
      
    }

    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_MOTOR)) {
      autoModeMotor = true;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR, autoModeMotor, cellThreshold);
      Serial.println("AUTO MODE MOTOR: ON");
      
    }

    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_PUMP)) {
      autoModePump = true;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP, autoModePump, waterThreshold);
      Serial.println("AUTO MODE PUMP: ON");
      
    }
  }    

  // ------------------- TRẠNG THÁI TẮT -------------------
  else if (strcmp(messageStatus, "OFF") == 0) {
    if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_LED)) {
      digitalWrite(RELAY_PIN_LED, LOW);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_LED, RELAY_PIN_LED);
      autoModeLed = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED, autoModeLed, String(tempThreshold, 1));
      Serial.println("LED : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_FAN)) {
      digitalWrite(RELAY_PIN_FAN, LOW);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_FAN, RELAY_PIN_FAN); 
      autoModeFan = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN, autoModeFan, String(humThreshold, 1));
      Serial.println("FAN : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_MOTOR)) {
      digitalWrite(RELAY_PIN_MOTOR, LOW);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
      autoModeMotor = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR, autoModeMotor, cellThreshold);
      Serial.println("MOTOR : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_PUMP)) {
      digitalWrite(RELAY_PIN_PUMP, LOW);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);
      autoModePump = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP, autoModePump, waterThreshold);
      Serial.println("PUMP : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_LED)) {
      autoModeLed = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED, autoModeLed, String(tempThreshold, 1));
      Serial.println("AUTO MODE LED: OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_FAN)) {
      autoModeFan = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN, autoModeFan, String(humThreshold, 1));
      Serial.println("AUTO MODE FAN: OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_MOTOR)) {
      autoModeMotor = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR, autoModeMotor, cellThreshold);
      Serial.println("AUTO MODE MOTOR: OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_PUMP)) {
      autoModePump = false;
      sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP, autoModePump, waterThreshold);
      Serial.println("AUTO MODE PUMP: OFF");
    }
  }

  if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_LED)) {
    if (strlen(messageThreshold) > 0)
      tempThreshold = atof(doc["threshold"]);  // Chuyển chuỗi nhận được sang số để gắn cho biến float tempThreshold
  sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED, autoModeLed, String(tempThreshold, 1)); // String(tempThreshold, 1) -> chuyển số thành chuỗi String để gửi lên App (đồng bộ với hàm)
    
  }
  else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_FAN)) {
    if (strlen(messageThreshold) > 0)
      humThreshold = atof(doc["threshold"]);   // số
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN, autoModeFan, String(humThreshold, 1));
  }
  else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_MOTOR)) {
    if (strlen(messageThreshold) > 0)
      cellThreshold = String((const char*)doc["threshold"]);  // chữ
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR, autoModeMotor, cellThreshold);

  }
  else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_PUMP)) {
    if (strlen(messageThreshold) > 0)
      waterThreshold = String((const char*)doc["threshold"]); // chữ
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP, autoModePump, waterThreshold);
  }
}

// Hàm kiểm tra Time
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

// Hàm xử lý AutoMode
void handleAutoMode(float temp, float hum, float average_reading, int levelPercent) {
    // --- Điều khiển LED sưởi ---
  if (autoModeLed) {
    if (temp < (tempThreshold - 0.2)) {
      digitalWrite(RELAY_PIN_LED, HIGH);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED); 
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_LED, RELAY_PIN_LED); 
    }
    else if (temp > (tempThreshold + 0.2)) {
      digitalWrite(RELAY_PIN_LED, LOW);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED); 
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_LED, RELAY_PIN_LED); 
    }
  }

  if (autoModeFan) {
    if (hum < humThreshold) {
      digitalWrite(RELAY_PIN_FAN, LOW);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_FAN, RELAY_PIN_FAN); 
    }
    else if (hum > humThreshold) {
      digitalWrite(RELAY_PIN_FAN, HIGH);
      delay(100);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_FAN, RELAY_PIN_FAN); 
    };
  }

if (autoModeMotor) {
 if (cellThreshold == "Thap") {
      if (average_reading < 50){
        digitalWrite(RELAY_PIN_MOTOR, HIGH);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
      }
      else if (average_reading > 210) {
        digitalWrite(RELAY_PIN_MOTOR, LOW);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
      }
    }
    else  if (cellThreshold == "Trung binh") {
      if (average_reading < 150){
        digitalWrite(RELAY_PIN_MOTOR, HIGH);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
      }
      else if (average_reading < 210) {
        digitalWrite(RELAY_PIN_MOTOR, LOW);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_MOTOR, RELAY_PIN_MOTOR);
      }
    }
}

  if (autoModePump) {
    if (waterThreshold == "Thap") {
      if (levelPercent < 30){
        digitalWrite(RELAY_PIN_PUMP, HIGH);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_PUMP); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);
      }
      else if (levelPercent > 85) {
        digitalWrite(RELAY_PIN_PUMP, LOW);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);
      }
    }
    else  if (waterThreshold == "Trung binh") {
      if (levelPercent < 70){
        digitalWrite(RELAY_PIN_PUMP, HIGH);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);
      }
      else if (levelPercent > 85) {
        digitalWrite(RELAY_PIN_PUMP, LOW);
        delay(100);
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP); 
        sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_REAL_PUMP, RELAY_PIN_PUMP);
      }
    }
  }
}