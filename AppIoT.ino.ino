#include "secrets.h"  // Khai báo file KEY
#include <WiFiClientSecure.h> // Thư viện cho phép ESP32 kết nối bảo mật (SSL/TLS) với AWS IoT Core qua WIFI
#include <MQTTClient.h> // Thư viện dùng giao thức MQTT để publish/subscribe dữ liệu
#include <ArduinoJson.h>  //Thư viện xử lý dữ liệu JSON 
#include "WiFi.h" // Thư viện WIFI
#include <DHT.h>  // Thư viện dùng cho cảm biến nhiệt độ - độ ẩm DHT22
#include "time.h"

// Khai báo TOPIC, chân kết nối
#define DHT22_PIN 21  // GPIO đọc cảm biến nhiệt độ - độ ẩm
#define WATER_SENSOR_PIN 36  // GPIO đọc cảm biến mực nước
#define RELAY_PIN_LED 16 // Khai báo chân kết nối Relay điều khiển LED sưởi
#define RELAY_PIN_FAN 17 // Khai báo chân kết nối Relay điều khiển FAN
#define RELAY_PIN_MOTOR 4 // Khai báo chân kết nối Relay điều khiển Motor thức ăn
#define RELAY_PIN_PUMP 5 // Khai báo chân kết nối Relay điều khiển Bơm nước

//#define ACS712_PIN 34   // Chân ADC của ESP32

#define AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_RELAY "esp32/request/relay"
#define AWS_IOT_SUBSCRIBE_TOPIC_REQUEST_AUTOMODE "esp32/request/autoMode"

#define AWS_IOT_PUBLISH_TOPIC_TEMP "esp32/esp32-to-aws-temp"  // Khai báo Topic gửi nhiệt độ lên server
#define AWS_IOT_PUBLISH_TOPIC_HUM "esp32/esp32-to-aws-hum"  // Khai báo Topic gửi độ ẩm lên server
#define AWS_IOT_PUBLISH_TOPIC_WATER_LEVEL "esp32/esp32-to-aws-water-level"  // Khai báo Topic gửi độ ẩm lên server

#define AWS_IOT_PUBLISH_TOPIC_DATA "esp32/esp32-to-aws-data"  // Khai báo Topic gửi data lên server

#define AWS_IOT_SUBSCRIBE_TOPIC_LED "esp32/led/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi
#define AWS_IOT_SUBSCRIBE_TOPIC_FAN "esp32/fan/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi
#define AWS_IOT_SUBSCRIBE_TOPIC_MOTOR "esp32/motor/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi
#define AWS_IOT_SUBSCRIBE_TOPIC_PUMP "esp32/pump/control" // Khai báo Topic nhận lệnh điều khiển LED sưởi

#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_LED "device/automode/led"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_FAN "device/automode/fan"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_MOTOR "device/automode/motor"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_PUMP "device/automode/pump"

#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_LED "device/automode/threshold/led"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_FAN "device/automode/threshold/fan"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_MOTOR "device/automode/threshold/motor"
#define AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_PUMP "device/automode/threshold/pump"

#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED "device/automode/confirm/led"
#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN "device/automode/confirm/fan"
#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR "device/automode/confirm/motor"
#define AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP "device/automode/confirm/pump"



#define AWS_IOT_PUBLISH_TOPIC_LED   "device/status/led"
#define AWS_IOT_PUBLISH_TOPIC_FAN   "device/status/fan"
#define AWS_IOT_PUBLISH_TOPIC_MOTOR "device/status/motor"
#define AWS_IOT_PUBLISH_TOPIC_PUMP  "device/status/pump"



#define PUBLISH_INTERVAL1 5000  // Khai báo thời gian publish lên server
#define PUBLISH_INTERVAL2 10000  // Khai báo thời gian publish lên server
DHT dht22(DHT22_PIN, DHT22);  // Khai báo đối tượng cảm biến DHT22
WiFiClientSecure net = WiFiClientSecure();  // Client bảo mật (TLS)
MQTTClient client = MQTTClient(256);  // Đối tượng MQTT, buffer 256 byte

int minValue = 0;      // khi cảm biến khô
int maxValue = 2300;   // khi ngập hoàn toàn (chỉnh theo thực tế)

  float temp = 0;
  float hum = 0;
  int sensorValue = 0;
  int levelPercent = 0; 
  String sendLevel;

// NTP config
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;   // múi giờ VN = GMT+7
const int daylightOffset_sec = 0;

//const int sensitivity = 185; // mV/A 
//const float VREF = 3.3;      // Điện áp tham chiếu ESP32 ADC
//const int ADC_RES = 4095;    // Độ phân giải 12 bit
unsigned long lastPublishTime1 = 0;  // Khai báo biến dùng để lưu thời điểm cuối cùng ESP32 gửi dữ liệu lên server
unsigned long lastPublishTime2 = 0;  // Khai báo biến dùng để lưu thời điểm cuối cùng ESP32 gửi dữ liệu lên server

