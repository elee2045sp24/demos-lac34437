#include "M5StickCPlus2.h"
#include <WiFi.h>
#include "esp_wpa2.h"  //wpa2 library for connections to Enterprise networks
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>

bool setupComplete = true;
Preferences preferences;

WebServer server(80);
HTTPClient http;
const byte DNS_PORT = 53;
DNSServer dnsServer;

String server_root = "https://bbs.ugavel.com/";
String discover_route = server_root + "discovered";
String sync_route = server_root + "sync";
String update_route = server_root + "getapp";
String station_mac = "none";

bool hand_state = false;

// Time client RTC sync variables
#if defined(ARDUINO)
#define NTP_TIMEZONE "UTC-7" // Setting timezone to EST 
#define NTP_SERVER1 "0.pool.ntp.org"
#define NTP_SERVER2 "1.pool.ntp.org"
#define NTP_SERVER3 "2.pool.ntp.org"

#if __has_include(<esp_sntp.h>)
#include <esp_sntp.h>
#define SNTP_ENABLED 1
#elif __has_include(<sntp.h>)
#include <sntp.h>
#define SNTP_ENABLED 1
#endif
#endif

//Mic variables
unsigned long micTimerStart = 0;
const unsigned long micInterval = 3000; // 3 seconds
int32_t shortAverage = 0;
int32_t shortSum = 0;
size_t shortSampleCount = 0;

static constexpr const size_t record_number = 200;
static constexpr const size_t record_length = 240;
static constexpr const size_t record_size = record_number * record_length;
static constexpr const size_t record_samplerate = 44100;
static int16_t prev_y[record_length];
static int16_t prev_h[record_length];
static size_t rec_record_idx = 2;
static size_t draw_record_idx = 0;
static int16_t *rec_data;

#define PIN_CLK 0
#define PIN_DATA 34

//MQTT variables
const char broker[] = "eduoracle.ugavel.com";
int mqtt_port = 1883;
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
const char topic_clicker_status[] = "ugaelee2045demo/user/topic_clicker_status";
const char topic_clicker_data[] = "ugaelee2045demo/user/topic_clicker_data";
char buffer[100];

#pragma pack(1)

struct MyData {
  float accel_x;
  float accel_y;
  float accel_z;
  float gyro_x;
  float gyro_y;
  float gyro_z;
  float bat_voltage;
  int micValue;
  bool handState;
  char currentTime[20];  // Added for time data as string (format: "YYYY-MM-DD HH:mm:ss")
  char firstname[20];    // Adjust the size as needed
  char lastname[20];     // Adjust the size as needed
};

bool connectWifi() {
  station_mac = WiFi.macAddress();
  Serial.println(station_mac);
  String username = preferences.getString("username", "");
  String password = preferences.getString("password", "");
  String ssid = preferences.getString("ssid", "");
  bool is_eap = preferences.getBool("eap", false);

  StickCP2.Display.setCursor(7, 20, 2);
  StickCP2.Display.clear();
  StickCP2.Display.println("Connecting to wifi");
  StickCP2.Display.println(ssid);

  WiFi.mode(WIFI_STA);


  if (is_eap) {
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)"", 0);
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)username.c_str(), username.length());
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)password.c_str(), password.length());
    esp_wifi_sta_wpa2_ent_enable();

    WiFi.begin(ssid.c_str());  //connect to wifi
  } else {
    WiFi.begin(ssid.c_str(), password.c_str());
  }

  int num_attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
    num_attempts++;

    if (num_attempts > 60) {
      ESP.restart();
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

void doWifiWifiSetup() {
  StickCP2.Display.setCursor(0, 20, 2);
  StickCP2.Display.clear();
  String randomPassword = generateRandomPassword(8);  // Min of 8 characters needed to set the softAP password
  String macAddress = WiFi.macAddress();
  String softAPssid = macAddress.substring(12);
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(softAPssid.c_str(), randomPassword.c_str());
  Serial.println(randomPassword);

  IPAddress apIP = WiFi.softAPIP();

  StickCP2.Display.println("Wifi setup");
  StickCP2.Display.println("Connect Wifi to");
  StickCP2.Display.println(softAPssid.c_str());
  StickCP2.Display.println("and go to");

  StickCP2.Display.println(apIP);

  StickCP2.Display.println("Password is");
  StickCP2.Display.println(randomPassword);


  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handle_root);
  server.on("/generate_204", handle_root);
  server.on("/hotspot-detect.html", handle_root);
  server.on("/success.txt", handle_root);
  server.on("/connecttest.txt", handle_root);
  server.on("/wpad.dat", handle_root);
  server.onNotFound(handle_root);
  server.on("/type", handle_wifi);
  server.on("/set", handle_form);
  server.begin();

  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
}

