#include "mhz19.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mhz19 {

static const char *const TAG = "mhz19";
static const uint8_t MHZ19_REQUEST_LENGTH = 8;
static const uint8_t MHZ19_RESPONSE_LENGTH = 9;
static const uint8_t MHZ19_COMMAND_GET_PPM[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t MHZ19_COMMAND_ABC_ENABLE[] = {0xFF, 0x01, 0x79, 0xA0, 0x00, 0x00, 0x00, 0x00};
static const uint8_t MHZ19_COMMAND_ABC_DISABLE[] = {0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t MHZ19_COMMAND_ABC_GET_STATUS[] = {0xFF, 0x01, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t MHZ19_COMMAND_CALIBRATE_ZERO[] = {0xFF, 0x01, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t mhz19_checksum(const uint8_t *command) {
  uint8_t sum = 0;
  for (uint8_t i = 1; i < MHZ19_REQUEST_LENGTH; i++) {
    sum += command[i];
  }
  return 0xFF - sum + 0x01;
}

void MHZ19Component::setup() {
  if (this->abc_boot_logic_ == MHZ19_ABC_ENABLED) {
    this->abc_enable();
  } else if (this->abc_boot_logic_ == MHZ19_ABC_DISABLED) {
    this->abc_disable();
  }
}

void MHZ19Component::update() {
  uint8_t response[MHZ19_RESPONSE_LENGTH];
  if (!this->mhz19_write_command_(MHZ19_COMMAND_GET_PPM, response)) {
    ESP_LOGW(TAG, "Reading data from MHZ19 failed!");
    this->status_set_warning();
    return;
  }

  if (response[0] != 0xFF || response[1] != 0x86) {
    ESP_LOGW(TAG, "Invalid preamble from MHZ19!");
    this->status_set_warning();
    return;
  }

  uint8_t checksum = mhz19_checksum(response);
  if (response[8] != checksum) {
    ESP_LOGW(TAG, "MHZ19 Checksum doesn't match: 0x%02X!=0x%02X", response[8], checksum);
    this->status_set_warning();
    return;
  }

  this->status_clear_warning();
  const uint16_t ppm = (uint16_t(response[2]) << 8) | response[3];
  const int temp = int(response[4]) - 40;
  const uint8_t status = response[5];
  const uint8_t abc_timer = response[6];
  const uint8_t abc_cycle_count = response[7];

  ESP_LOGD(TAG, "MHZ19 Received CO₂=%uppm Temperature=%d°C Status=0x%02X ABC Timer=%u ABC Cycle Count=%u", ppm, temp, status, abc_timer, abc_cycle_count);
  // ESP_LOGD(TAG, "Raw Output: %02X %02X %02X %02X %02X %02X %02X", response[2], response[3], response[4], response[5], response[6], response[7], response[8] );

  if (this->co2_sensor_ != nullptr)
    this->co2_sensor_->publish_state(ppm);
  if (this->temperature_sensor_ != nullptr)
    this->temperature_sensor_->publish_state(temp);
  if (this->abc_timer_sensor_ != nullptr)
    this->abc_timer_sensor_->publish_state(abc_timer);
  if (this->abc_cycle_count_sensor_ != nullptr)
    this->abc_cycle_count_sensor_->publish_state(abc_cycle_count);

  // this->abc_get_status();
}

void MHZ19Component::calibrate_zero() {
  ESP_LOGD(TAG, "MHZ19 Calibrating zero point");
  this->mhz19_write_command_(MHZ19_COMMAND_CALIBRATE_ZERO, nullptr);
}

void MHZ19Component::abc_enable() {
  ESP_LOGD(TAG, "MHZ19 Enabling automatic baseline calibration");
  this->mhz19_write_command_(MHZ19_COMMAND_ABC_ENABLE, nullptr);
}

void MHZ19Component::abc_disable() {
  ESP_LOGD(TAG, "MHZ19 Disabling automatic baseline calibration");
  this->mhz19_write_command_(MHZ19_COMMAND_ABC_DISABLE, nullptr);
}

void MHZ19Component::abc_get_status() {
  uint8_t response[MHZ19_RESPONSE_LENGTH];
  if (!this->mhz19_write_command_(MHZ19_COMMAND_ABC_GET_STATUS, response)) {
    ESP_LOGW(TAG, "Reading data from MHZ19 failed!");
    this->status_set_warning();
    return;
  }
  ESP_LOGD(TAG, "MHZ19 getting ABC status");
  this->mhz19_write_command_(MHZ19_COMMAND_ABC_GET_STATUS, nullptr);

    if (response[0] != 0xFF || response[1] != 0x7D) {
    ESP_LOGW(TAG, "Invalid preamble from MHZ19!");
    this->status_set_warning();
    return;
  }

  uint8_t checksum = mhz19_checksum(response);
  if (response[8] != checksum) {
    ESP_LOGW(TAG, "MHZ19 Checksum doesn't match: 0x%02X!=0x%02X", response[8], checksum);
    this->status_set_warning();
    return;
  }

  this->status_clear_warning();
  const uint8_t abcstatus = response[7];

  ESP_LOGD(TAG, "MHZ19 ABC Status=0x%02X", abcstatus);
  ESP_LOGD(TAG, "Raw Output: %02X %02X %02X %02X %02X %02X %02X", response[2], response[3], response[4], response[5], response[6], response[7] );  
}

bool MHZ19Component::mhz19_write_command_(const uint8_t *command, uint8_t *response) {
  // Empty RX Buffer
  while (this->available())
    this->read();
  this->write_array(command, MHZ19_REQUEST_LENGTH);
  this->write_byte(mhz19_checksum(command));
  this->flush();

  if (response == nullptr)
    return true;

  return this->read_array(response, MHZ19_RESPONSE_LENGTH);
}
float MHZ19Component::get_setup_priority() const { return setup_priority::DATA; }
void MHZ19Component::dump_config() {
  ESP_LOGCONFIG(TAG, "MH-Z19:");
  LOG_SENSOR("  ", "CO2", this->co2_sensor_);
  LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);
  this->check_uart_settings(9600);

  if (this->abc_boot_logic_ == MHZ19_ABC_ENABLED) {
    ESP_LOGCONFIG(TAG, "  Automatic baseline calibration enabled on boot");
  } else if (this->abc_boot_logic_ == MHZ19_ABC_DISABLED) {
    ESP_LOGCONFIG(TAG, "  Automatic baseline calibration disabled on boot");
  }
}

}  // namespace mhz19
}  // namespace esphome
