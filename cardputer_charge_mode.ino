/*
 * CardPuter Charge Mode
 *
 * Minimal charging indicator for M5Stack CardPuter ADV (2025).
 * Turns off the display and shows battery level via RGB LED.
 *
 * LED Colors:
 *   Red    = 0-25%
 *   Orange = 25-50%
 *   Yellow = 50-75%
 *   Green  = 75-100%
 *
 * Inspired by: https://www.reddit.com/r/CardPuter/comments/1ecr1gb/
 *
 * Author: Szymon
 * License: MIT
 */

#include <driver/ledc.h>
#include <driver/rmt_tx.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

// ============================================================================
// Pin Definitions (CardPuter ADV)
// ============================================================================

#define LED_PIN      21   // WS2812 RGB LED
#define LCD_BL_PIN   38   // LCD backlight (PWM) - must stay on for LED power
#define LCD_MOSI     35   // SPI data
#define LCD_SCK      36   // SPI clock
#define LCD_CS       37   // SPI chip select
#define LCD_DC       34   // Data/Command
#define LCD_RST      33   // Reset

// ============================================================================
// Configuration
// ============================================================================

#define BATTERY_UPDATE_MS  10000  // Update LED color every 10 seconds
#define MIN_BACKLIGHT_DUTY 5000   // Minimum PWM duty to keep LED powered (~30%)

// Battery voltage thresholds (Li-Po: 3.0V empty, 4.2V full)
#define BATTERY_MIN_V  3.0f
#define BATTERY_MAX_V  4.2f

// ============================================================================
// Global Handles
// ============================================================================

rmt_channel_handle_t led_chan = NULL;
rmt_encoder_handle_t led_encoder = NULL;
spi_device_handle_t spi_lcd = NULL;

// ============================================================================
// Battery Functions
// ============================================================================

// Hysteresis: current color zone (0-3), -1 = unset
int current_zone = -1;

/**
 * Read battery level as percentage (0-100).
 * CardPuter ADV has 2:1 voltage divider on GPIO10 (ADC1_CH9 on ESP32-S3).
 * Uses 8-sample moving average for stability.
 */
int getBatteryLevel() {
  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(10);  // GPIO10
    delay(5);
  }
  float voltage = (sum / 8) * 2.0f / 1000.0f;  // 2:1 divider, mV to V

  int level = ((voltage - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V)) * 100;
  return constrain(level, 0, 100);
}

/**
 * Get LED color based on battery level with hysteresis.
 * 4 zones: Red (0-25%), Orange (25-50%), Yellow (50-75%), Green (75-100%)
 * Hysteresis band: +/-3% to prevent flickering at zone edges.
 */
void getBatteryColor(int level, uint8_t* r, uint8_t* g, uint8_t* b) {
  // Zone thresholds with 3% hysteresis
  int new_zone;
  if (current_zone == -1) {
    // First reading - no hysteresis
    if (level < 25) new_zone = 0;
    else if (level < 50) new_zone = 1;
    else if (level < 75) new_zone = 2;
    else new_zone = 3;
  } else {
    // Apply hysteresis - only change zone if we cross threshold +/- 3%
    new_zone = current_zone;
    if (current_zone == 0 && level >= 28) new_zone = 1;
    else if (current_zone == 1 && level < 22) new_zone = 0;
    else if (current_zone == 1 && level >= 53) new_zone = 2;
    else if (current_zone == 2 && level < 47) new_zone = 1;
    else if (current_zone == 2 && level >= 78) new_zone = 3;
    else if (current_zone == 3 && level < 72) new_zone = 2;
  }
  current_zone = new_zone;

  switch (new_zone) {
    case 0: *r = 255; *g = 0;   *b = 0;   break;  // Red
    case 1: *r = 255; *g = 128; *b = 0;   break;  // Orange
    case 2: *r = 255; *g = 255; *b = 0;   break;  // Yellow
    default: *r = 0;  *g = 255; *b = 0;   break;  // Green
  }
}

// ============================================================================
// WS2812 LED Driver (RMT-based)
// ============================================================================

typedef struct {
  rmt_encoder_t base;
  rmt_encoder_t *bytes_encoder;
  rmt_encoder_t *copy_encoder;
  int state;
  rmt_symbol_word_t reset_code;
} ws2812_encoder_t;

