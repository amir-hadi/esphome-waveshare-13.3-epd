#include "epd_photo_frame.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/util.h"

namespace esphome {
namespace epd_photo_frame {

static const char *const TAG = "epd_photo_frame";

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
  ESP_LOGCONFIG(TAG, "  Reset Pin: %s", this->reset_pin_->get_pin().c_str());
  ESP_LOGCONFIG(TAG, "  DC Pin: %s", this->dc_pin_->get_pin().c_str());
  ESP_LOGCONFIG(TAG, "  Busy Pin: %s", this->busy_pin_->get_pin().c_str());
  ESP_LOGCONFIG(TAG, "  Power Pin: %s", this->power_pin_->get_pin().c_str());
  ESP_LOGCONFIG(TAG, "  CS Master Pin: %s", this->cs_master_pin_->get_pin().c_str());
  ESP_LOGCONFIG(TAG, "  CS Slave Pin: %s", this->cs_slave_pin_->get_pin().c_str());
  ESP_LOGCONFIG(TAG, "  Image URL: %s", this->image_url_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %d ms", this->update_interval_);
}

void EPDPhotoFrame::update() {
  if (!this->display_initialized_) {
    ESP_LOGW(TAG, "Display not initialized, skipping update");
    return;
  }
  
  ESP_LOGI(TAG, "Starting image update cycle");
  this->downloadAndDisplayImage();
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
  if (!wifi::global_wifi_component->is_connected()) {
    ESP_LOGW(TAG, "WiFi not connected, cannot download image");
    return false;
  }
  
  ESP_LOGI(TAG, "Starting image download...");
  
  // Use ESPHome's HTTP request component
  auto request = http_request::HttpRequest::create();
  request->set_url(this->image_url_);
  request->set_method("GET");
  
  // Set up response handling
  request->set_response_callback([this](http_request::HttpResponse *response) {
    if (response->get_status_code() == 200) {
      ESP_LOGI(TAG, "Image download successful, size: %d bytes", response->get_body().size());
      this->saveImageToFlash(response->get_body());
    } else {
      ESP_LOGE(TAG, "HTTP request failed with status: %d", response->get_status_code());
    }
  });
  
  request->set_error_callback([](const std::string &error) {
    ESP_LOGE(TAG, "HTTP request error: %s", error.c_str());
  });
  
  request->send();
  
  // For now, return true - in a real implementation you'd wait for the callback
  return true;
}

bool EPDPhotoFrame::saveImageToFlash(const std::string &image_data) {
  // This would save the image data to flash storage
  // For now, just log the size
  ESP_LOGI(TAG, "Would save %d bytes to flash", image_data.size());
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
  
  this->resetDisplay();
  this->powerOn();
  
  // Send initialization commands
  this->sendCommand(PSR);
  this->sendData(0x8F);
  
  this->sendCommand(PWR_EPD);
  this->sendData(0x03);
  
  this->sendCommand(PON);
  this->waitForBusy();
  
  this->sendCommand(PSR);
  this->sendData(0x8F);
  
  this->sendCommand(TRES);
  this->sendData(0x04); // 1200
  this->sendData(0xB0);
  this->sendData(0x06); // 1600
  this->sendData(0x40);
  
  this->sendCommand(CDI);
  this->sendData(0x97);
  
  this->sendCommand(TCON);
  this->sendData(0x05);
  
  this->sendCommand(EN_BUF);
  this->sendData(0x80);
  
  this->sendCommand(CCSET);
  this->sendData(0x00);
  
  this->sendCommand(PWS);
  this->sendData(0xAA);
  
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
  ESP_LOGI(TAG, "Turning on display...");
  
  this->sendCommand(DRF);
  this->waitForBusy();
  
  ESP_LOGI(TAG, "Display update complete");
}

void EPDPhotoFrame::waitForBusy() {
  ESP_LOGD(TAG, "Waiting for display to be ready...");
  while (this->busy_pin_->digital_read()) {
    delay(10);
  }
  ESP_LOGD(TAG, "Display ready");
}

void EPDPhotoFrame::resetDisplay() {
  ESP_LOGD(TAG, "Resetting display...");
  this->reset_pin_->digital_write(false);
  delay(200);
  this->reset_pin_->digital_write(true);
  delay(200);
}

void EPDPhotoFrame::powerOn() {
  ESP_LOGD(TAG, "Powering on display...");
  this->power_pin_->digital_write(true);
  delay(1000);
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



}  // namespace epd_photo_frame
}  // namespace esphome