String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

String generateRandomPassword(int length) {
  String charset = "0123456789";
  String password = "";

  for (int i = 0; i < length; i++) {
    int randomIndex = random(charset.length());
    password += charset[randomIndex];
  }

  return password;
}

boolean isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

boolean captivePortal() {
  if (!isIp(server.hostHeader())) {
    Serial.println("Request redirected to captive portal");
    server.sendHeader("Location", String("http://192.168.4.1"), true);
    server.send(302, "text/plain", "");
    server.client().stop();
    return true;
  }
  return false;
}

void handle_root() {
  if (captivePortal()) {
    return;
  }
  String HTML = "<!DOCTYPE html>\
  <html>\
  <body>\
  <form action=\"/type\" method=\"POST\">";
  // Add form fields for first and last name
  HTML += "First Name: <input type='text' name='firstname' autocomplete='off'/><br>";
  HTML += "Last Name: <input type='text' name='lastname' autocomplete='off'/><br>";
  HTML = HTML + "WPA (Home WiFi)<input type=\"radio\" name=\"type\" value=\"wpa\"/><br>\
  EAP (Enterpise / PAWs secure)<input type=\"radio\" name=\"type\" value=\"eap\"/><br>\
  <input type=\"submit\">\
  </body>\
  </html>";
  server.send(200, "text/html", HTML);
}

void handle_wifi() {
  if (captivePortal()) {
    return;
  }
  String firstname = server.arg("firstname");  // Get the entered first name
  String lastname = server.arg("lastname");    // Get the entered last name
  preferences.putString("firstname", firstname);
  preferences.putString("lastname", lastname);
  String type = server.arg("type");  // Declare 'type' variable

  String HTML = "<!DOCTYPE html>\
  <html>\
  <body>\
  <form action=\"/set\" method=\"POST\">";

  int n = WiFi.scanNetworks();

  // Check if there are any networks available
  if (n > 0) {
    HTML += "Selected Wi-Fi network type: " + type + "<br>";

    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      bool isEAP = (WiFi.encryptionType(i) == WIFI_AUTH_WPA2_ENTERPRISE);

      // Only display networks that match the selected type
      if ((type == "wpa" && !isEAP) || (type == "eap" && isEAP)) {
        HTML += "<input type='radio' name='ssid' value='" + ssid + "'>" + ssid + "<br>";
      }
    }

    HTML += "password<input type='password' name='password' autocomplete='off'/><br>";

    // Check if the selected type is EAP and include the username field
    if (type == "eap") {
      HTML += "username (UGA ID not 811) <input type='text' name='username' autocomplete='off'/><br>";
    }

    HTML += "<input type='submit'>";
  } else {
    HTML += "No networks found.";
  }

  HTML += "</form></body></html>";
  server.send(200, "text/html", HTML);
}

void handle_form() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String username = server.arg("username");
  bool use_eap = false;
  if (username != "") {
    use_eap = true;
  }

  preferences.putString("username", username);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putBool("eap", use_eap);

  String HTML = "<!DOCTYPE html>\
  <html>\
  <body>\
  <p>Success.  Disconnecting</p>\
  </body>\
  </html>";

  server.send(200, "text/html", HTML);
  preferences.end();
  ESP.restart();
}

// Taken from https://github.com/m5stack/M5StickCPlus2/tree/master/examples Basic/mic
void getMicInfo() {
  if (StickCP2.Mic.isEnabled()) {
    static constexpr int shift = 6;
    auto data = &rec_data[0];

    // Check if the timer has elapsed
    if (millis() - micTimerStart > micInterval) {
      micTimerStart = millis(); // Reset the timer

      shortAverage = (shortSampleCount > 0) ? (shortSum / shortSampleCount) : 0;

      // Use shortAverage as needed (e.g., publish to MQTT)
      Serial.print("Short Average Noise Level: ");
      Serial.println(shortAverage);

      // Reset counters for the next interval
      shortSum = 0;
      shortSampleCount = 0;


    }
    // Accumulate data for short average
    for (size_t i = 0; i < record_length; ++i) {
      shortSum += abs(data[i] >> shift);
    }
    shortSampleCount += record_length;
  }
}