size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                     const void *data, size_t data_size, rmt_encode_state_t *ret_state) {
  ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
  rmt_encode_state_t session_state = RMT_ENCODING_RESET;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  size_t encoded = 0;

  switch (ws->state) {
    case 0:
      encoded += ws->bytes_encoder->encode(ws->bytes_encoder, channel, data, data_size, &session_state);
      if (session_state & RMT_ENCODING_COMPLETE) ws->state = 1;
      if (session_state & RMT_ENCODING_MEM_FULL) { state = (rmt_encode_state_t)(state | RMT_ENCODING_MEM_FULL); goto out; }
    case 1:
      encoded += ws->copy_encoder->encode(ws->copy_encoder, channel, &ws->reset_code, sizeof(ws->reset_code), &session_state);
      if (session_state & RMT_ENCODING_COMPLETE) { ws->state = RMT_ENCODING_RESET; state = (rmt_encode_state_t)(state | RMT_ENCODING_COMPLETE); }
      if (session_state & RMT_ENCODING_MEM_FULL) { state = (rmt_encode_state_t)(state | RMT_ENCODING_MEM_FULL); goto out; }
  }
out:
  *ret_state = state;
  return encoded;
}

esp_err_t ws2812_del(rmt_encoder_t *encoder) {
  ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
  rmt_del_encoder(ws->bytes_encoder);
  rmt_del_encoder(ws->copy_encoder);
  free(ws);
  return ESP_OK;
}

esp_err_t ws2812_reset(rmt_encoder_t *encoder) {
  ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
  rmt_encoder_reset(ws->bytes_encoder);
  rmt_encoder_reset(ws->copy_encoder);
  ws->state = RMT_ENCODING_RESET;
  return ESP_OK;
}

esp_err_t ws2812_encoder_new(rmt_encoder_handle_t *ret_encoder) {
  ws2812_encoder_t *encoder = (ws2812_encoder_t*)calloc(1, sizeof(ws2812_encoder_t));
  if (!encoder) return ESP_FAIL;

  encoder->base.encode = ws2812_encode;
  encoder->base.del = ws2812_del;
  encoder->base.reset = ws2812_reset;

  // WS2812 timing: bit0 = 300ns high + 900ns low, bit1 = 900ns high + 300ns low
  rmt_bytes_encoder_config_t bytes_cfg = {};
  bytes_cfg.bit0.level0 = 1; bytes_cfg.bit0.duration0 = 3;
  bytes_cfg.bit0.level1 = 0; bytes_cfg.bit0.duration1 = 9;
  bytes_cfg.bit1.level0 = 1; bytes_cfg.bit1.duration0 = 9;
  bytes_cfg.bit1.level1 = 0; bytes_cfg.bit1.duration1 = 3;
  bytes_cfg.flags.msb_first = 1;

  if (rmt_new_bytes_encoder(&bytes_cfg, &encoder->bytes_encoder) != ESP_OK) {
    free(encoder);
    return ESP_FAIL;
  }

  rmt_copy_encoder_config_t copy_cfg = {};
  if (rmt_new_copy_encoder(&copy_cfg, &encoder->copy_encoder) != ESP_OK) {
    rmt_del_encoder(encoder->bytes_encoder);
    free(encoder);
    return ESP_FAIL;
  }

  // Reset code: >50us low (use 80us for safety margin)
  encoder->reset_code.level0 = 0; encoder->reset_code.duration0 = 400;
  encoder->reset_code.level1 = 0; encoder->reset_code.duration1 = 400;

  *ret_encoder = &encoder->base;
  return ESP_OK;
}

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!led_chan || !led_encoder) return;
  uint8_t data[3] = {g, r, b};  // WS2812 uses GRB order
  rmt_transmit_config_t cfg = {};
  rmt_transmit(led_chan, led_encoder, data, sizeof(data), &cfg);
}

// ============================================================================
// LCD Control (ST7789)
// ============================================================================

void lcd_cmd(uint8_t cmd) {
  if (!spi_lcd) return;
  gpio_set_level((gpio_num_t)LCD_DC, 0);
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &cmd;
  spi_device_polling_transmit(spi_lcd, &t);
}

