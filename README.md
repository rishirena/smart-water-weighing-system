# Smart Water Weighing System

An IoT based system that weighs water, shows the weight on a display, sends it to the internet, and pours out an exact amount of water on command from anywhere.

Built with an Arduino Uno, an ESP8266, a load cell, a small pump, and ThingSpeak.

## The problem

In many industries (food and beverage units, chemical mixing, small process plants, labs) liquid has to be added in a fixed quantity again and again. Today this is mostly done by hand: someone watches a jug or a scale and closes the valve when it "looks right".

This causes a few real problems:

- Wrong quantity. Humans over-pour or under-pour, so batches are not consistent.
- Wasted material and time. Someone has to stand there and watch every time.
- No record. Nobody knows how much was actually dispensed or what the level is right now, unless they walk up to it.
- No remote control. If the operator is not at the machine, nothing happens.

# Our solution

We built a system that measures liquid by 'Weight' instead of by eye, and controls the pump on its own.

- A load cell under the container measures the weight all the time.
- The weight is shown on a big LED display next to the machine and also uploaded to the cloud.
- The operator types the amount needed (for example 150 ml) from a phone or laptop.
- The pump starts, and **stops by itself** once that much water has been added.

We use weight because it is simple and reliable: 1 gram of water is 1 millilitre, so 150 g of extra weight means 150 ml of water.


## How it works (the big picture)
````
 ThingSpeak (cloud)
        ^
        |  WiFi
        v
   ESP8266 NodeMCU  <------ RS485 cable ------>  Arduino Uno
   (internet side)                                 |
                                                   |-- HX711 + load cell (weight)
                                                   |-- Dot matrix display
                                                   |-- L298N driver --> water pump
                                                   |-- Relay (optional)
