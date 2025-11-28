# CardPuter Charge Mode

Minimal charging indicator for M5Stack CardPuter ADV (2025).

Turns off the display and shows battery level via the built-in RGB LED.

## Inspiration

This project was inspired by [this Reddit post](https://www.reddit.com/r/CardPuter/comments/1ecr1gb/can_somebody_make_a_bin_that_turns_the_display/) requesting a charging mode with LED status indicator.

## Features

- Display completely off (saves power, prevents burn-in)
- LED color indicates battery level:
  - Red = 0-25%
  - Orange = 25-50%
  - Yellow = 50-75%
  - Green = 75-100%
- Updates every 10 seconds
- Works with M5Launcher (load as .bin from SD card)

## Requirements

- M5Stack CardPuter ADV (2025 model with ESP32-S3)
- arduino-cli with ESP32 board support
- USB-C cable

## Installation

### Using arduino-cli

```bash
# Clone the repository
git clone https://github.com/screenfluent/cardputer_charge_mode.git
cd cardputer_charge_mode

# Compile
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer .

# Upload (replace /dev/ttyACM0 with your port)
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:m5stack_cardputer .
```

### For M5Launcher

Export as .bin and copy to SD card:

```bash
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer --output-dir ./build .
# Copy build/cardputer_charge_mode.ino.bin to SD card
```

## Roadmap

- [ ] Splash screen showing battery percentage at startup
- [ ] Button press to temporarily show battery level on LCD
- [ ] Pre-built .bin releases

## Technical Notes

The CardPuter's LED and LCD backlight share a power rail. The backlight PWM must maintain a minimum duty cycle (~30%) for the LED to function. The display is put to sleep via ST7789 SLPIN command, so the screen appears off despite the backlight being partially on.

## License

MIT License - see code header for details.
