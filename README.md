# DeskBot

An interactive desktop companion built on the ESP32-C3 Mini. DeskBot uses a time-of-flight (ToF) sensor to detect nearby people, shows animated eyes on an OLED display, and uses a touch sensor to switch between eyes, time and weather screens.

## Features

- **Animated eyes** with six moods: happy, serious, sad, curious, sleepy and excited
- **Presence detection** using a VL53L0X ToF sensor
- **Automatic wake and sleep:** wakes when someone is within 1 m, sleeps after 5 seconds with no one detected
- **Touch interaction** with a TTP223 sensor to cycle display modes
- **Time display** via NTP, set to Indian Standard Time (IST, UTC+05:30)
- **Weather display** from the OpenWeatherMap API, with periodic refresh and automatic retry on failure

## Hardware

| Component              | Purpose                         |
| ---------------------- | ------------------------------- |
| ESP32-C3 Mini          | Main microcontroller            |
| SSD1306 OLED display   | Shows eyes, time and weather    |
| VL53L0X ToF sensor     | Detects nearby people           |
| TTP223 touch sensor    | User input                      |
| Jumper wires           | Connections                     |

## Wiring

The OLED and the VL53L0X share the same I2C bus.

| Component | Pin | ESP32-C3 Mini |
| --------- | --- | ------------- |
| OLED      | SDA | GPIO 8        |
| OLED      | SCL | GPIO 9        |
| OLED      | VCC | 3.3V          |
| OLED      | GND | GND           |
| VL53L0X   | SDA | GPIO 8        |
| VL53L0X   | SCL | GPIO 9        |
| VL53L0X   | VIN | 3.3V          |
| VL53L0X   | GND | GND           |
| TTP223    | OUT | GPIO 10       |
| TTP223    | VCC | 3.3V          |
| TTP223    | GND | GND           |

**I2C addresses:** SSD1306 `0x3C`, VL53L0X `0x29`

## Software Setup

Built with the Arduino framework.

### Libraries

Install via the Arduino IDE Library Manager:

- Adafruit GFX Library
- Adafruit SSD1306
- Adafruit VL53L0X
- ArduinoJson (v6)

`WiFi`, `HTTPClient`, `WiFiClientSecure`, `Wire` and `time` come with the ESP32 Arduino core.

### Configuration

1. Get an API key from OpenWeatherMap (https://openweathermap.org/api).
2. Open `DeskBot.ino` and set your credentials:

   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   const char* OWM_API_KEY = "YOUR_OWM_API_KEY";
   ```

### Upload

1. In the Arduino IDE, select your ESP32-C3 board and COM port.
2. Connect the board over USB and upload the sketch.
3. Open the Serial Monitor at **115200 baud** to see startup messages, sensor status, Wi-Fi details and weather API diagnostics.

## Usage

### Presence detection

The bot wakes when an object is within 1000 mm and goes to sleep after 5 seconds with no presence detected.

### Touch controls

| Touch                   | Display       |
| ----------------------- | ------------- |
| First touch while awake | Time          |
| Second touch            | Weather       |
| Third touch             | Animated eyes |

Touching the sensor while the bot is asleep wakes it without changing the display mode.

### Screens

- **Eyes:** animated eyes with blinking and movement; the mood changes periodically.
- **Time:** current time from an NTP server, in IST.
- **Weather:** location, temperature, condition and a matching icon. Data refreshes every 10 minutes, and failed requests retry every 15 seconds.

## Adjustable Parameters

| Parameter                | Default    |
| ------------------------ | ---------- |
| ToF detection distance   | 1000 mm    |
| Sleep timeout            | 5000 ms    |
| ToF sampling interval    | 100 ms     |
| Weather refresh interval | 10 minutes |
| Weather retry interval   | 15 seconds |
| Timezone                 | IST (UTC+05:30) |

## Notes

- A Wi-Fi connection is required for time sync and weather updates.
- The ToF sensor must initialize for automatic presence detection. Touch wake-up still works without it.
- The HTTPS requests currently use `setInsecure()`, which skips certificate verification. Configure proper certificate checking before any production use.