```

Two boards are used because they are good at different things.

- The **Arduino Uno** does the real-time work: reading the weight, running the display, controlling the pump. It keeps working even if the internet is down.
- The **ESP8266** does only the WiFi part: talking to ThingSpeak.
- They are connected with **RS485**, which is the same kind of cable link used in industry because it works over long distances and is not easily disturbed by electrical noise.

### What happens when you dispense water

1. You enter `150` in Field 3 of the ThingSpeak channel.
2. The ESP8266 reads it (it checks every 16 seconds).
3. It sends `ML:150` to the Arduino over RS485.
4. The Arduino notes the current weight and switches the pump on.
5. While pouring, the display counts down the remaining ml: 150, 100, 50...
6. When the weight has increased by 150 g, the Arduino switches the pump off and shows `DONE`.
7. The ESP8266 resets Field 3 back to 0, so you can enter 150 again next time.

---

## Components

| Part | What it does |
|------|--------------|
| Arduino Uno | Main controller |
| ESP8266 NodeMCU | WiFi and cloud connection |
| Load cell + HX711 module | Measures weight |
| 4 in 1 FC16 dot matrix display (MAX7219) | Shows weight and remaining ml |
| L298N motor driver | Switches the pump on and off |
| DC water pump | Moves the water |
| 2 x RS485 (MAX485) modules | Link between the two boards |
| Relay module (optional) | Extra on/off output, controlled from the cloud |
| 12 V power supply | Powers the pump only |

---

## Wiring

### Arduino Uno

| Arduino pin | Goes to |
|-------------|---------|
| D0 (RX) | RS485 module RO |
| D1 (TX) | RS485 module DI |
| D4 | RS485 module DE and RE (joined together) |
| D3 | HX711 DT |
| D2 | HX711 SCK |
| D5 | L298N IN1 |
| D6 | L298N ENA |
| D8 | L298N IN2 |
| D7 | Relay IN (optional) |
| D10 | Display CS |
| D11 | Display DIN |
| D13 | Display CLK |
| 5V and GND | HX711, display, RS485 module, L298N logic |

### ESP8266 NodeMCU

| NodeMCU pin | Goes to |
|-------------|---------|
| D5 (GPIO14) | RS485 module RO |
| D6 (GPIO12) | RS485 module DI |
| D4 (GPIO2) | RS485 module DE and RE (joined together) |
| 3V3 and GND | RS485 module power |

### RS485 link between the boards

- A terminal to A terminal
- B terminal to B terminal
- GND to GND (this common ground is important, without it communication fails)

### Load cell to HX711

| Load cell wire | HX711 |
|----------------|-------|
| Red | E+ |
| Black | E- |
| White | A- |
| Green | A+ |

### L298N and pump

| L298N | Connects to |
|-------|-------------|
| OUT1 | Pump + |
| OUT2 | Pump - |
| 12V (VIN) | 12 V supply + |
| GND | 12 V supply - and Arduino GND |

**Power warnings**

- The 12 V supply goes only to the L298N. Never connect it to the Arduino or the ESP8266.
- The ESP8266 works at 3.3 V, so its RS485 module must be powered from 3V3.
- All grounds must be joined together.

---

## ThingSpeak setup

Create a channel with three fields:

| Field | Meaning | Who writes it |
|-------|---------|---------------|
| Field 1 | Weight in grams | The ESP8266 |
| Field 2 | Relay: 1 is on, 0 is off | You |
| Field 3 | Pump target in ml, for example 150 | You (it is reset to 0 automatically) |

Copy the channel ID, write API key and read API key into `esp8266_node/secrets.h`.

The free ThingSpeak account allows one update every 15 seconds, so the ESP8266 works in a 16 second cycle.

---

## How the code works

The project has three sketches.

```
arduino_uno/arduino_uno.ino      runs on the Arduino Uno
esp8266_node/esp8266_node.ino    runs on the ESP8266
esp8266_node/secrets.h           WiFi and ThingSpeak keys
calibration/calibration.ino      one time helper to calibrate the scale
```

### Arduino Uno code

The main loop runs four small jobs again and again, and none of them block each other:

1. **Read the scale.** About every 200 ms it takes a reading from the HX711 and smooths it a little so the number does not jump around.
2. **Check the pump.** If the pump is running, it compares the weight now with the weight when the pump started. When the difference reaches the target, the pump stops.
3. **Update the display.** It shows the weight in grams (like `250g`), the remaining ml while pouring (like `100ml`), or short messages (`READY`, `DONE`, `FAIL`, `ERR`). It only redraws when the text changes, so there is no flicker.
4. **Listen on RS485.** It reads incoming commands and replies.

The commands it understands:

| Command | Meaning |
|---------|---------|
| `R` | Send me your status |
| `ML:150` | Pour 150 ml |
| `STOP` | Stop the pump now |
| `ON` / `OFF` | Switch the relay |

After every command the Arduino replies with one line, for example `W:250,P:0,R:1`, which means weight 250 g, pump off, relay on.

**Safety built in**

- If the load cell stops giving readings while the pump is running, the pump is switched off and the display shows `FAIL`.
- If one pour takes longer than 2 minutes (empty tank, blocked pipe), the pump is switched off.
- A new pour command is ignored while a pour is already running.

### ESP8266 code

Every 16 seconds it does one cycle:

1. Ask the Arduino for its status (`R`).
2. Upload the weight to ThingSpeak Field 1.
3. Read Field 2 and, if it is different from the relay state, send `ON` or `OFF`.
4. Read Field 3. If it is above 0 and the pump is idle, send `ML:<value>`. Once the Arduino confirms the pump started, the next upload also writes 0 into Field 3.

If WiFi drops, it reconnects on its own.

### Why the ESP8266 is the "master" on RS485

RS485 is half duplex, which means only one board can talk at a time. If both talked whenever they wanted, the messages would crash into each other. So the ESP8266 always speaks first, and the Arduino only replies. This keeps the link clean.

---

## Libraries needed

Install from the Arduino IDE Library Manager:

- HX711 by Bogdan Necula
- MD_Parola by MajicDesigns
- MD_MAX72XX by MajicDesigns
- ESP8266 board package (for the ESP8266 sketch)

---

## Setting it up

1. Open `esp8266_node/secrets.h` and fill in your WiFi name, password, and ThingSpeak keys. Do not upload your real keys to GitHub.
2. **Calibrate the scale.** Upload `calibration/calibration.ino` to the Arduino. In the Serial Monitor (9600 baud) send `t` with the scale empty. Put a known weight on it and send `c 500` if it is 500 g. Copy the number it prints into `CAL_FACTOR` in `arduino_uno.ino`.
3. Upload `arduino_uno/arduino_uno.ino` to the Arduino Uno. **Unplug the RS485 wires from D0 and D1 while uploading**, then plug them back in.
4. Upload `esp8266_node/esp8266_node.ino` to the NodeMCU. Open the Serial Monitor at 115200 baud to see what it is doing.
5. Switch on with the container empty. The scale zeroes itself at start up.

---

## Good to know

- The weight shown is 0 until the scale has zeroed, so give it a couple of seconds after power on.
- If the display says `ERR` or the weight never changes, check the HX711 wires. If DT and SCK are connected the other way round on your board, swap the two `HX_DT_PIN` and `HX_SCK_PIN` lines at the top of the sketch.
- ThingSpeak is checked every 16 seconds, so there is a small delay between typing an amount and the pump starting.
- A little water keeps flowing in the pipe after the pump stops, so the final amount can be a few grams above the target. Calibration and pump speed affect this.

## Possible improvements

- Use MQTT for faster response than the 16 second polling.
- Add a small keypad or buttons for dispensing without the internet.
- Add a flow sensor as a second measurement.
- Log every batch with time and quantity for records.
