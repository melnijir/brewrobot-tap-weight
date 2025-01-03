# brewrobot-tap-weight
Application for measuring tapped beers using ESP32, cheap scale and HX711 with integration to Home Assistant using MQTT. Application supports automatic integration to HA.

The buttons on the TTGO are used to reset the stored weight (in case of power loss) and to tare.

The scale has to be calibrated before the use! Calibration is not directly supported yet, but you can use any available [example](https://raw.githubusercontent.com/RuiSantosdotme/Random-Nerd-Tutorials/master/Projects/Arduino/Arduino_Load_Cell/Arduino_Calibrate_Load_Cell.ino).

![Display image](https://brewrobot.org/img/projects/brewrobot-tap-weight/display.png)
![Wiring image](https://brewrobot.org/img/projects/brewrobot-tap-weight/wiring.png)

## Required libs

All libs should be availible in Arduino IDE, you'll only need the [ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) support.

* [HX711](https://github.com/bogde/HX711) by Bogdan
* [PubSubClient](https://pubsubclient.knolleary.net) by Nick O'Leary
* [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) by Bodmer

## All you need

* TTGO ESP32  ![TTGO ESP32 image](https://brewrobot.org/img/projects/brewrobot-tap-weight/ttgo.png)
* Cheap scale ![Scale image](https://brewrobot.org/img/projects/brewrobot-tap-weight/scale.png)
* Beer        ![Beer keg image](https://brewrobot.org/img/projects/brewrobot-tap-weight/keg.png)
