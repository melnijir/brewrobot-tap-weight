#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "HX711.h"
#include "beer.h"

/*
 * Base constants
 */

// Wifi settings
const char* ssid           = ""; // REQUIRED - WiFi SSID
const char* password       = ""; // REQUIRED - WiFi password

// MQTT broker settings
const char *mqtt_broker    = ""; // REQUIRED - MQTT broker IP address
const char *mqtt_username  = ""; // REQUIRED - MQTT broker user
const char *mqtt_password  = ""; // REQUIRED - MQTT broker password
const char *mqtt_device    = "beerWeight";
const char *mqtt_base_addr = "homeassistant/sensor/";
const int mqtt_port = 1883;

// Sync settings
const int weight_change_sync  = 15;    // Sync every 60s
const int weight_regular_sync = 10*60; // Sync every 10 minutes even if there is no change

// Output/input PIN settings
const int LOADCELL_DOUT_PIN = 22;
const int LOADCELL_SCK_PIN  = 21;
const int BUTTON1PIN        = 35;
const int BUTTON2PIN        = 0;

// Graphics settings
const int lcd_msg_text_spacing = 20;
const int lcd_msg_text_top     = 10;
const int lcd_msg_text_left    = 10;
const int lcd_msg_dellay       = 1000;
const int lcd_head_text_left   = 130;
const int lcd_head_dellay      = 1000;
const int lcd_width            = 240;
const int lcd_height           = 135;

/*
 * Loop variables
 */
int   lcd_text_offset     = lcd_msg_text_top; // Display text spacing
bool  tara_next_run       = true;             // Should we do tara on the next run?
bool  redraw_screen       = true;             // Should we redraw the screen with weight?
float weight_offset       = 0;                // Offset saved from the last boot
int   last_measure        = 0;                // Measured value change detection
bool  measurement_changed = false;            // Detect if measured weight changed
bool  measurement_send    = false;            // Should be a new measurement send?
int   regular_sync        = 0;                // Counter if we should sync regulary
int   change_sync         = 0;                // Counter if we should sync after change

/*
 * Display, scale, persistent storage and MQTT
 */
TFT_eSPI tft = TFT_eSPI();
HX711 scale;
Preferences preferences;
WiFiClient esp_client;
PubSubClient pubsub_client(esp_client);

String get_device_id() {
  String chip_ip = String(WiFi.macAddress().substring(9, 11)+
                          WiFi.macAddress().substring(12,14)+
                          WiFi.macAddress().substring(15,17));
  return (String(mqtt_device)+chip_ip);
}

void set_lcd_newline() {
  lcd_text_offset+=lcd_msg_text_spacing;
  tft.setCursor(lcd_msg_text_left, lcd_text_offset, 2);
}

void clear_lcd_text() {
  tft.fillRect(lcd_head_text_left, 0, lcd_width-lcd_head_text_left, lcd_height, TFT_BLACK);
}

bool connect_to_wifi() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(lcd_msg_text_left, lcd_text_offset, 2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  tft.setTextSize(1);
  tft.print("Connecting to: ");
  tft.print(ssid);
  set_lcd_newline();
  WiFi.begin(ssid, password);
  int connect_timeout = 10;
  while (WiFi.status() != WL_CONNECTED && connect_timeout-- > 0) {
      delay(500);
      tft.print(".");
  }
  set_lcd_newline();
  if (connect_timeout > 0) {
    tft.print("CONNECTED");
    delay(lcd_msg_dellay);
    return true;
  } else {
    tft.print("CONN ERR!");
    delay(lcd_msg_dellay);
    return false;
  }
}

bool diconnect_from_wifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 0, 125, 125, image_data_RGB);
  lcd_text_offset = lcd_msg_text_top;
  return true;
}

bool put_mqtt_message(String topic, String payload) {
  pubsub_client.setServer(mqtt_broker, mqtt_port);
  String client_id = get_device_id();
  set_lcd_newline();
  tft.print("Sending MQTT message:");
  set_lcd_newline();
  if (pubsub_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      tft.print("CONNECTED");
      delay(500);
      pubsub_client.beginPublish(topic.c_str(), payload.length(), false);
      for (size_t pos = 0; pos <= payload.length(); pos+=64) {
          pubsub_client.print(payload.substring(pos,pos+64).c_str());
      }
      int res = pubsub_client.endPublish();
      pubsub_client.disconnect();
      if (res == 1)
        tft.print(" SENT");
      else
        tft.print(" ERR!");
      delay(lcd_msg_dellay);
      return (res == 1);
  } else {
      tft.print("CONN ERR!");
      delay(lcd_msg_dellay);
      return false;
  }
}