void I2Cinit() {
  rec_data = (typeof(rec_data))heap_caps_malloc(record_size *

                                                  sizeof(int16_t),
                                                MALLOC_CAP_8BIT);
  memset(rec_data, 0, record_size * sizeof(int16_t));
  StickCP2.Speaker.setVolume(255);

  StickCP2.Speaker.end();
  StickCP2.Mic.begin();

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = 44100,
    .bits_per_sample =
      I2S_BITS_PER_SAMPLE_16BIT,  // is fixed at 12bit, stereo, MSB
    .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
  #if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 1, 0)
    .communication_format =
      I2S_COMM_FORMAT_STAND_I2S,  // Set the format of the communication.
  #else                             // 设置通讯格式
    .communication_format = I2S_COMM_FORMAT_I2S,
  #endif
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 3,
    .dma_buf_len = 256,
  };

  i2s_pin_config_t pin_config;
  #if (ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 3, 0))
  pin_config.mck_io_num = I2S_PIN_NO_CHANGE;
  #endif
  pin_config.bck_io_num = I2S_PIN_NO_CHANGE;
  pin_config.ws_io_num = PIN_CLK;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config.data_in_num = PIN_DATA;

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

void RTCinit() {
  configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  #if SNTP_ENABLED
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    Serial.print('.');
    delay(1000);
  }
  #else
  delay(1600);
  struct tm timeInfo;
  while (!getLocalTime(&timeInfo, 1000)) {
    Serial.print('.');
  };
  #endif

  Serial.println("\r\n NTP Connected.");

  time_t t = time(nullptr) + 1;  // Advance one second.
  while (t > time(nullptr));  /// Synchronization in seconds
  StickCP2.Rtc.setDateTime(gmtime(&t));

}

void onMqttMessage(int messageSize) {
  Serial.print("Received a message from topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");

  while (mqttClient.available()) {
    if (mqttClient.messageTopic() == topic_clicker_data) {
      // Read the incoming data into a MyData structure
      MyData receivedData;
      mqttClient.read((uint8_t*)&receivedData, sizeof(receivedData));

      // Process the received data
      Serial.println("Received complete message:");
      Serial.print("Accelerometer X: ");
      Serial.println(receivedData.accel_x);
      Serial.print("Accelerometer Y: ");
      Serial.println(receivedData.accel_y);
      Serial.print("Accelerometer Z: ");
      Serial.println(receivedData.accel_z);
      Serial.print("Gyroscope X: ");
      Serial.println(receivedData.gyro_x);
      Serial.print("Gyroscope Y: ");
      Serial.println(receivedData.gyro_y);
      Serial.print("Gyroscope Z: ");
      Serial.println(receivedData.gyro_z);
      Serial.print("Battery Voltage: ");
      Serial.println(receivedData.bat_voltage);
      Serial.print("Mic Value: ");
      Serial.println(receivedData.micValue);
      Serial.print("Hand State: ");
      Serial.println(receivedData.handState ? "True" : "False");
      Serial.print("Current Time: ");
      Serial.println(receivedData.currentTime);
      Serial.print("First Name: ");
      Serial.println(receivedData.firstname);
      Serial.print("Last Name: ");
      Serial.println(receivedData.lastname);
    }
  }
}

