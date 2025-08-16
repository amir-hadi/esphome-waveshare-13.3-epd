# Installation Guide for EPD Photo Frame ESPHome Component

## Prerequisites

- ESPHome installed and configured
- ESP32 development board
- Spectra6 e-paper display (13.3" 1200x1600)
- Basic knowledge of ESPHome configuration

## Installation Steps

### 1. Copy Component Files

Copy the entire `epd_photo_frame` folder to your ESPHome `custom_components` directory:

```bash
# If using ESPHome CLI
cp -r epd_photo_frame ~/.esphome/custom_components/

# If using ESPHome Docker
cp -r epd_photo_frame /path/to/esphome/custom_components/
```

### 2. Restart ESPHome

Restart your ESPHome instance to load the new component.

### 3. Add to Configuration

Add the component to your ESPHome configuration file:

```yaml
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
    image_url: "http://your-server.com/image.bin"
    update_interval: 30min
```

### 4. Configure Pins

Ensure your pins match the hardware connections:

| Pin | Function | ESP32 Pin |
|-----|----------|-----------|
| SCK | SPI Clock | GPIO13 |
| MOSI | SPI Data Out | GPIO14 |
| CS_M | Chip Select Master | GPIO18 |
| CS_S | Chip Select Slave | GPIO22 |
| RST | Reset | GPIO26 |
| DC | Data/Command | GPIO27 |
| BUSY | Busy signal | GPIO25 |
| PWR | Power control | GPIO33 |

### 5. Set Image URL

Update the `image_url` to point to your image server:

```yaml
image_url: "http://your-server.com/image.bin"
```

### 6. Compile and Upload

Compile and upload your configuration:

```bash
esphome compile your_config.yaml
esphome upload your_config.yaml
```

## Troubleshooting

### Component Not Found

- Ensure the component is in the correct `custom_components` directory
- Restart ESPHome after copying the component
- Check that all required files are present

### Compilation Errors

- Verify all pin assignments are correct
- Ensure SPI configuration is properly set
- Check that the ESP32 board is supported

### Display Not Working

- Verify all hardware connections
- Check power supply to the display
- Monitor logs for error messages
- Ensure the image URL is accessible

### Image Not Displaying

- Check WiFi connectivity
- Verify the image URL is correct
- Ensure the image format matches requirements (1200x1600, 4bpp)
- Check that the image file is accessible

## Next Steps

Once the basic component is working:

1. **Customize Update Intervals**: Adjust the `update_interval` based on your needs
2. **Add Automation**: Create automations for scheduled updates
3. **Integrate with Home Assistant**: Use the display entity in your HA dashboard
4. **Add Manual Controls**: Create buttons for manual refresh

## Support

For issues and questions:
- Check the logs for error messages
- Verify hardware connections
- Ensure all dependencies are met
- Refer to the README.md for detailed documentation