bool send_device_registration() {
  String registration_json = R"({
   "device_class":"weight",
   "state_topic":"homeassistant/sensor/XXX/state",
   "unit_of_measurement":"kg",
   "value_template":"{{ value_json.weight }}",
   "unique_id":"XXX",
   "device":{
      "identifiers":[
          "XXX"
      ],
      "name":"Beer weight",
      "manufacturer": "Brewrobot sensors",
      "model": "Beer weight sensor ESP32",
      "model_id": "A1",
      "serial_number": "00001",
      "hw_version": "1.00",
      "sw_version": "2025.0.0",
      "configuration_url": "https://github.com/melnijir/brewrobot-tap-weight"
    }
  })";
  registration_json.replace("XXX",get_device_id());
  return put_mqtt_message(String(mqtt_base_addr+get_device_id()+"/config"), registration_json);
}

bool send_device_state(float value) {
  String state_json = R"({
   "weight":XXX
  })";
  state_json.replace("XXX",String(value));
  return put_mqtt_message(String(mqtt_base_addr+get_device_id()+"/state"), state_json);
}

bool connect_and_register() {
  bool ret = false;
  if (connect_to_wifi()) {
    ret = send_device_registration();
  }
  diconnect_from_wifi();
  return ret;
}

bool connect_and_send(float value) {
  bool ret = false;
  if (connect_to_wifi()) {
    ret = send_device_state(value);
  }
  diconnect_from_wifi();
  redraw_screen = true;
  return ret;
}

void IRAM_ATTR toggleButton1() {
  preferences.clear();
  weight_offset = 0;
}

void IRAM_ATTR toggleButton2() {
  tara_next_run = true;
}

void setup(void) {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.pushImage(0, 0, 125, 125, image_data_RGB);

  Serial.begin(115200);
  Serial.println("Serial connection ready.");

  pinMode(BUTTON1PIN, INPUT);
  pinMode(BUTTON2PIN, INPUT);
  attachInterrupt(BUTTON1PIN, toggleButton1, FALLING);
  attachInterrupt(BUTTON2PIN, toggleButton2, FALLING);
  Serial.println("Buttons setup completed.");

  // Set scale
  // Example - used weight is 80kg, calibrated measurements are average -1989144
  //           -1989144 / 80 == -24864,3
  //           calibration factor is -24864,3
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(-29391.255);
  Serial.println("Scale setup ready.");

  // Load persistent storage
  preferences.begin("brewrobot", false);
  weight_offset = preferences.getFloat("weight", 0);
  Serial.println("Persistent storage loaded.");

  // Register ourselves as a MQTT device, try max. 5 times
  int connect_timeout = 5;
  while (!connect_and_register() && connect_timeout-- > 0) {
    delay(500);
  }
}

void loop() {
  // If tara is wanted, do tara
  if (tara_next_run) {
    clear_lcd_text();
    tft.setCursor(lcd_head_text_left, 35, 2);
    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.setTextSize(2);
    tft.println("Tara");
    delay(lcd_head_dellay);
    scale.tare();
    tft.setCursor(lcd_head_text_left, 70, 2);
    tft.println("Done");
    delay(lcd_head_dellay);
    tara_next_run = false;
    redraw_screen = true;
  }
  // Measure only if scale is ready
  if (scale.is_ready()) {
    float weight = -scale.get_units(10);
    if (weight < 0.05) {
      weight = 0;
    }
    weight += weight_offset;
    int current_measure = (weight * 10);
    float rounded_weight = (float(current_measure) / 10);
    if (last_measure != current_measure) {
      last_measure = current_measure;
      measurement_changed = true;
      redraw_screen = true;
    }
    if (redraw_screen) {
      clear_lcd_text();
      tft.setCursor(lcd_head_text_left, 15, 2);
      tft.setTextColor(TFT_WHITE,TFT_BLACK);
      tft.setTextSize(2);
      tft.println("Tapped:");
      tft.setCursor(lcd_head_text_left, 60, 2);
      tft.setTextSize(3);
      tft.println(String(rounded_weight, 1));
      tft.setCursor(lcd_head_text_left, 110, 2);
      tft.setTextSize(1);
      tft.println("liters of beer");
      redraw_screen = false;
    }
    change_sync++;
    regular_sync++;
    if (measurement_changed) {
      preferences.putFloat("weight", weight);
      measurement_changed = false;
      measurement_send = true;
    }
    if (((change_sync % weight_change_sync) == 0) && measurement_send) {
      measurement_send = !connect_and_send(rounded_weight);
    }
    if ((regular_sync % weight_regular_sync) == 0) {
      connect_and_send(rounded_weight);
    }
  } else {
    clear_lcd_text();
    tft.setCursor(lcd_head_text_left, 35, 2);
    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.setTextSize(2);
    tft.println("Scale");
    tft.setCursor(lcd_head_text_left, 70, 2);
    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.setTextSize(2);
    tft.println("error :(");
    redraw_screen = true;
  }
  // Sleep until next measurement
  delay(1000);
}
