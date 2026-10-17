#pragma once
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <vector>

using esp_err_t = int;
using i2s_port_t = int;
using i2s_mode_t = int;
constexpr int ESP_OK = 0, I2S_NUM_0 = 0, I2S_MODE_MASTER = 1;
constexpr int I2S_MODE_RX = 2, I2S_MODE_PDM = 4, I2S_BITS_PER_SAMPLE_16BIT = 16;
constexpr int I2S_CHANNEL_FMT_ONLY_RIGHT = 1, I2S_COMM_FORMAT_STAND_I2S = 1;
constexpr int I2S_CHANNEL_MONO = 1, I2S_PIN_NO_CHANGE = -1;
struct i2s_config_t {
  int mode, sample_rate, bits_per_sample, channel_format, communication_format;
  int dma_buf_count, dma_buf_len;
};
struct i2s_pin_config_t { int mck_io_num, bck_io_num, ws_io_num, data_out_num, data_in_num; };
struct FakeI2S {
  std::deque<uint8_t> bytes;
  size_t read_calls = 0;
  size_t max_read = 4096;
  bool fail_read = false;
  bool fail_begin = false;
  bool installed = false;
  void feed(const std::vector<uint8_t> &chunk) { bytes.insert(bytes.end(), chunk.begin(), chunk.end()); }
};
inline FakeI2S I2S;
inline int i2s_driver_install(int, const i2s_config_t *config, int, void *) {
  assert(config->sample_rate == 16000 && config->bits_per_sample == 16);
  assert(config->mode == (I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM));
  assert(config->dma_buf_count == 8 && config->dma_buf_len == 160);
  if (I2S.fail_begin) return -1;
  I2S.installed = true;
  return ESP_OK;
}
inline int i2s_set_clk(int, int rate, int bits, int channels) {
  assert(rate == 16000 && bits == 16 && channels == I2S_CHANNEL_MONO);
  return ESP_OK;
}
inline int i2s_set_pin(int, const i2s_pin_config_t *pins) {
  assert(pins->ws_io_num == 42 && pins->data_in_num == 41);
  assert(pins->bck_io_num == -1 && pins->mck_io_num == -1 && pins->data_out_num == -1);
  return ESP_OK;
}
inline int i2s_driver_uninstall(int) { I2S.installed = false; return ESP_OK; }
inline int i2s_read(int, void *destination, size_t requested, size_t *bytes_read, unsigned wait) {
  assert(requested > 0 && wait == 20);
  I2S.read_calls++;
  *bytes_read = 0;
  if (I2S.fail_read) return -1;
  const size_t count = std::min({requested, I2S.max_read, I2S.bytes.size()});
  for (size_t i = 0; i < count; ++i) {
    static_cast<uint8_t *>(destination)[i] = I2S.bytes.front();
    I2S.bytes.pop_front();
  }
  *bytes_read = count;
  return ESP_OK;
}
