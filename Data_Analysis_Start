print()
print()
print()
print()
print()

#Import anything
import math
import numpy as np
import matplotlib.pyplot as plt
from scipy.optimize import minimize_scalar

#SET THESE SAMPLING FREQUNCY GOALS
f = 60 #Hz
samples_per_second = 800
ac_cycles_per_datapoint = 10
measurements_per_datapoint = round((ac_cycles_per_datapoint / f) * samples_per_second)






################################### Make Variables
#User Inputted Data 
allowable_current = 10 #Amps
allowable_voltage = 20 #Volts
allowable_frequency = 60 #Hz
node_name = 'Tester_device'

#Collected Data empty vectors
time_measurements = np.zeros(measurements_per_datapoint)
voltage_measurements = np.zeros(measurements_per_datapoint)
current_measurements = np.zeros(measurements_per_datapoint)

#Make some fake data for my vectors for now
time_measurements = np.arange(measurements_per_datapoint) / samples_per_second
voltage_peak = 28        # V; stays below allowable_voltage of 20 V
current_peak = 10        # A; reaches allowable_current of 10 A
voltage_measurements = voltage_peak * np.sin(2 * math.pi * f * time_measurements)
current_measurements = current_peak * np.sin(2 * math.pi * f * time_measurements)

#More varibales i later realized i wanted to define -- will organize
sig_figs = 4
voltage_tolerance_percent = 5 #% more or less
frequency_tolerance = 0.5 #hz more or less
current_tolerance = 0 # I made a variable just in case we want to add some kind of tolerance
#Frequency stuff
frequency_search_percent = 10
number_of_harmonics = 15
voltage_thd_limit_percent = 5.0
current_thd_limit_percent = 5.0
waveform_residual_limit_percent = 3.0
minimum_ac_frequency_hz = 1.0
maximum_reliable_frequency_hz = 300.0







################################### Clean/spiffy up the incoming data

# any missing measurements or skipped measurements that should e flagged? 
# random electrical noise? -- apply a simple and not too constrained butterworth low pass filter 
# consider the voltage sensor offset, calibrate








################################### Calculate data on a single device for one single data point to be transmitted
#Find RMS values
V_rms = math.sqrt(np.mean(voltage_measurements ** 2)) #volts
I_rms = math.sqrt(np.mean(current_measurements ** 2)) #amps

#Find Real power
P_real = np.mean(voltage_measurements * current_measurements) #watts

#Find apparent power
S_apparent = V_rms * I_rms  # volt amps

#Find energy consumed ### based on defined measurements per time interval, maybe should pull from time vector
energy = P_real * ((measurements_per_datapoint / samples_per_second)/3600000)

#Find power factor ### not sure how useful this is yet
P_factor = P_real / S_apparent









################################### Find Frequency & THD
# IEEE 519-2022 is a useful reference. For systems at or below 1 kV,
# it specifies a 5% limit for individual voltage harmonics and an 8%
# limit for total voltage harmonic distortion at the point of common
# coupling (PCC)

# Find the strongest voltage frequency in the selected AC range.
fft_values = np.fft.rfft(voltage_measurements)

fft_frequencies = np.fft.rfftfreq(
    len(voltage_measurements),
    1 / samples_per_second
)

frequency_options = (
    (fft_frequencies >= minimum_ac_frequency_hz) &
    (fft_frequencies <= maximum_reliable_frequency_hz)
)

fft_magnitudes = np.abs(fft_values[frequency_options])
fft_frequency_options = fft_frequencies[frequency_options]

approximate_frequency = fft_frequency_options[np.argmax(fft_magnitudes)]


# Fine-tune the frequency by fitting a sine wave.
def sine_fit_error(test_frequency):

    sine_wave = np.sin(2 * np.pi * test_frequency * time_measurements)

    cosine_wave = np.cos(2 * np.pi * test_frequency * time_measurements)

    wave_options = np.column_stack((
        sine_wave,
        cosine_wave,
        np.ones(len(time_measurements))
    ))

    coefficients = np.linalg.lstsq(
        wave_options,
        voltage_measurements,
        rcond=None
    )[0]

    fitted_voltage = wave_options @ coefficients

    return np.mean((voltage_measurements - fitted_voltage) ** 2)


