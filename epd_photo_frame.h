#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace epd_photo_frame {

class EPDPhotoFrame : public PollingComponent, public display::DisplayBuffer, public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ> {
 public:
  void set_reset_pin(GPIOPin *reset_pin) { reset_pin_ = reset_pin; }
  void set_dc_pin(GPIOPin *dc_pin) { dc_pin_ = dc_pin; }
  void set_busy_pin(GPIOPin *busy_pin) { busy_pin_ = busy_pin; }
  void set_power_pin(GPIOPin *power_pin) { power_pin_ = power_pin; }
  void set_cs_master_pin(GPIOPin *cs_master_pin) { cs_master_pin_ = cs_master_pin; }
  void set_cs_slave_pin(GPIOPin *cs_slave_pin) { cs_slave_pin_ = cs_slave_pin; }
  void set_image_url(const std::string &image_url) { image_url_ = image_url; }
  void set_update_interval(uint32_t update_interval) { update_interval_ = update_interval; }

  void setup() override;
  void dump_config() override;
  void update() override;
  void loop() override;

  // Display buffer interface
  void fill(Color color) override;
  void fill_rect(int x1, int y1, int width, int height, Color color) override;
  void draw_pixel_at(int x, int y, Color color) override;
  int get_width_internal() override { return 1200; }
  int get_height_internal() override { return 1600; }

  // Custom methods
  void downloadAndDisplayImage();
  void displayImageFromFlash();
  void clearDisplay();
  void sleepDisplay();
  void wakeDisplay();
  
  // Service calls
  void refresh();
  void setImageUrl(const std::string &url);
  std::string getImageUrl() const { return image_url_; }

 protected:
  // EPD commands
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void initDisplay();
  void turnOnDisplay();
  
  // Image handling
  bool downloadImage();
  bool saveImageToFlash();
  bool loadImageFromFlash();
  
  // Helper methods
  void waitForBusy();
  void resetDisplay();
  void powerOn();
  void powerOff();

  GPIOPin *reset_pin_{nullptr};
  GPIOPin *dc_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  GPIOPin *power_pin_{nullptr};
  GPIOPin *cs_master_pin_{nullptr};
  GPIOPin *cs_slave_pin_{nullptr};
  
  std::string image_url_;
  uint32_t update_interval_{1800000}; // 30 minutes default
  
  bool display_initialized_{false};
  bool image_downloaded_{false};
  bool display_updated_{false};
  
  static const int SCREEN_WIDTH = 1200;
  static const int SCREEN_HEIGHT = 1600;
  static const int BYTES_PER_ROW = 600; // 4bpp
  
  // EPD command constants
  static const uint8_t PSR = 0x00;
  static const uint8_t PWR_EPD = 0x01;
  static const uint8_t POF = 0x02;
  static const uint8_t PON = 0x04;
  static const uint8_t BTST_N = 0x05;
  static const uint8_t BTST_P = 0x06;
  static const uint8_t DTM = 0x10;
  static const uint8_t DRF = 0x12;
  static const uint8_t CDI = 0x50;
  static const uint8_t TCON = 0x60;
  static const uint8_t TRES = 0x61;
  static const uint8_t AN_TM = 0x74;
  static const uint8_t AGID = 0x86;
  static const uint8_t BUCK_BOOST_VDDN = 0xB0;
  static const uint8_t TFT_VCOM_POWER = 0xB1;
  static const uint8_t EN_BUF = 0xB6;
  static const uint8_t BOOST_VDDP_EN = 0xB7;
  static const uint8_t CCSET = 0xE0;
  static const uint8_t PWS = 0xE3;
  static const uint8_t CMD66 = 0xF0;
};

}  // namespace epd_photo_frame
}  // namespace esphome
