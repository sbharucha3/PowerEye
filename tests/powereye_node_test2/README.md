# PowerEye tests: what to expect

These sketches read the voltage and current channels from the ADS131M02 and show what the board is measuring. They do not generate example readings: the numbers should change with the signals connected to the inputs.

## Running a sketch

1. Open the `.ino` file for the test you want to run in Arduino IDE.
2. Select **Seeed XIAO ESP32S3**. For Test 2, enable **USB CDC On Boot**.
3. Upload the sketch, then open Serial Monitor at **115200 baud**.
4. Keep the Serial Monitor open and watch for repeated readings.

## PowerEye Test 1: check that readings arrive

Test 1 is the basic connection check. At startup, it prints a response from the ADC and a heading for the readings. If the ADC is sending data, a new line should appear about once a second with voltage/current readings. About every ten seconds it also prints an average.

In everyday terms, this test answers: **“Is the board receiving changing readings from the voltage and current inputs?”** It is mainly a data-flow check; its raw readings are not necessarily the actual voltage or current of the equipment.

If it repeatedly says **“No samples this second”**, the board is not seeing the ADC's data-ready signal. Check that the ADC is powered and that its data-ready wire is connected to the expected pin. An ADC ID of `0x0000` or `0xFFFF` can also mean the board is not communicating with the ADC.

## PowerEye Test 2: see an analysis of the readings

Test 2 gathers a short batch of input readings, analyzes it, and prints a section labeled `Window 0`, `Window 1`, and so on. With data arriving as expected, a new window should appear several times a second. Each section summarizes the voltage and current seen in that short interval:

| Serial Monitor label | Plain-language meaning |
|---|---|
| Voltage RMS / Current RMS | The overall level of the voltage/current signal during the window. |
| Real power | The power the analysis calculates is being used. |
| Apparent power / Reactive power | Other calculated descriptions of the voltage/current relationship. |
| Power factor | How closely the current and voltage are working together, on a scale usually near 0 to 1. |
| Energy (window) | Estimated energy for this short window, not a running total. |
| Frequency | How quickly the voltage signal repeats, in cycles per second (Hz). |
| Voltage THD / Current THD | An estimate of extra waveform content beyond a simple repeating wave; lower usually means a cleaner wave. |
| Voltage residual / Current residual | How much of each measured waveform the analysis did not explain with its fitted wave pattern. |
| Alerts | A warning when a measurement falls outside the limits set in the sketch; `none` means no configured limits were exceeded. |

If the voltage signal is too small or the window does not contain enough usable waveform information, the sketch may still print the basic measurements but will say that frequency/harmonic analysis is unavailable. An alert is a prompt to check the signal and configured limits, not by itself proof that the hardware is faulty.

## Important: readings need calibration

Test 2 currently uses default scale values of `1.0`. Until `V_SCALE` and `I_SCALE` are calibrated for the voltage and current sensor circuits, the displayed `V` and `A` values should **not** be treated as trustworthy real-world volts and amps. The alert limits also assume 120 V, 10 A, 60 Hz, and related default thresholds; adjust them to match the intended setup. The sketch expects the ADC to already be producing samples and does not configure its conversion rate.
