#include "epd_photo_frame.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <string>
#include "esphome/core/application.h"
#include "esphome/core/util.h"
#include <esp_http_client.h>
#include <esp_spiffs.h>

namespace esphome {
namespace epd_photo_frame {

static const char *const TAG = "epd_photo_frame";

static inline void cs_all(esphome::GPIOPin *cs_m, esphome::GPIOPin *cs_s, bool level_high) {
  cs_m->digital_write(level_high);
  cs_s->digital_write(level_high);
}

static bool read_exact(FILE *fp, uint8_t *buf, size_t len) {
  size_t off = 0; while (off < len) { size_t r = fread(buf + off, 1, len - off, fp); if (r == 0) return false; off += r; }
  return true;
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
  // Match GPIO_Config: power on by default
  this->power_pin_->digital_write(true);
  
  // Initialize SPI
  this->spi_setup();
  
  // Initialize display
  this->initDisplay();
  
  // SPIFFS will be mounted on first use
  spiffs_mounted_ = false;

  // No web upload handlers anymore; downloads are performed from backend via HTTP client

  ESP_LOGCONFIG(TAG, "EPD Photo Frame setup complete");
}
bool EPDPhotoFrame::mountSpiffs() {
  if (spiffs_mounted_) return true;
  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = nullptr,
      .max_files = 4,
      .format_if_mount_failed = true,
  };
  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPIFFS mount failed: %d", (int) ret);
    return false;
  }
  size_t total = 0, used = 0;
  if (esp_spiffs_info(nullptr, &total, &used) == ESP_OK) {
    ESP_LOGI(TAG, "SPIFFS total=%u used=%u", (unsigned) total, (unsigned) used);
  }
  spiffs_mounted_ = true;
  return true;
}

void EPDPhotoFrame::startDownload() {
  ESP_LOGI(TAG, "Start download (background) from %s", image_url_.c_str());
  xTaskCreatePinnedToCore(&EPDPhotoFrame::download_task_trampoline, "epd_dl", 4096, this, 5, nullptr, 0);
}

void EPDPhotoFrame::displayFromFile() {
  ESP_LOGI(TAG, "Display from /spiffs/image.bin");
  if (this->sendImageDataFromFile("/spiffs/image.bin")) {
    this->turnOnDisplay();
  } else {
    ESP_LOGE(TAG, "Display from file failed");
  }
}

void EPDPhotoFrame::download_task_trampoline(void *param) {
  auto *self = reinterpret_cast<EPDPhotoFrame *>(param);
  self->run_download_task();
  vTaskDelete(nullptr);
}