void formatTime(char* buffer, size_t bufferSize, time_t timestamp) {
  struct tm timeinfo;
  localtime_r(&timestamp, &timeinfo);

  // Convert components to strings and concatenate them
  snprintf(buffer, bufferSize, "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void sendMQTT() {
  // Create a struct to hold the data
  MyData data;

  // Fill the struct with required data
  auto imu_update = StickCP2.Imu.update();
  if (imu_update) {
    auto imuData = StickCP2.Imu.getImuData();
    data.accel_x = imuData.accel.x;
    data.accel_y = imuData.accel.y;
    data.accel_z = imuData.accel.z;
    data.gyro_x = imuData.gyro.x;
    data.gyro_y = imuData.gyro.y;
    data.gyro_z = imuData.gyro.z;

    data.bat_voltage = StickCP2.Power.getBatteryVoltage();
    int micValue = 0;
    if (StickCP2.Mic.isEnabled()) {
      auto mic_data = &rec_data[0];
      for (size_t i = 0; i < record_length; ++i) {
        micValue += abs(mic_data[i] >> 6);  // Assuming shift value 6
      }
      micValue /= record_length;
    }
    data.micValue = micValue;

    data.handState = hand_state;

    // Get current time as a formatted string
    formatTime(data.currentTime, sizeof(data.currentTime), time(nullptr));

    // Get first and last names from preferences
    String firstname = preferences.getString("firstname", "");
    String lastname = preferences.getString("lastname", "");

    // Ensure strings are null-terminated
    strlcpy(data.firstname, firstname.c_str(), sizeof(data.firstname));
    strlcpy(data.lastname, lastname.c_str(), sizeof(data.lastname));
  }

  // Print the composed message
  Serial.println("Sending MQTT message...");

  // Publish the raw bytes over MQTT
  mqttClient.beginMessage(topic_clicker_data);
  mqttClient.write((uint8_t*)&data, sizeof(data));
  mqttClient.endMessage();
}


void setup() {
  pinMode(4, OUTPUT); // Set HOLD pin 04 as output
  digitalWrite(4, HIGH);  //IMPORTANT, Set HOLD pin to high to maintain power supply or M5StickCP2 will turn off

  auto cfg = M5.config();
  StickCP2.begin(cfg);
  StickCP2.Display.init();
  StickCP2.Display.startWrite();
  StickCP2.Display.setCursor(7, 20, 2);
  StickCP2.Display.setRotation(1);
  StickCP2.Display.setTextColor(GREEN);
  StickCP2.Display.setTextDatum(top_center);
  StickCP2.Display.endWrite();

  preferences.begin("my-app", false);

  StickCP2.Display.clear();
  StickCP2.Display.println("Hold button for Wifi Config");

  delay(2000);  // Two second config delay

  if (digitalRead(37) == LOW) {  // Reads button A
    doWifiWifiSetup();
  }

  connectWifi();  // from flash
  StickCP2.Display.clear();

  WiFi.mode(WIFI_STA);

  // Sync RTC clock with NTP
  Serial.println("Synching Clock");
  StickCP2.Display.drawString("Connected to Wifi", StickCP2.Display.width() / 2, StickCP2.Display.height() / 2);
  StickCP2.Display.drawString("RTC Clock synching with NTP", StickCP2.Display.width() / 2, StickCP2.Display.height() / 2 + 12);
  RTCinit();

  //Mic setup call
  Serial.println("Init Mic");
  I2Cinit();

  //MQTT setup
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setUsernamePassword("giiuser", "giipassword");
  mqttClient.connect(broker, mqtt_port);
  mqttClient.subscribe(topic_clicker_data);
  mqttClient.subscribe(topic_clicker_status);

  Serial.println("Setup finished");
  StickCP2.Display.clear();
}

void loop() {
  static unsigned long lastUpdateTime = 0;
  const unsigned long updateInterval = 2000;  // Set the update interval to 1000 milliseconds (1 second)

  mqttClient.poll(); // Checks for MQTT messages
  StickCP2.update(); // Checks for button presses

  if (StickCP2.BtnA.wasPressed()) {
    hand_state = !hand_state;
    sendMQTT(); // Send data and hand_state state to topic_clicker_data
    StickCP2.Display.clear();
    int vol = StickCP2.Power.getBatteryVoltage();
    StickCP2.Display.setCursor(10, 10, 2);
    StickCP2.Display.printf("BAT: %dmv", vol);
  }

  // Display the hand_state and reminder message on the StickCP2 display
  StickCP2.Display.setCursor(10, 70, 2);
  StickCP2.Display.printf("Hand State: %s", hand_state ? "Raised" : "Lowered");
  StickCP2.Display.setCursor(10, 100, 2);
  StickCP2.Display.println("Press Button A to change state");

  // Check if it's time to update the battery voltage display
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= updateInterval) {
    StickCP2.Display.fillRect(30, 0, 100, 30, 0);
    int vol = StickCP2.Power.getBatteryVoltage();
    StickCP2.Display.setCursor(10, 10, 2);
    StickCP2.Display.printf("BAT: %dmv", vol);

    // Update the last update time
    lastUpdateTime = currentTime;
  }

  // Keep mic recording
  if (StickCP2.Mic.isEnabled()) {
    auto data = &rec_data[0];
    if (StickCP2.Mic.record(data, record_length, record_samplerate)) {
      // Call the getMicInfo function for short average calculation 
      getMicInfo();
    }
  }
  delay(100);
}