void lcd_data(uint8_t data) {
  if (!spi_lcd) return;
  gpio_set_level((gpio_num_t)LCD_DC, 1);
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &data;
  spi_device_polling_transmit(spi_lcd, &t);
}

/**
 * Initialize ST7789 and put it to sleep.
 * Display stays off but backlight PWM keeps LED powered.
 */
void lcd_sleep() {
  // Hardware reset
  gpio_set_level((gpio_num_t)LCD_RST, 0);
  delay(10);
  gpio_set_level((gpio_num_t)LCD_RST, 1);
  delay(120);

  // Wake from sleep (required before sending commands)
  lcd_cmd(0x11);  // SLPOUT
  delay(120);

  // Configure display
  lcd_cmd(0x3A);  // COLMOD
  lcd_data(0x55); // 16-bit color
  lcd_cmd(0x36);  // MADCTL
  lcd_data(0x60); // Rotation

  // Turn off and sleep
  lcd_cmd(0x28);  // DISPOFF
  delay(10);
  lcd_cmd(0x10);  // SLPIN
  delay(100);
}

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
  // 1. Configure LCD backlight PWM
  //    Must maintain minimum duty cycle to power the LED
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.duty_resolution = LEDC_TIMER_14_BIT;
  timer_cfg.timer_num = LEDC_TIMER_0;
  timer_cfg.freq_hz = 1000;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_cfg);

  ledc_channel_config_t chan_cfg = {};
  chan_cfg.gpio_num = LCD_BL_PIN;
  chan_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  chan_cfg.channel = LEDC_CHANNEL_7;
  chan_cfg.timer_sel = LEDC_TIMER_0;
  chan_cfg.duty = MIN_BACKLIGHT_DUTY;
  chan_cfg.hpoint = 0;
  ledc_channel_config(&chan_cfg);

  // 2. Initialize SPI for LCD (non-fatal - LED is priority)
  //    Note: RGB LED and backlight share power. If SPI fails, we can't sleep
  //    the LCD, but we MUST keep backlight on or LED won't work. Trade-off:
  //    working LED indicator > sleeping display.
  gpio_set_direction((gpio_num_t)LCD_DC, GPIO_MODE_OUTPUT);
  gpio_set_direction((gpio_num_t)LCD_RST, GPIO_MODE_OUTPUT);

  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = LCD_MOSI;
  bus_cfg.miso_io_num = -1;
  bus_cfg.sclk_io_num = LCD_SCK;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  esp_err_t spi_err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

  // ESP_ERR_INVALID_STATE = already initialized (OK to continue)
  if (spi_err == ESP_OK || spi_err == ESP_ERR_INVALID_STATE) {
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = 40000000;
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = LCD_CS;
    dev_cfg.queue_size = 1;
    if (spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_lcd) == ESP_OK) {
      lcd_sleep();
    }
    // If add_device fails: LCD stays on but LED works (acceptable trade-off)
  }

  // 4. Initialize WS2812 LED via RMT (proceed regardless of LCD status)
  rmt_tx_channel_config_t tx_cfg = {};
  tx_cfg.gpio_num = (gpio_num_t)LED_PIN;
  tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_cfg.resolution_hz = 10000000;  // 10MHz = 100ns resolution
  tx_cfg.mem_block_symbols = 64;
  tx_cfg.trans_queue_depth = 4;
  if (rmt_new_tx_channel(&tx_cfg, &led_chan) != ESP_OK) return;
  if (ws2812_encoder_new(&led_encoder) != ESP_OK) return;
  if (rmt_enable(led_chan) != ESP_OK) return;

  // 5. Set initial LED color based on battery
  delay(50);
  int level = getBatteryLevel();
  uint8_t r, g, b;
  getBatteryColor(level, &r, &g, &b);
  setLedColor(r, g, b);
}

void loop() {
  static unsigned long last_update = 0;

  // Update LED color periodically
  if (millis() - last_update > BATTERY_UPDATE_MS) {
    last_update = millis();

    int level = getBatteryLevel();
    uint8_t r, g, b;
    getBatteryColor(level, &r, &g, &b);
    setLedColor(r, g, b);
  }

  delay(100);
}
