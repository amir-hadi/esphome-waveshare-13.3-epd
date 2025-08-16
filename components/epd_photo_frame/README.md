# EPD Photo Frame ESPHome Component

This ESPHome component provides support for the Spectra6 e-paper display (13.3" 1200x1600) with image download capability.

## Features

- Downloads images from HTTP URLs
- Displays images on Spectra6 e-paper display
- Automatic display refresh at configurable intervals
- Power management and sleep modes
- SPI communication with dual CS pins (master/slave)

## Hardware Requirements

- ESP32 development board
- Spectra6 e-paper display (13.3" 1200x1600)
- SPI connections for display communication

## Pin Configuration

| Pin | Function | Description |
|-----|----------|-------------|
| 13  | SCK      | SPI Clock |
| 14  | MOSI     | SPI Data Out |
| 18  | CS_M     | Chip Select Master |
| 22  | CS_S     | Chip Select Slave |
| 26  | RST      | Reset |
| 27  | DC       | Data/Command |
| 25  | BUSY     | Busy signal |
| 33  | PWR      | Power control |

## Installation

1. Copy the `epd_photo_frame` folder to your ESPHome `custom_components` directory
2. Restart ESPHome
3. Add the component to your configuration

## Configuration

```yaml
# Example configuration
esphome:
  name: epd_photo_frame
  friendly_name: EPD Photo Frame

# WiFi configuration
wifi:
  ssid: "YourWiFiSSID"
  password: "YourWiFiPassword"

# Enable logging
logger:

# Enable Home Assistant API
api:

# Enable OTA updates
ota:

# SPI configuration
spi:
  clk_pin: GPIO13
  mosi_pin: GPIO14

# EPD Photo Frame component
display:
  - platform: epd_photo_frame
    id: epd_display
    reset_pin: GPIO26
    dc_pin: GPIO27
    busy_pin: GPIO25
    power_pin: GPIO33
    cs_master_pin: GPIO18
    cs_slave_pin: GPIO22
    image_url: "http://10.0.0.253:8080/image.bin"
    update_interval: 30min
```

## Configuration Options

| Option | Type | Required | Default | Description |
|--------|------|----------|---------|-------------|
| `reset_pin` | pin | Yes | - | Reset pin for the display |
| `dc_pin` | pin | Yes | - | Data/Command pin |
| `busy_pin` | pin | Yes | - | Busy signal pin |
| `power_pin` | pin | Yes | - | Power control pin |
| `cs_master_pin` | pin | Yes | - | Master chip select pin |
| `cs_slave_pin` | pin | Yes | - | Slave chip select pin |
| `image_url` | string | No | "http://10.0.0.253:8080/image.bin" | URL to download images from |
| `update_interval` | time | No | 30min | How often to update the display |

## Image Format

The component expects images in the following format:
- Resolution: 1200x1600 pixels
- Color depth: 4 bits per pixel (16 colors)
- Format: Raw binary data (.bin file)
- Size: 960,000 bytes (600 bytes per row × 1600 rows)

## Usage

The component will automatically:
1. Initialize the display on startup
2. Download the image from the specified URL
3. Display the image on the e-paper display
4. Refresh at the specified interval

## Troubleshooting

- Ensure all pins are correctly connected
- Check that the image URL is accessible
- Verify WiFi connectivity
- Check the logs for error messages

## Development

This component is based on the original PlatformIO firmware and has been adapted for ESPHome. The display driver maintains compatibility with the Spectra6 e-paper display specifications.

## License

MIT License - see LICENSE file for details.