bool autoModeLed = false;
bool autoModeFan = false;
bool autoModeMotor = false;
bool autoModePump = false;

float tempThreshold = 0;
float humThreshold = 0;
String cellThreshold;
String waterThreshold;


//float zeroVoltage = 0; // Điện áp tại 0A

void setup() {
  Serial.begin(9600); // Khởi động serial monitor để debug
  dht22.begin();  // Khởi động DHT22
  pinMode(RELAY_PIN_LED, OUTPUT); // Set RELAY output
  pinMode(RELAY_PIN_FAN, OUTPUT); // Set RELAY output
  pinMode(RELAY_PIN_MOTOR, OUTPUT); // Set RELAY output
  pinMode(RELAY_PIN_PUMP, OUTPUT); // Set RELAY output

  analogSetAttenuation(ADC_11db); // Set ADC nhận điện áp tối đa 3.3V tránh sai kết quả vì defaut ADC ở mức 1.1V
  WiFi.mode(WIFI_STA);  // Mode kết nối WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Kết nối WIFI

  Serial.println("ESP32 connecting to Wi-Fi");

// Loading trong khi connect WIFI
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  // Gọi hàm kết nối server
  connectToAWS();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Syncing time...");
  delay(2000);
  printLocalTime();

    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED);
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR);
    sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP);

    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_LED, autoModeLed, String(tempThreshold, 1));
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_FAN, autoModeFan, String(humThreshold, 1));
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_MOTOR, autoModeMotor, cellThreshold);
    sendConfirmAutomode(AWS_IOT_PUBLISH_TOPIC_AUTOMODE_PUMP, autoModePump, waterThreshold);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
  Serial.println("WiFi lost connection! Reconnecting...");
  WiFi.disconnect();
  WiFi.reconnect();
  delay(2000);
}

if (!client.connected()) {
  Serial.println("MQTT disconnected! Reconnecting...");
  connectToAWS();
}

  minValue = 0;      // khi cảm biến khô
  maxValue = 2300;   // khi ngập hoàn toàn (chỉnh theo thực tế)

  temp = dht22.readTemperature();
  hum = dht22.readHumidity();
  sensorValue = analogRead(WATER_SENSOR_PIN);
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
  // In ra để kiểm tra kết quả hoặc báo lỗi
  if ( isnan(temp) || isnan(hum)) {
    Serial.println("Failed to read from DHT22 sensor!");
  } 

  // Chờ 2 giây để đọc
  delay(2000);
  handleAutoMode(temp, hum, levelPercent);

// millis(): số mili giây đã trôi qua từ lúc ESP32 khởi động. Cụm code này có ý nghĩa: so sánh thời gian để ESP gửi dữ liệu mỗi 4 giây một lần
  if (millis() - lastPublishTime1 > PUBLISH_INTERVAL1 || millis() < lastPublishTime1) { 
    sendToAWS();  // Function gửi dữ liệu lên server
    lastPublishTime1 = millis(); 
  }
  
  client.loop();  // Duy trì kết nối server

}