# Search near the strongest FFT result.
frequency_result = minimize_scalar(
    sine_fit_error,
    bounds=(
        max(
            minimum_ac_frequency_hz,
            approximate_frequency * (1 - frequency_search_percent / 100)
        ),
        min(
            maximum_reliable_frequency_hz,
            approximate_frequency * (1 + frequency_search_percent / 100)
        )
    ),
    method="bounded"
)

measured_frequency = frequency_result.x

# Use only harmonics that fit below the reliable frequency limit.
usable_harmonics = min(number_of_harmonics, int(maximum_reliable_frequency_hz // measured_frequency))

# Build the fundamental plus its harmonics.
harmonic_waves = [np.ones(len(time_measurements))]

for harmonic_number in range(1, usable_harmonics + 1):

    harmonic_frequency = harmonic_number * measured_frequency

    harmonic_waves.append(np.sin(2 * np.pi * harmonic_frequency * time_measurements))

    harmonic_waves.append(np.cos(2 * np.pi * harmonic_frequency * time_measurements))

harmonic_waves = np.column_stack(harmonic_waves)


# Fit the harmonic model to voltage and current.
voltage_coefficients = np.linalg.lstsq(
    harmonic_waves,
    voltage_measurements,
    rcond=None
)[0]

current_coefficients = np.linalg.lstsq(
    harmonic_waves,
    current_measurements,
    rcond=None
)[0]

fitted_voltage = harmonic_waves @ voltage_coefficients
fitted_current = harmonic_waves @ current_coefficients

# Calculate RMS value of each harmonic.
voltage_harmonics = []
current_harmonics = []

for harmonic_number in range(1, usable_harmonics + 1):

    i = 2 * harmonic_number - 1

    voltage_harmonic_rms = math.sqrt(
        voltage_coefficients[i] ** 2 +
        voltage_coefficients[i + 1] ** 2
    ) / math.sqrt(2)

    current_harmonic_rms = math.sqrt(
        current_coefficients[i] ** 2 +
        current_coefficients[i + 1] ** 2
    ) / math.sqrt(2)

    voltage_harmonics.append(voltage_harmonic_rms)
    current_harmonics.append(current_harmonic_rms)

# THD is higher-harmonic RMS divided by fundamental RMS.
voltage_THD = (math.sqrt(sum(value ** 2 for value in voltage_harmonics[1:]))/ voltage_harmonics[0] * 100)
current_THD = (math.sqrt(sum(value ** 2 for value in current_harmonics[1:]))/ current_harmonics[0] * 100)

# Residual detects non-harmonic waveform distortion and noise.
voltage_residual_percent = (math.sqrt(np.mean((voltage_measurements - fitted_voltage) ** 2))/ math.sqrt(np.mean((voltage_measurements - np.mean(voltage_measurements)) ** 2)) * 100)
current_residual_percent = (math.sqrt(np.mean((current_measurements - fitted_current) ** 2))/ math.sqrt(np.mean((current_measurements - np.mean(current_measurements)) ** 2)) * 100)

# Check for abnormal frequency and waveform conditions.
abnormal_condition_alerts = []

if abs(measured_frequency - f) > frequency_tolerance:
    abnormal_condition_alerts.append("Frequency is outside the allowed tolerance.")

if voltage_THD > voltage_thd_limit_percent:
    abnormal_condition_alerts.append("Voltage harmonic distortion is too high.")

if current_THD > current_thd_limit_percent:
    abnormal_condition_alerts.append("Current harmonic distortion is too high.")

if voltage_residual_percent > waveform_residual_limit_percent:
    abnormal_condition_alerts.append("Voltage waveform has unusual distortion or noise.")

if current_residual_percent > waveform_residual_limit_percent:
    abnormal_condition_alerts.append("Current waveform has unusual distortion or noise.")










#################################### Print a summary of node information 
print("\n" + "=" * 40)
print("Summary Info Of This Datapoint")
print("=" * 40)

print(f"Measured Frequency: {measured_frequency:.4f} Hz")
print(f"Voltage RMS: {V_rms:.4g} V")
print(f"Current RMS: {I_rms:.4g} A")
print(f"Average Real Power: {P_real:.4g} W")
print(f"Apparent Power: {S_apparent:.4g} VA")
print(f"Energy Consumed: {energy:.4g} kWh")
print(f"Power Factor: {P_factor:.4g}")
print(f"Voltage THD: {voltage_THD:.4f} %")
print(f"Current THD: {current_THD:.4f} %")










################################### Detect Other Abnormal Conditions
print("\n")
print("=" * 40)
print("Abnormal Conditions")
print("=" * 40)

# Check voltage
if V_rms > allowable_voltage * (1 + voltage_tolerance_percent/100):
    print(f"WARNING: Overvoltage!!! {V_rms:.4g} V")
elif V_rms < allowable_voltage * (1 - voltage_tolerance_percent/100):
    print(f"WARNING: Undervoltage!!! {V_rms:.4g} V")
   
# Check current
if I_rms > allowable_current:
    print(f"WARNING: Overcurrent! {I_rms:.4g} A")

if voltage_THD > voltage_thd_limit_percent:
    print(
        f"WARNING: Voltage THD is too high: {voltage_THD:.4f} %"
    )

if current_THD > current_thd_limit_percent:
    print(
        f"WARNING: Current THD is too high: {current_THD:.4f} %"
    )










############################# print some information to check what i am working with

# Print 10 randomly chosen raw measurements just to see
number_of_preview_values = min(10, len(time_measurements))
random_start_index = np.random.randint(0,len(time_measurements) - number_of_preview_values + 1)
random_end_index = random_start_index + number_of_preview_values

print("\n\n" + "=" * 52)
print("Random Raw Data Preview")
print("=" * 52)
print(f"{'Time (s)':>12} {'Voltage (V)':>14} {'Current (A)':>14}")
print("-" * 52)

for index in range(random_start_index, random_end_index):
    print(
        f"{time_measurements[index]:12.6f} "
        f"{voltage_measurements[index]:14.4f} "
        f"{current_measurements[index]:14.4f}"
    )
print("=" * 52)

#plot currnet, frequeny, and voltage
fft_values = np.fft.rfft(voltage_measurements)
fft_frequencies = np.fft.rfftfreq(len(voltage_measurements), 1 / samples_per_second)

# Scale spectrum to approximately peak-voltage amplitude.
fft_magnitudes = 2 * np.abs(fft_values) / len(voltage_measurements)

# Show up to the 5th harmonic, while never exceeding Nyquist frequency.
maximum_plot_frequency = min(5 * f, samples_per_second / 2)

# Make three clean plots on one page.
figure, axes = plt.subplots(3, 1, figsize=(10, 9), constrained_layout=True)

# Frequency spectrum
axes[0].plot(fft_frequencies, fft_magnitudes, color="purple", linewidth=1.5)
axes[0].set_xlim(0, maximum_plot_frequency)
axes[0].set_title("Voltage Frequency Spectrum")
axes[0].set_xlabel("Frequency (Hz)")
axes[0].set_ylabel("Magnitude (V peak)")
axes[0].grid(True, alpha=0.3)

# Current waveform
axes[1].plot(
    time_measurements,
    current_measurements,
    color="tab:blue",
    linewidth=1.5
)
axes[1].set_title("Current Waveform")
axes[1].set_xlabel("Time (seconds)")
axes[1].set_ylabel("Current (A)")
axes[1].grid(True, alpha=0.3)

# Voltage waveform
axes[2].plot(
    time_measurements,
    voltage_measurements,
    color="tab:red",
    linewidth=1.5
)
axes[2].set_title("Voltage Waveform")
axes[2].set_xlabel("Time (seconds)")
axes[2].set_ylabel("Voltage (V)")
axes[2].grid(True, alpha=0.3)

plt.show()

















print()
print()
print()
print()
print()