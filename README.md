# CardPuter Charge Mode

Minimal charging indicator for M5Stack CardPuter ADV (2025).

Turns off the display and shows battery level via the built-in RGB LED.

## Features

- Display completely off (saves power, prevents burn-in)
- LED color indicates battery level:
  - Red = 0-25%
  - Orange = 25-50%
  - Yellow = 50-75%
  - Green = 75-100%
- Updates every 10 seconds

## How It Works

This app is designed to work alongside [M5Launcher](https://github.com/bmorcelli/M5Cardputer-Launcher).

**Typical workflow:**
1. Your CardPuter runs M5Launcher as the main firmware
2. You have `cardputer_charge_mode.ino.bin` on your SD card
3. When you want to charge:
   - Plug in USB-C power cable
   - Turn on CardPuter
   - Open M5Launcher and run the charge mode app
   - LED shows battery level, screen stays off
4. When LED turns green (75%+), you're charged
5. Restart to return to M5Launcher

With the display off, the device draws less power - charging is faster and there's no risk of screen burn-in from a static image.

## Quick Start

**For most users - no compiling needed!**

1. Download `cardputer_charge_mode.ino.bin` from [Releases](https://github.com/screenfluent/cardputer_charge_mode/releases)
2. Copy the `.bin` file to your SD card
3. Insert SD card into CardPuter
4. Open M5Launcher and run the bin file

That's it! The display will turn off and the LED will show your battery level.

## Building from Source

For developers who want to modify or compile the code themselves.

### Requirements

- [arduino-cli](https://arduino.github.io/arduino-cli/)
- ESP32 board support: `arduino-cli core install esp32:esp32`

### Compile & Flash

```bash
# Clone
git clone https://github.com/screenfluent/cardputer_charge_mode.git
cd cardputer_charge_mode

# Compile
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer .

# Flash (replace /dev/ttyACM0 with your port)
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:m5stack_cardputer .
```

### Export .bin

```bash
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer --output-dir ./build .
# Output: build/cardputer_charge_mode.ino.bin
```

## Technical Notes

The CardPuter's LED and LCD backlight share a power rail. The backlight PWM must maintain a minimum duty cycle (~30%) for the LED to function. The display is put to sleep via ST7789 SLPIN command, so the screen appears off despite the backlight being partially on.

## Roadmap

- [ ] Splash screen showing battery percentage at startup
- [ ] Button press to temporarily show battery level on LCD
- [ ] Green LED at 80% (healthy charge), blinking green at 100% (full)

## Inspiration

This project was inspired by [this Reddit post](https://www.reddit.com/r/CardPuter/comments/1ecr1gb/can_somebody_make_a_bin_that_turns_the_display/) requesting a charging mode with LED status indicator.

## License

MIT
