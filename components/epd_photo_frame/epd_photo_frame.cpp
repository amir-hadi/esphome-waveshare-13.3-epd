#include "epd_photo_frame.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <string>
#include "esphome/core/application.h"
#include "esphome/core/util.h"

namespace esphome {
namespace epd_photo_frame {

static const char *const TAG = "epd_photo_frame";

static inline void cs_all(esphome::GPIOPin *cs_m, esphome::GPIOPin *cs_s, bool level_high) {
  cs_m->digital_write(level_high);
  cs_s->digital_write(level_high);
}

void EPDPhotoFrame::setup() {
  ESP_LOGCONFIG(TAG, "Setting up EPD Photo Frame...");
  
  // Initialize pins
  this->reset_pin_->setup();
  this->dc_pin_->setup();
  this->busy_pin_->setup();
  this->power_pin_->setup();
  this->cs_master_pin_->setup();
  this->cs_slave_pin_->setup();
  
  // Set initial pin states
  this->reset_pin_->digital_write(false);
  this->dc_pin_->digital_write(false);
  this->cs_master_pin_->digital_write(true);
  this->cs_slave_pin_->digital_write(true);
  this->power_pin_->digital_write(false);
  
  // Initialize SPI
  this->spi_setup();
  
  // Initialize display
  this->initDisplay();
  
  ESP_LOGCONFIG(TAG, "EPD Photo Frame setup complete");
}

void EPDPhotoFrame::dump_config() {
  ESP_LOGCONFIG(TAG, "EPD Photo Frame:");
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_PIN("  Power Pin: ", this->power_pin_);
  LOG_PIN("  CS Master Pin: ", this->cs_master_pin_);
  LOG_PIN("  CS Slave Pin: ", this->cs_slave_pin_);
  ESP_LOGCONFIG(TAG, "  Image URL: %s", this->image_url_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %d ms", this->update_interval_);
}

void EPDPhotoFrame::update() {
  // DisplayBuffer is not a PollingComponent; left empty for now
}

void EPDPhotoFrame::loop() {
  // Handle any ongoing operations
}

void EPDPhotoFrame::downloadAndDisplayImage() {
  ESP_LOGI(TAG, "Downloading and displaying image from: %s", this->image_url_.c_str());
  
  if (this->downloadImage()) {
    ESP_LOGI(TAG, "Image downloaded successfully");
    if (this->displayImageFromFlash()) {
      ESP_LOGI(TAG, "Image displayed successfully");
      this->display_updated_ = true;
    } else {
      ESP_LOGE(TAG, "Failed to display image");
    }
  } else {
    ESP_LOGE(TAG, "Failed to download image");
  }
}

bool EPDPhotoFrame::downloadImage() {
  // Stubbed for compilation: in a full implementation, perform HTTP GET and save
  // the data to flash, then set image_downloaded_.
  ESP_LOGI(TAG, "Simulating image download from %s", this->image_url_.c_str());
  this->image_downloaded_ = true;
  return true;
}

bool EPDPhotoFrame::saveImageToFlash() {
  // Stubbed: Implement flash write here
  this->image_downloaded_ = true;
  return true;
}

bool EPDPhotoFrame::displayImageFromFlash() {
  if (!this->image_downloaded_) {
    ESP_LOGW(TAG, "No image downloaded, cannot display");
    return false;
  }
  
  ESP_LOGI(TAG, "Displaying image from flash...");
  
  // Load image data from flash (simplified for now)
  if (!this->loadImageFromFlash()) {
    ESP_LOGE(TAG, "Failed to load image from flash");
    return false;
  }
  
  // Send image data to display
  this->sendImageData();
  
  // Turn on display
  this->turnOnDisplay();
  
  ESP_LOGI(TAG, "Image display complete");
  return true;
}

bool EPDPhotoFrame::loadImageFromFlash() {
  // This would load the image data from flash
  // For now, just return true
  return true;
}

void EPDPhotoFrame::sendImageData() {
  ESP_LOGI(TAG, "Sending image data to display...");
  
  // Master half
  this->cs_master_pin_->digital_write(false);
  this->sendCommand(DTM);
  this->dc_pin_->digital_write(true);
  
  // Send image data row by row (simplified)
  for (int line = 0; line < SCREEN_HEIGHT; line++) {
    // In a real implementation, you'd read from flash and send the data
    // For now, just send dummy data
    for (int k = 0; k < BYTES_PER_ROW / 2; k++) {
      this->sendData(0xFF); // White pixels
    }
  }
  
  this->cs_master_pin_->digital_write(true);
  delay(50);
  
  // Slave half
  this->cs_slave_pin_->digital_write(false);
  this->sendCommand(DTM);
  this->dc_pin_->digital_write(true);
  
  for (int line = 0; line < SCREEN_HEIGHT; line++) {
    for (int k = 0; k < BYTES_PER_ROW / 2; k++) {
      this->sendData(0xFF); // White pixels
    }
  }
  
  this->cs_slave_pin_->digital_write(true);
}

void EPDPhotoFrame::initDisplay() {
  ESP_LOGI(TAG, "Initializing EPD display...");

  // Reset sequence (mirror original firmware)
  this->reset_pin_->digital_write(true);
  delay(30);
  this->reset_pin_->digital_write(false);
  delay(30);
  this->reset_pin_->digital_write(true);
  delay(30);
  this->reset_pin_->digital_write(false);
  delay(30);
  this->reset_pin_->digital_write(true);
  delay(30);

  // Ensure power enabled as in original
  this->power_pin_->digital_write(true);

  // Data tables from original firmware
  const uint8_t AN_TM_V[] = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
  const uint8_t CMD66_V[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
  const uint8_t PSR_V[] = {0xDF, 0x69};
  const uint8_t CDI_VV[] = {0xF7};
  const uint8_t TCON_VV[] = {0x03, 0x03};
  const uint8_t AGID_VV[] = {0x10};
  const uint8_t PWS_VV[] = {0x22};
  const uint8_t CCSET_VV[] = {0x01};
  const uint8_t TRES_VV[] = {0x04, 0xB0, 0x03, 0x20};
  const uint8_t PWR_VV[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
  const uint8_t EN_BUF_VV[] = {0x07};
  const uint8_t BTST_P_VV[] = {0xE8, 0x28};
  const uint8_t BOOST_VDDP_EN_VV[] = {0x01};
  const uint8_t BTST_N_VV[] = {0xE8, 0x28};
  const uint8_t BUCK_BOOST_VDDN_VV[] = {0x01};
  const uint8_t TFT_VCOM_POWER_VV[] = {0x02};

  // Helper to send command + buffer under both CS or specific
  auto send_buf = [this](uint8_t cmd, const uint8_t *buf, size_t len) {
    this->sendCommand(cmd);
    for (size_t i = 0; i < len; i++) this->sendData(buf[i]);
  };

  // Sequence
  this->cs_master_pin_->digital_write(false);
  send_buf(AN_TM, AN_TM_V, sizeof(AN_TM_V));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(CMD66, CMD66_V, sizeof(CMD66_V));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(PSR, PSR_V, sizeof(PSR_V));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(CDI, CDI_VV, sizeof(CDI_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(TCON, TCON_VV, sizeof(TCON_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(AGID, AGID_VV, sizeof(AGID_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(PWS, PWS_VV, sizeof(PWS_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(CCSET, CCSET_VV, sizeof(CCSET_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  send_buf(TRES, TRES_VV, sizeof(TRES_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  this->cs_master_pin_->digital_write(false);
  send_buf(PWR_EPD, PWR_VV, sizeof(PWR_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  this->cs_master_pin_->digital_write(false);
  send_buf(EN_BUF, EN_BUF_VV, sizeof(EN_BUF_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  this->cs_master_pin_->digital_write(false);
  send_buf(BTST_P, BTST_P_VV, sizeof(BTST_P_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  this->cs_master_pin_->digital_write(false);
  send_buf(BOOST_VDDP_EN, BOOST_VDDP_EN_VV, sizeof(BOOST_VDDP_EN_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  this->cs_master_pin_->digital_write(false);
  send_buf(BTST_N, BTST_N_VV, sizeof(BTST_N_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  this->cs_master_pin_->digital_write(false);
  send_buf(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_VV, sizeof(BUCK_BOOST_VDDN_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  this->cs_master_pin_->digital_write(false);
  send_buf(TFT_VCOM_POWER, TFT_VCOM_POWER_VV, sizeof(TFT_VCOM_POWER_VV));
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);
  
  this->display_initialized_ = true;
  ESP_LOGI(TAG, "EPD display initialized");
}

void EPDPhotoFrame::sendCommand(uint8_t command) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(command);
  this->disable();
}

void EPDPhotoFrame::sendData(uint8_t data) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(data);
  this->disable();
}

void EPDPhotoFrame::turnOnDisplay() {
  ESP_LOGI(TAG, "Turning on display (PON -> DRF -> POF)...");

  // POWER_ON
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  this->sendCommand(PON);
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);
  this->waitForBusy();

  // Display Refresh with 0x00 parameter
  delay(50);
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  this->sendCommand(DRF);
  this->sendData(0x00);
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);
  this->waitForBusy();

  // POWER_OFF with 0x00 parameter
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  this->sendCommand(POF);
  this->sendData(0x00);
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);

  ESP_LOGI(TAG, "Display update complete");
}

void EPDPhotoFrame::waitForBusy() {
  ESP_LOGD(TAG, "Waiting for display to be ready...");
  const uint32_t start_ms = millis();
  // Original firmware: LOW = busy, HIGH = idle
  while (!this->busy_pin_->digital_read()) {
    App.feed_wdt();
    delay(10);
    if (millis() - start_ms > 15000) {  // 15s timeout to avoid WDT reset
      ESP_LOGW(TAG, "Busy wait timed out after 15000 ms; proceeding");
      break;
    }
  }
  delay(20);
  ESP_LOGD(TAG, "Display ready (busy=%d)", this->busy_pin_->digital_read());
}

void EPDPhotoFrame::resetDisplay() {
  ESP_LOGD(TAG, "Resetting display...");
  this->reset_pin_->digital_write(false);
  delay(10);
  this->reset_pin_->digital_write(true);
  delay(10);
}

void EPDPhotoFrame::powerOn() {
  ESP_LOGD(TAG, "Powering on display...");
  this->power_pin_->digital_write(true);
  delay(100);
}

void EPDPhotoFrame::powerOff() {
  ESP_LOGD(TAG, "Powering off display...");
  this->power_pin_->digital_write(false);
}

void EPDPhotoFrame::clearDisplay() {
  ESP_LOGI(TAG, "Clearing display...");
  
  this->sendCommand(DTM);
  this->dc_pin_->digital_write(true);
  
  // Send white data to clear
  for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT / 2; i++) {
    this->sendData(0xFF);
  }
  
  this->turnOnDisplay();
}

void EPDPhotoFrame::sleepDisplay() {
  ESP_LOGI(TAG, "Putting display to sleep...");
  this->sendCommand(POF);
  this->waitForBusy();
  this->powerOff();
}

void EPDPhotoFrame::wakeDisplay() {
  this->powerOn();
  this->initDisplay();
}

void EPDPhotoFrame::refresh() {
  ESP_LOGI(TAG, "Manual refresh requested");
  this->downloadAndDisplayImage();
}

void EPDPhotoFrame::setImageUrl(const std::string &url) {
  ESP_LOGI(TAG, "Setting new image URL: %s", url.c_str());
  this->image_url_ = url;
  // Optionally trigger a refresh with the new URL
  this->downloadAndDisplayImage();
}

void EPDPhotoFrame::draw_absolute_pixel_internal(int x, int y, Color color) {
  // Minimal implementation to satisfy abstract base; not used by our raw transfer
}

display::DisplayType EPDPhotoFrame::get_display_type() {
  return display::DISPLAY_TYPE_BINARY; // placeholder
}



}  // namespace epd_photo_frame
}  // namespace esphome