void connectToAWS() {
  // Cấu hình WiFiClientSecure để sử dụng thông tin đăng nhập server
  // Nạp giấy chứng nhận để kết nối bảo mật TLS
  net.setCACert(AWS_CERT_CA); //
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
  StaticJsonDocument<200> messageData;

  // Ghi dữ liệu nhiệt độ từ sensor
  messageTemp["data_TempC"] = temp;
  // Ghi dữ liệu độ ẩm từ sensor
  messageHum["data_Hum"] = hum;
  // Ghi dữ liệu mực nước từ sensor
  messageWaterLevel["data_WaterLevel"] = sendLevel;
  // Ghi dữ liệu nhiệt độ , độ ẩm từ sensor để lưu trữ
  messageData["deviceId"] = "esp32";
  messageData["timestamp"] = timeString;
  messageData["temperature"] = temp;
  messageData["humidity"] = hum;
  
  char messageBufferTemp[512];
  char messageBufferHum[512];
  char messageBufferWaterLevel[512];
  char messageBufferData[512];
  // Chuyển JSON thành string
  serializeJson(messageTemp, messageBufferTemp); 
  serializeJson(messageHum, messageBufferHum);  
  serializeJson(messageWaterLevel, messageBufferWaterLevel);  
  serializeJson(messageData, messageBufferData);
  // Publish lên topic server
  client.publish(AWS_IOT_PUBLISH_TOPIC_TEMP, messageBufferTemp);
  client.publish(AWS_IOT_PUBLISH_TOPIC_HUM, messageBufferHum);
  client.publish(AWS_IOT_PUBLISH_TOPIC_WATER_LEVEL, messageBufferWaterLevel);
  client.publish(AWS_IOT_PUBLISH_TOPIC_DATA, messageBufferData);

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

void sendToAwsRelayStatus(const char* topic, int pin) {
  int state = digitalRead(pin); // Đọc trạng thái thực tế của relay
  StaticJsonDocument<50> messageStatus;
  messageStatus["status"] = (state == HIGH) ? "ON" : "OFF";

  char buffer[100];
  serializeJson( messageStatus, buffer);
  client.publish(topic, buffer);
}

void sendConfirmAutomode(const char* topic, bool autoMode, String threshold ) {

  // Gửi lại trạng thái
  StaticJsonDocument<150> doc;


  doc["autoMode"] = autoMode;
  doc["selectedThreshold"] = threshold;

  char buffer[150];

  serializeJson(doc, buffer);

  client.publish(topic, buffer);

}
// Xử lý dữ liệu app gửi lên topic
void messageHandler(String &topic, String &payload) {
   Serial.println("received:");
  Serial.println("- topic: " + topic);
  Serial.println("- payload:");
  Serial.println(payload);

  // Thêm kiểm tra JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(error.c_str());
    Serial.println("⚠️ Payload lỗi, không xử lý tiếp!");
    return;
  }

  if (!doc.containsKey("status") && !doc.containsKey("threshold")) {
    Serial.println("⚠️ JSON không có key hợp lệ!");
    return;
  }

  const char* messageStatus = doc["status"] | "";     // fallback rỗng để tránh null
  const char* messageThreshold = doc["threshold"] | "";

  // --- Debug thêm ---
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
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED);
      Serial.println("LED : ON");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_FAN)) {
      digitalWrite(RELAY_PIN_FAN, HIGH);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
      Serial.println("FAN : ON");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_MOTOR)) {
      digitalWrite(RELAY_PIN_MOTOR, HIGH);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR);
      Serial.println("MOTOR : ON");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_PUMP)) {
      digitalWrite(RELAY_PIN_PUMP, HIGH);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP);
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
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_LED, RELAY_PIN_LED);
      Serial.println("LED : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_FAN)) {
      digitalWrite(RELAY_PIN_FAN, LOW);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_FAN, RELAY_PIN_FAN); 
      Serial.println("FAN : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_MOTOR)) {
      digitalWrite(RELAY_PIN_MOTOR, LOW);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_MOTOR, RELAY_PIN_MOTOR);
      Serial.println("MOTOR : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_PUMP)) {
      digitalWrite(RELAY_PIN_PUMP, LOW);
      sendToAwsRelayStatus(AWS_IOT_PUBLISH_TOPIC_PUMP, RELAY_PIN_PUMP);
      Serial.println("PUMP : OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_LED)) {
      autoModeLed = false;
      Serial.println("AUTO MODE LED: OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_FAN)) {
      autoModeFan = false;
      Serial.println("AUTO MODE FAN: OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_MOTOR)) {
      autoModeMotor = false;
      Serial.println("AUTO MODE MOTOR: OFF");
    }
    else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_PUMP)) {
      autoModePump = false;
      Serial.println("AUTO MODE PUMP: OFF");
    }
  }
  if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_LED)) {
     if (strlen(messageThreshold) > 0)
  tempThreshold = atof(doc["threshold"]);  // số
}
else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_FAN)) {
   if (strlen(messageThreshold) > 0)
  humThreshold = atof(doc["threshold"]);   // số
}
else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_MOTOR)) {
   if (strlen(messageThreshold) > 0)
  cellThreshold = String((const char*)doc["threshold"]);  // chữ
}
else if (topic.equals(AWS_IOT_SUBSCRIBE_TOPIC_AUTOMODE_THRESHOLD_PUMP)) {
   if (strlen(messageThreshold) > 0)
  waterThreshold = String((const char*)doc["threshold"]); // chữ
}

}


void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void handleAutoMode(float temp, float hum, int levelPercent) {
    // --- Điều khiển LED sưởi ---
  if (autoModeLed) {
    if (temp < (tempThreshold - 0.2)) digitalWrite(RELAY_PIN_LED, HIGH);
    else if (temp > (tempThreshold + 0.2)) digitalWrite(RELAY_PIN_LED, LOW);
}

if (autoModeFan) {
    if (hum < humThreshold) digitalWrite(RELAY_PIN_FAN, LOW);
    else if (hum > humThreshold) digitalWrite(RELAY_PIN_FAN, HIGH);
}

//if (autoModeMotor) {
//    if (hum < (humThreshold + 0.5)) digitalWrite(RELAY_PIN_LED, LOW);
//    else if (hum > (humThreshold - 0.5)) digitalWrite(RELAY_PIN_LED, HIGH);
//}

if (autoModePump) {
    if (waterThreshold == "Thấp") {
      if (levelPercent < 30){
        digitalWrite(RELAY_PIN_PUMP, HIGH);
      }
      else if (levelPercent < 85) {
        digitalWrite(RELAY_PIN_PUMP, LOW);
      }
    }
    else  if (waterThreshold == "Trung bình") {
      if (levelPercent < 70){
        digitalWrite(RELAY_PIN_PUMP, HIGH);
      }
      else if (levelPercent < 85) {
        digitalWrite(RELAY_PIN_PUMP, LOW);
      }
    }
}
}


