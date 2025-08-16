#include "epd_photo_frame.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <string>
#include "esphome/core/application.h"
#include "esphome/core/util.h"
#include "esphome/components/web_server_base/web_server_base.h"
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

  // Register HTTP upload handler
  if (web_server_base::global_web_server_base) {
    web_server_base::global_web_server_base->add_handler(this);
    ESP_LOGCONFIG(TAG, "Registered /epd/upload handler");
  }

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

bool EPDPhotoFrame::downloadFileToSpiffs(const std::string &url, const char *path) {
  ESP_LOGI(TAG, "Downloading %s -> %s", url.c_str(), path);
  if (!this->mountSpiffs()) {
    ESP_LOGE(TAG, "SPIFFS not mounted");
    return false;
  }
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 15000;
  cfg.method = HTTP_METHOD_GET;
  cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    ESP_LOGE(TAG, "http_client_init failed");
    return false;
  }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "http_client_open failed: %d", (int) err);
    esp_http_client_cleanup(client);
    return false;
  }
  int status = esp_http_client_fetch_headers(client);
  if (status >= 0) {
    int cl = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "HTTP headers ok, content-length=%d", cl);
  }
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    ESP_LOGE(TAG, "Open failed: %s", path);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  uint8_t buf[2048];
  int total = 0;
  while (true) {
    int r = esp_http_client_read(client, (char *) buf, sizeof(buf));
    if (r < 0) { ESP_LOGE(TAG, "http read error: %d", r); fclose(fp); esp_http_client_close(client); esp_http_client_cleanup(client); return false; }
    if (r == 0) break;
    fwrite(buf, 1, r, fp);
    total += r;
    if ((total & 0x0FFF) == 0) { App.feed_wdt(); delay(1); }
  }
  fclose(fp);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  ESP_LOGI(TAG, "Saved %d bytes to %s", total, path);
  return total > 0;
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
  esp_http_client_config_t cfg = {};
  cfg.url = this->image_url_.c_str();
  cfg.timeout_ms = 15000;
  cfg.method = HTTP_METHOD_GET;
  cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGE(TAG, "client init failed");
    if (download_success_binary_) download_success_binary_->publish_state(false);
    if (download_status_text_) download_status_text_->publish_state("init_failed");
    return;
  }
  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGE(TAG, "open failed");
    if (download_success_binary_) download_success_binary_->publish_state(false);
    if (download_status_text_) download_status_text_->publish_state("open_failed");
    esp_http_client_cleanup(client);
    return;
  }
  esp_http_client_fetch_headers(client);
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    ESP_LOGE(TAG, "open %s failed", path);
    if (download_success_binary_) download_success_binary_->publish_state(false);
    if (download_status_text_) download_status_text_->publish_state("file_open_failed");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }
  uint8_t buf[1024];
  int total = 0;
  while (true) {
    int r = esp_http_client_read(client, (char *) buf, sizeof(buf));
    if (r < 0) {
      ESP_LOGE(TAG, "read err %d", r);
      if (download_status_text_) download_status_text_->publish_state("read_err:" + to_string(r));
      break;
    }
    if (r == 0) break;
    fwrite(buf, 1, r, fp);
    total += r;
    // Yield to avoid WDT on other tasks; don't reset WDT from this task
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  fclose(fp);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  ESP_LOGI(TAG, "DL task done: %d bytes", total);
  if (download_bytes_sensor_) download_bytes_sensor_->publish_state(total);
  const int expected = (SCREEN_WIDTH * SCREEN_HEIGHT) / 2; // 960000
  if (total == expected) {
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
  // For now use in-memory upload buffer if present
  return !upload_buffer_.empty();
}

void EPDPhotoFrame::sendImageData() {
  ESP_LOGI(TAG, "Sending image data to display...");

  auto send_half = [this](GPIOPin *cs_pin, const uint8_t *start_ptr) {

    cs_pin->digital_write(false);
    this->sendCommand(DTM);
    this->dc_pin_->digital_write(true);
    this->enable();
    for (int line = 0; line < SCREEN_HEIGHT; line++) {
      const uint8_t *row = start_ptr + (line * BYTES_PER_ROW / 2);
      this->write_array(row, BYTES_PER_ROW / 2);
      if ((line & 0x1F) == 0) App.feed_wdt();
    }
    this->disable();
    cs_pin->digital_write(true);
  };
  const uint8_t *base = upload_buffer_.empty() ? nullptr : upload_buffer_.data();
  if (base == nullptr) {
    ESP_LOGW(TAG, "No image buffer; sending white screen");
    static uint8_t white[BYTES_PER_ROW / 2];
    memset(white, 0xFF, sizeof(white));
    // fallback: white both halves
    send_half(this->cs_master_pin_, white);
    delay(20);
    send_half(this->cs_slave_pin_, white);
    return;
  }

  // Left(master) half is first 300 bytes per row, right(slave) half is next 300
  send_half(this->cs_master_pin_, base);
  delay(20);
  send_half(this->cs_slave_pin_, base + (BYTES_PER_ROW / 2));
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

bool EPDPhotoFrame::canHandle(web_server_idf::AsyncWebServerRequest *request) const {
  return (request->method() == HTTP_POST && request->url() == "/epd/upload") ||
         (request->method() == HTTP_POST && request->url() == "/upload");
}

void EPDPhotoFrame::handleRequest(web_server_idf::AsyncWebServerRequest *request) {
  // For POST with body, we finalize in handleBody(). If there is a body, do nothing here.
  // If we reached here, either multipart upload is used (handled via handleUpload) or body will arrive in handleBody.
  // Send 100-continue-like acceptance by not responding here.
  ESP_LOGD(TAG, "handleRequest entry for %s (len=%u)", request->url().c_str(), (unsigned) request->contentLength());
}

void EPDPhotoFrame::handleBody(web_server_idf::AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    // Start streaming: initialize state and set up for master half
    streaming_upload_ = true;
    upload_expected_total_ = total;
    upload_complete_ = false;
    current_row_ = 0;
    row_fill_ = 0;
    master_dtm_sent_ = false;
    slave_dtm_sent_ = false;
    upload_expected_total_ = total;
    upload_complete_ = false;
    ESP_LOGI(TAG, "Upload start: total=%u", (unsigned) total);
    // Open SPIFFS file for write
    upload_fp_ = fopen("/spiffs/image.bin", "wb");
    if (!upload_fp_) {
      request->send(500, "text/plain", "Open failed");
      return;
    }
    upload_bytes_written_ = 0;
  }
  // Stream incoming bytes to file; we will render from file after upload
  size_t offset = 0;
  while (offset < len) {
    size_t to_write = len - offset;
    size_t w = fwrite(data + offset, 1, to_write, upload_fp_);
    offset += w;
    if (w == 0) break;
    upload_bytes_written_ += w;
    if ((offset & 0xFFF) == 0) App.feed_wdt();
  }
  ESP_LOGD(TAG, "Upload chunk wrote: len=%u", (unsigned) len);
  if (index + len >= total) {
    long pos = ftell(upload_fp_);
    fclose(upload_fp_);
    upload_fp_ = nullptr;
    ESP_LOGI(TAG, "Upload finalize file size=%ld", pos);
    if (pos == (long)(1200 * 1600 / 2)) {
      request->send(200, "text/plain", "OK");
      // Render from file
      if (!this->sendImageDataFromFile("/spiffs/image.bin")) {
        ESP_LOGE(TAG, "Failed to send image from file");
      }
      this->turnOnDisplay();
    } else {
      request->send(400, "text/plain", "Invalid size");
    }
  }
}

void EPDPhotoFrame::handleUpload(web_server_idf::AsyncWebServerRequest *request, const std::string &filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    ESP_LOGI(TAG, "Multipart upload start: %s total=%u", filename.c_str(), (unsigned) request->contentLength());
    upload_fp_ = fopen("/spiffs/image.bin", "wb");
    upload_bytes_written_ = 0;
    if (!upload_fp_) {
      request->send(500, "text/plain", "Open failed");
      return;
    }
  }
  if (upload_fp_ && len > 0) {
    fwrite(data, 1, len, upload_fp_);
    upload_bytes_written_ += len;
  }
  if (final) {
    fclose(upload_fp_);
    upload_fp_ = nullptr;
    ESP_LOGI(TAG, "Multipart upload finalize size=%u", (unsigned) upload_bytes_written_);
    if (upload_bytes_written_ == (size_t)(1200 * 1600 / 2)) {
      request->send(200, "text/plain", "OK");
      if (!this->sendImageDataFromFile("/spiffs/image.bin")) {
        ESP_LOGE(TAG, "Failed to send image from file");
      }
      this->turnOnDisplay();
    } else {
      request->send(400, "text/plain", "Invalid size");
    }
  }
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