void EPDPhotoFrame::run_download_task() {
  if (!this->mountSpiffs()) return;
  const char *path = "/spiffs/image.bin";
  ESP_LOGI(TAG, "DL task: %s -> %s", this->image_url_.c_str(), path);
  // Ensure previous file is removed and enough space exists
  unlink(path);
  size_t total_bytes = 0, used_bytes = 0;
  if (esp_spiffs_info(nullptr, &total_bytes, &used_bytes) == ESP_OK) {
    int expected = (SCREEN_WIDTH * SCREEN_HEIGHT) / 2; // 960000
    size_t free_bytes = (total_bytes > used_bytes) ? (total_bytes - used_bytes) : 0;
    if (free_bytes < (size_t) expected + 4096) {
      ESP_LOGW(TAG, "Not enough SPIFFS space: total=%u used=%u free=%u need~%u", (unsigned) total_bytes, (unsigned) used_bytes, (unsigned) free_bytes, (unsigned) (expected + 4096));
      ESP_LOGW(TAG, "Formatting SPIFFS to reclaim space (task)...");
      esp_vfs_spiffs_unregister(nullptr);
      if (esp_spiffs_format(nullptr) == ESP_OK) {
        spiffs_mounted_ = false;
        if (!this->mountSpiffs()) {
          ESP_LOGE(TAG, "SPIFFS re-mount failed after format (task)");
          if (download_success_binary_) download_success_binary_->publish_state(false);
          if (download_status_text_) download_status_text_->publish_state("spiffs_remount_failed");
          return;
        }
      } else {
        ESP_LOGE(TAG, "SPIFFS format failed (task)");
        if (download_success_binary_) download_success_binary_->publish_state(false);
        if (download_status_text_) download_status_text_->publish_state("spiffs_format_failed");
        return;
      }
    }
  }
  // Ranged download in 100KB chunks with up to 3 retries per chunk
  const int expected = (SCREEN_WIDTH * SCREEN_HEIGHT) / 2; // 960000
  const int chunk_size = 100 * 1024;
  int downloaded_total = 0;
  // Create/truncate file
  FILE *fp = fopen(path, "wb");
  if (fp) fclose(fp);
  fp = fopen(path, "rb+");
  if (!fp) {
    ESP_LOGE(TAG, "open %s failed", path);
    if (download_success_binary_) download_success_binary_->publish_state(false);
    if (download_status_text_) download_status_text_->publish_state("file_open_failed");
    return;
  }
  for (int start = 0; start < expected; start += chunk_size) {
    int end = start + chunk_size - 1;
    if (end >= expected) end = expected - 1;
    const int need = end - start + 1;
    int attempt = 0;
    bool chunk_ok = false;
    while (attempt < 3 && !chunk_ok) {
      attempt++;
      esp_http_client_config_t cfg = {};
      cfg.url = this->image_url_.c_str();
      cfg.timeout_ms = 5000;
      cfg.method = HTTP_METHOD_GET;
      cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;
      esp_http_client_handle_t client = esp_http_client_init(&cfg);
      if (!client) { ESP_LOGW(TAG, "client init failed (chunk %d)", start); break; }
      char range[64];
      snprintf(range, sizeof(range), "bytes=%d-%d", start, end);
      esp_http_client_set_header(client, "Range", range);
      if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGW(TAG, "open failed (chunk %d try %d)", start, attempt);
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }
      esp_http_client_fetch_headers(client);
      // Position file and stream
      fseek(fp, start, SEEK_SET);
      int got = 0;
      uint8_t buf[1024];
      while (got < need) {
        int to_read = need - got;
        if (to_read > (int) sizeof(buf)) to_read = sizeof(buf);
        int r = esp_http_client_read(client, (char *) buf, to_read);
        if (r < 0) { ESP_LOGW(TAG, "read err %d (chunk %d)", r, start); break; }
        if (r == 0) break;
        fwrite(buf, 1, r, fp);
        got += r;
        // Yield so other tasks run
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      if (got == need) {
        chunk_ok = true;
        downloaded_total += got;
      } else {
        ESP_LOGW(TAG, "chunk %d-%d incomplete got=%d need=%d (attempt %d)", start, end, got, need, attempt);
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
    if (!chunk_ok) {
      fclose(fp);
      if (download_bytes_sensor_) download_bytes_sensor_->publish_state(downloaded_total);
      if (download_success_binary_) download_success_binary_->publish_state(false);
      if (download_status_text_) download_status_text_->publish_state("chunk_failed");
      return;
    }
  }
  fclose(fp);
  ESP_LOGI(TAG, "DL task done (ranged): %d bytes", downloaded_total);
  if (download_bytes_sensor_) download_bytes_sensor_->publish_state(downloaded_total);
  if (downloaded_total == expected) {
    if (download_success_binary_) download_success_binary_->publish_state(true);
    if (download_status_text_) download_status_text_->publish_state("ok");
  } else {
    if (download_success_binary_) download_success_binary_->publish_state(false);
    if (download_status_text_) download_status_text_->publish_state("incomplete");
  }
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


void EPDPhotoFrame::initDisplay() {
  ESP_LOGI(TAG, "Initializing EPD display...");

  // Reset sequence (mirror original firmware more closely)
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

  // Display Refresh
  delay(50);
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  this->sendCommand(DRF);
  // Match reference: DRF takes a single 0x00 parameter
  this->sendData(0x00);
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);
  this->waitForBusy();

  // POWER_OFF
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, false);
  this->sendCommand(POF);
  // Match reference: POF takes a single 0x00 parameter
  this->sendData(0x00);
  cs_all(this->cs_master_pin_, this->cs_slave_pin_, true);
  // Reference waits for busy release after POF as well
  this->waitForBusy();

  ESP_LOGI(TAG, "Display update complete");
}

void EPDPhotoFrame::waitForBusy() {
  ESP_LOGD(TAG, "Waiting for display to be ready...");
  const uint32_t start_ms = millis();
  // Original firmware: LOW = busy, HIGH = idle
  while (!this->busy_pin_->digital_read()) {
    App.feed_wdt();
    delay(10);
    if (millis() - start_ms > 60000) {  // 60s timeout to avoid WDT reset
      ESP_LOGW(TAG, "Busy wait timed out after 60000 ms; proceeding");
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

// clearDisplay removed; not used in current flow

void EPDPhotoFrame::sleepDisplay() {
  ESP_LOGI(TAG, "Putting display to sleep...");
  this->sendCommand(POF);
  this->waitForBusy();
  this->powerOff();
}

void EPDPhotoFrame::setImageUrl(const std::string &url) {
  ESP_LOGI(TAG, "Setting new image URL: %s", url.c_str());
  this->image_url_ = url;
}

void EPDPhotoFrame::draw_absolute_pixel_internal(int x, int y, Color color) {
  // Minimal implementation to satisfy abstract base; not used by our raw transfer
}

display::DisplayType EPDPhotoFrame::get_display_type() {
  return display::DISPLAY_TYPE_BINARY; // placeholder
}



bool esphome::epd_photo_frame::EPDPhotoFrame::sendImageDataFromFile(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    ESP_LOGE("epd_photo_frame", "Failed to open %s", path);
    return false;
  }
  ESP_LOGI("epd_photo_frame", "Sending image from %s", path);

  // Master half
  ESP_LOGI(TAG, "Master half start");
  this->cs_master_pin_->digital_write(false);
  this->sendCommand(DTM);
  this->dc_pin_->digital_write(true);
  this->enable();
  uint8_t row_buf[BYTES_PER_ROW / 2];
  for (int line = 0; line < SCREEN_HEIGHT; line++) {
    // Read master half of this row
    size_t r = fread(row_buf, 1, sizeof(row_buf), fp);
    if (r != sizeof(row_buf)) {
      fclose(fp);
      this->disable();
      this->cs_master_pin_->digital_write(true);
      return false;
    }
    this->write_array(row_buf, sizeof(row_buf));
    // Discard slave half of this row to maintain alignment
    if (fread(row_buf, 1, sizeof(row_buf), fp) != sizeof(row_buf)) {
      fclose(fp);
      this->disable();
      this->cs_master_pin_->digital_write(true);
      return false;
    }
    delay(1);
    if ((line & 0x1F) == 0) App.feed_wdt();
    if ((line % 100) == 0) {
      ESP_LOGI(TAG, "Master row %d/%d", line, SCREEN_HEIGHT);
    }
  }
  this->disable();
  this->cs_master_pin_->digital_write(true);
  delay(50);

  // Slave half
  ESP_LOGI(TAG, "Slave half start");
  // Restart file from beginning for slave half
  fseek(fp, 0, SEEK_SET);
  this->cs_slave_pin_->digital_write(false);
  this->sendCommand(DTM);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (int line = 0; line < SCREEN_HEIGHT; line++) {
    // Discard master half of this row
    if (fread(row_buf, 1, sizeof(row_buf), fp) != sizeof(row_buf)) {
      fclose(fp);
      this->disable();
      this->cs_slave_pin_->digital_write(true);
      return false;
    }
    // Read and send slave half of this row
    size_t r = fread(row_buf, 1, sizeof(row_buf), fp);
    if (r != sizeof(row_buf)) {
      fclose(fp);
      this->disable();
      this->cs_slave_pin_->digital_write(true);
      return false;
    }
    this->write_array(row_buf, sizeof(row_buf));
    delay(1);
    if ((line & 0x1F) == 0) App.feed_wdt();
    if ((line % 100) == 0) {
      ESP_LOGI(TAG, "Slave row %d/%d", line, SCREEN_HEIGHT);
    }
  }
  this->disable();
  this->cs_slave_pin_->digital_write(true);
  fclose(fp);
  ESP_LOGI("epd_photo_frame", "Image data sent from file");
  return true;
}


}  // namespace epd_photo_frame
}  // namespace esphome
