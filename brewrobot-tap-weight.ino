#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "HX711.h"
#include "beer.h"
#include "wifi_online.h"
#include "wifi_offline.h"
#include "wifi_sync.h"
#include "mqtt_online.h"
#include "mqtt_offline.h"
#include "mqtt_sync.h"


/*
 * Base constants
 */

// Wifi settings
const char* ssid                   = "";              // REQUIRED - WiFi SSID
const char* password               = "";              // REQUIRED - WiFi password
const unsigned long reconnect_time = 60000;           // Try to reconnect every 60s

// MQTT broker settings
const char *mqtt_broker            = "";              // REQUIRED - MQTT broker IP address
const char *mqtt_username          = "";              // REQUIRED - MQTT broker user
const char *mqtt_password          = "";              // REQUIRED - MQTT broker password
const char *mqtt_device            = "beerWeight";
const char *mqtt_base_addr         = "homeassistant/sensor/";
const int mqtt_port                = 1883;

// Sync settings
const int weight_change_sync       = 5000;            // Sync on change 5 seconds
const int weight_regular_sync      = 60000*5;         // Sync at least every 5 minutes even if there is no change

// Screensaver
const int screensaver_timeout      = 60000*5;         // Run screensaver every 5 minutes

// Output/input PIN settings
const int LOADCELL_DOUT_PIN = 22;
const int LOADCELL_SCK_PIN  = 21;
const int BUTTON1PIN        = 35;
const int BUTTON2PIN        = 0;

// Graphics settings
const int lcd_head_first_top    = 25;
const int lcd_head_second_top   = 60;
const int lcd_head_text_left    = 125;
const int lcd_head_dellay       = 1000;
const int lcd_width             = 240;
const int lcd_height            = 135;
const int lcd_logo_beer_width   = 100;
const int lcd_logo_beer_height  = 125;
const int lcd_logo_conn_width   = 14;
const int lcd_logo_conn_height  = 14;
const int lcd_logo_conn_top     = 0;
const int lcd_logo_conn_left    = 208;
const int lcd_logo_conn_spacing = 4;
enum lcd_logo_type { ONLINE, OFFLINE, SYNC, CLEAR };

/*
 * Loop variables
 */
bool  tara_next_run             = true;             // Should we do tara on the next run?
bool  redraw_screen             = true;             // Should we redraw the screen with weight?
float weight_offset             = 0;                // Offset saved from the last boot
int   last_measure              = 0;                // Measured value change detection
bool  measurement_changed       = false;            // Detect if measured weight changed
bool  measurement_send          = false;            // Should be a new measurement send?
unsigned long last_change_sync  = 0;                // Value in miliseconds when was the last measured value update
unsigned long wifi_last_conn    = 0;                // Value in miliseconds when was the last connection time for WiFi
unsigned long mqtt_last_conn    = 0;                // Value in miliseconds when was the last connection time for MQTT
unsigned long last_screensaver  = 0;                // Value in miliseconds when was the last screensaver run
lcd_logo_type wifi_state        = lcd_logo_type::OFFLINE;
lcd_logo_type mqtt_state        = lcd_logo_type::OFFLINE;

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

