#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <stdio.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
// Networking and HTTP client functionality is intentionally not used directly here
// to keep the component self-contained for compilation. Image download can be
// implemented via automations or future integrations.

namespace esphome {
namespace epd_photo_frame {

class EPDPhotoFrame : public display::DisplayBuffer, public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ>, public web_server_idf::AsyncWebHandler {
 public:
  void set_reset_pin(GPIOPin *reset_pin) { reset_pin_ = reset_pin; }
  void set_dc_pin(GPIOPin *dc_pin) { dc_pin_ = dc_pin; }
  void set_busy_pin(GPIOPin *busy_pin) { busy_pin_ = busy_pin; }
  void set_power_pin(GPIOPin *power_pin) { power_pin_ = power_pin; }
  void set_cs_master_pin(GPIOPin *cs_master_pin) { cs_master_pin_ = cs_master_pin; }
  void set_cs_slave_pin(GPIOPin *cs_slave_pin) { cs_slave_pin_ = cs_slave_pin; }
  void set_image_url(const std::string &image_url) { image_url_ = image_url; }
  void set_update_interval(uint32_t update_interval) { update_interval_ = update_interval; }
  void assign_spi_parent(spi::SPIComponent *parent) { this->set_spi_parent(parent); }

  // Optional HA entities for reporting download results
  void set_download_bytes_sensor(sensor::Sensor *s) { download_bytes_sensor_ = s; }
  void set_download_success_binary(binary_sensor::BinarySensor *b) { download_success_binary_ = b; }
  void set_download_status_text(text_sensor::TextSensor *t) { download_status_text_ = t; }

  void setup() override;
  void dump_config() override;
  void update() override;
  void loop() override;

  // Display buffer interface
  int get_width_internal() override { return 1200; }
  int get_height_internal() override { return 1600; }
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  display::DisplayType get_display_type() override;
  // Web upload handler
  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override;
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override;
  void handleBody(web_server_idf::AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  void handleUpload(web_server_idf::AsyncWebServerRequest *request, const std::string &filename, size_t index, uint8_t *data, size_t len, bool final) override;
  bool isRequestHandlerTrivial() const override { return false; }

  // Custom methods
  void downloadAndDisplayImage();
  bool displayImageFromFlash();
  void clearDisplay();
  void sleepDisplay();
  void wakeDisplay();
  void startDownload();
  void displayFromFile();
  
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
  void sendImageData();
  
  // Image handling
  bool downloadImage();
  bool saveImageToFlash();
  bool loadImageFromFlash();
  bool downloadFileToSpiffs(const std::string &url, const char *path);
  bool mountSpiffs();

  // Background download task
  static void download_task_trampoline(void *param);
  void run_download_task();
  bool sendImageDataFromFile(const char *path);
  
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
  std::vector<uint8_t> upload_buffer_;
  bool upload_complete_{false};
  size_t upload_expected_total_{0};
  // Streaming upload state
  bool streaming_upload_{false};
  int current_row_{0};
  int row_fill_{0};
  uint8_t row_buffer_[600]{}; // BYTES_PER_ROW
  bool master_dtm_sent_{false};
  bool slave_dtm_sent_{false};
  FILE *upload_fp_{nullptr};
  bool spiffs_mounted_{false};
  size_t upload_bytes_written_{0};
  
  bool display_initialized_{false};
  bool image_downloaded_{false};
  bool display_updated_{false};
  
  // Optional entities
  sensor::Sensor *download_bytes_sensor_{nullptr};
  binary_sensor::BinarySensor *download_success_binary_{nullptr};
  text_sensor::TextSensor *download_status_text_{nullptr};
  
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