void write_lcd_first(String text) {
  tft.setCursor(lcd_head_text_left, lcd_head_first_top, 2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  tft.setTextSize(2);
  tft.println(text);
}

void write_lcd_second(String text) {
  tft.setCursor(lcd_head_text_left, lcd_head_second_top, 2);
  tft.setTextSize(3);
  tft.println(text);
}

void update_lcd_text(bool show, float weight = 0) {
  tft.fillRect(lcd_head_text_left, lcd_head_first_top, lcd_width-lcd_head_text_left, lcd_height-lcd_head_first_top, TFT_BLACK);
  if (!show)
    return;
  write_lcd_first("Tapped:");
  write_lcd_second(String(weight, 1));
  tft.setCursor(lcd_head_text_left, 110, 2);
  tft.setTextSize(1);
  tft.println("liters of beer");
}

void screensaver() {
  tft.fillScreen(TFT_BLACK);
  for (size_t pos_x = 0; pos_x < lcd_width; pos_x++) {
    tft.pushImage(pos_x, 5, lcd_logo_beer_width, lcd_logo_beer_height, image_data_beer);
    delay(25);
    tft.fillRect(pos_x, 5, 1, lcd_logo_beer_height, TFT_BLACK);
  }
  redraw_screen = true;
  update_lcd_graphics();
}

void draw_beer_logo(bool show) {
  if (show)
    tft.pushImage(0, 5, lcd_logo_beer_width, lcd_logo_beer_height, image_data_beer);
  else
    tft.fillRect(0, 5, lcd_logo_beer_width, lcd_logo_beer_height, TFT_BLACK);
}

void draw_wifi_logo(lcd_logo_type type) {
  switch(type) {
    case lcd_logo_type::ONLINE: {
        tft.pushImage(lcd_logo_conn_left, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, image_data_wifi_online);
      }
      break;
    case lcd_logo_type::OFFLINE: {
        tft.pushImage(lcd_logo_conn_left, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, image_data_wifi_offline);
      }
      break;
    case lcd_logo_type::SYNC: {
        tft.pushImage(lcd_logo_conn_left, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, image_data_wifi_sync);
      }
      break;
    case lcd_logo_type::CLEAR: {
        tft.fillRect(lcd_logo_conn_left, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, TFT_BLACK);
      }
      break;
  }
}

void draw_mqtt_logo(lcd_logo_type type) {
  switch(type) {
    case lcd_logo_type::ONLINE: {
        tft.pushImage(lcd_logo_conn_left+lcd_logo_conn_width+lcd_logo_conn_spacing, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, image_data_mqtt_online);
      }
      break;
    case lcd_logo_type::OFFLINE: {
        tft.pushImage(lcd_logo_conn_left+lcd_logo_conn_width+lcd_logo_conn_spacing, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, image_data_mqtt_offline);
      }
      break;
    case lcd_logo_type::SYNC: {
        tft.pushImage(lcd_logo_conn_left+lcd_logo_conn_width+lcd_logo_conn_spacing, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, image_data_mqtt_sync);
      }
      break;
    case lcd_logo_type::CLEAR: {
        tft.fillRect(lcd_logo_conn_left+lcd_logo_conn_width+lcd_logo_conn_spacing, lcd_logo_conn_top, lcd_logo_conn_width, lcd_logo_conn_height, TFT_BLACK);
      }
      break;
  }
}

void update_lcd_conn_graphics() {
  draw_wifi_logo(wifi_state);
  draw_mqtt_logo(mqtt_state);
}

void update_lcd_graphics() {
  draw_beer_logo(true);
  update_lcd_conn_graphics();
}

bool wifi_connect() {
  WiFi.begin(ssid, password);
  int connect_timeout = 50;
  while (WiFi.status() != WL_CONNECTED && connect_timeout-- > 0) {
      delay(100);
  }
  if (connect_timeout > 0) {
    return true;
  }
  return false;
}

bool wifi_connected() {
  return (WiFi.status() == WL_CONNECTED);
}

bool wifi_reconnect() {
    WiFi.disconnect();
    return WiFi.reconnect();
}

bool mqtt_connected() {
  return pubsub_client.connected();
}

bool mqtt_reconnect() {
  if (pubsub_client.connect(get_device_id().c_str(), mqtt_username, mqtt_password)) {
    return true;
  }
  return false;
}

bool mqtt_put_message(String topic, String payload) {
      pubsub_client.beginPublish(topic.c_str(), payload.length(), true);
      for (size_t pos = 0; pos <= payload.length(); pos+=64) {
          pubsub_client.print(payload.substring(pos,pos+64).c_str());
      }
      return pubsub_client.endPublish();
}

bool send_device_registration() {
  if (!mqtt_connected())
    return false;
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
      "name":"Beer scale",
      "manufacturer": "Brewrobot sensors",
      "model": "Beer scale weight sensor ESP32",
      "model_id": "A1",
      "serial_number": "00001",
      "hw_version": "1.00",
      "sw_version": "2025.1.1",
      "configuration_url": "https://github.com/melnijir/brewrobot-tap-weight"
    }
  })";
  registration_json.replace("XXX",get_device_id());
  return mqtt_put_message(String(mqtt_base_addr+get_device_id()+"/config"), registration_json);
}

bool send_device_state(float value) {
  if (!mqtt_connected())
    return false;
  String state_json = R"({
   "weight":XXX
  })";
  state_json.replace("XXX",String(value));
  return mqtt_put_message(String(mqtt_base_addr+get_device_id()+"/state"), state_json);
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
  update_lcd_graphics();
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

  write_lcd_first("Starting");
  write_lcd_second("...");

  // Setup WiFi connection
  wifi_state = lcd_logo_type::SYNC;
  update_lcd_conn_graphics();
  if (wifi_connect()) {
    wifi_state = lcd_logo_type::ONLINE;
  } else {
    wifi_state = lcd_logo_type::OFFLINE;
  }
  update_lcd_conn_graphics();

  // Setup MQTT connection
  pubsub_client.setServer(mqtt_broker, mqtt_port);
  if (wifi_connected()) {
    mqtt_state = lcd_logo_type::SYNC;
    update_lcd_conn_graphics();
    mqtt_reconnect();
    if (send_device_registration()) {
      mqtt_state = lcd_logo_type::ONLINE;
    } else {
      mqtt_state = lcd_logo_type::OFFLINE;
    }
  }
  update_lcd_conn_graphics();
}

void loop() {
  // WiFi connection checks
  // ----------------------
  if (!wifi_connected()) {
    wifi_state = lcd_logo_type::OFFLINE;
    unsigned long current_time = millis();
    if (current_time - wifi_last_conn >= reconnect_time) {
      wifi_state = lcd_logo_type::SYNC;
      update_lcd_conn_graphics();
      wifi_reconnect();
      wifi_last_conn = current_time;
    }
  } else {
    wifi_state = lcd_logo_type::ONLINE;
  }

  // MQTT connection checks
  // ----------------------
  if (!mqtt_connected()) {
    mqtt_state = lcd_logo_type::OFFLINE;
    unsigned long current_time = millis();
    if (current_time - mqtt_last_conn >= reconnect_time) {
      mqtt_state = lcd_logo_type::SYNC;
      update_lcd_conn_graphics();
      mqtt_reconnect();
      send_device_registration();
      mqtt_last_conn = current_time;
    }
  } else {
    mqtt_state = lcd_logo_type::ONLINE;
  }

  // Update LCD connections
  update_lcd_conn_graphics();

  // If tara is wanted, do tara
  if (tara_next_run) {
    update_lcd_text(false);
    write_lcd_first("Tara:");
    delay(lcd_head_dellay);
    scale.tare();
    write_lcd_second("done");
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
      update_lcd_text(true, rounded_weight);
      redraw_screen = false;
    }
    if (measurement_changed) {
      preferences.putFloat("weight", weight);
      measurement_changed = false;
      measurement_send = true;
    }
    unsigned long current_time = millis();
    if ((current_time - last_change_sync >= weight_change_sync) && measurement_send) {
      mqtt_state = lcd_logo_type::SYNC;
      update_lcd_conn_graphics();
      measurement_send = !send_device_state(rounded_weight);
      last_change_sync = current_time;
    }
    if (current_time - last_change_sync >= weight_regular_sync) {
      mqtt_state = lcd_logo_type::SYNC;
      update_lcd_conn_graphics();
      send_device_state(rounded_weight);
      last_change_sync = current_time;
    }
  } else {
    update_lcd_text(false);
    write_lcd_first("Scale");
    write_lcd_second("err :(");
    redraw_screen = true;
  }
  // Run MQTT rutines
  pubsub_client.loop();
  // Check if we should run OLED screensaver
  unsigned long current_time = millis();
  if (current_time - last_screensaver >= screensaver_timeout) {
    screensaver();
    last_screensaver = current_time;
  }
  // Sleep a while before the next run
  delay(200);
}
