# Battery estimation: X4 and X3

## X4

X4 exposes cell voltage through GPIO0 and USB presence through GPIO20. The
available board interface has no verified input for the charger red/green LED
or its charge-complete signal. Reading a high cell voltage is not equivalent
to detecting charge termination. See the [sample firmware pinout](https://github.com/CidVonHighwind/xteink-x4-sample)
and [CircuitPython pinout](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts).

The estimator treats charging percentages as approximate. In version 2.2.3
the former USB cap of 99% is removed: the existing slow upper-charge limiter
can reach an estimated 100% when the voltage target supports it. Initial USB
samples and restored 100% checkpoints still seed at no more than 99%, so they
cannot claim full immediately on boot. The final step advances through the
same rate limiter; it is not a charger-termination detector. Estimated 100%
can precede or follow the green LED, and may not be reached if unplugged
immediately at the LED transition. No extra voltage timer or unplug pulse is
used to manufacture a full indication.
`X4BatteryEstimator::update(..., chargeComplete)` supports a future verified
hardware signal; the current HAL deliberately leaves it false. Adding an
unverified GPIO read would risk conflicting with another peripheral.

The previous one-hour full-charge hold and its restart after sleep are removed.
There is a two-minute voltage-settling period after boot/unplug, followed by
bounded downward corrections. Recovery of terminal voltage after removing a
load cannot increase the displayed percentage during an awake battery-powered
session. An estimate can still be conservative after a sustained heavy load.

After power-off the charger may have charged the battery while the MCU could
not record it. Battery-powered boot therefore starts with a fresh voltage
estimate, whether it is above or below the saved percentage. USB-powered boot
uses the saved percentage bounded by the current sample and an initial 99%; without a
saved value it starts from the voltage estimate initially capped at 99%. That first
USB-only estimate is uncertain because voltage is already affected by charging.
The saved percentage is never evidence of an actual completed charge cycle.

Five ADC samples are median-filtered per poll, then the percentage target is
smoothed. Implausible voltage readings are rejected. The SDK cubic is used only
above 3200 mV: its low-voltage mathematical tail otherwise rises again and can
return 100% for a deeply discharged cell. At or below 3200 mV the target is 0%.
Voltage at or below 3500 mV for 20 seconds permits immediate downward correction
to avoid a stale high estimate masking a nearly empty cell. Invalid samples or
a gap over ten seconds break that confirmation. These are display policies,
not changes to the hardware charger or battery protection circuit.

Polling runs every 1.5 seconds from the main loop. Rendering reads an atomic
cache and requests an update when the whole percentage changes, including
while idle. NVS checkpoints are limited to USB transitions, ten-minute
intervals, and sleep. Serial diagnostics once per minute (and on USB edges):

```
[BAT] X4 mv=... raw=... estimate=... usb=... fullSignal=unavailable
```

For measurements after unplugging USB, create `/battery-calibration.enable`
in the SD card root and reboot. The optional logger appends to
`/battery-calibration.csv` every 15 seconds and on detected USB transitions.
Columns: `uptime_ms,millivolts,raw_percent,estimate_percent,usb`. Each boot adds
a comment marker because uptime restarts. Logging is disabled by default,
stops on a write error or at 8 MiB, and never deletes an older log. Remove the
enable file and reboot to disable it. The logger does not prevent sleep: for
a continuous awake trace, set the sleep timeout to Never, then restore your
normal setting afterwards. SD writes add a small measurement load; keep logging
and workload the same across calibration runs.

## Display scale and calibration limits

The X4 display scale keeps values through 80% unchanged, maps a base 92% to
96%, and preserves the 100% endpoint. Its correction is limited to four
percentage points. This presentation adjustment is based on the observed upper
range and is not a measurement of true state of charge. The saved checkpoint
uses the base estimate, preventing the scale from being applied twice on wake.

## Further characterization

The existing SDK curve is retained inside its useful voltage range. There is
no measured discharge curve for this particular cell/ADC, so the software
change must not be called a new hardware calibration. To obtain one:

1. Enable SD logging. Note the green LED transition manually, then unplug USB
   and retain samples at 2, 5, 10, 15 and 30 minutes after unplugging. A green
   LED event is not automatically present in the CSV; a USB transition is.
2. Log a normal reading discharge cycle with page-turn cadence, Wi-Fi and clock
   settings kept fixed. Repeat with a heavier load to separate voltage sag
   from actual capacity loss. Record the battery voltage near shutdown.
3. Compare at least two cycles before changing the voltage-to-percentage curve.
   Check ADC readings against a meter where practical.
4. To synchronize 100% with green, identify the actual charger chip and trace
   its status outputs on the exact board revision. Only connect a status GPIO
   after confirming its electrical levels and availability. The screen will
   still follow with poll/e-ink refresh latency.

Voltage-only characterization remains an estimate: load, relaxation,
temperature and battery age affect the mapping. Exact reference percentages
require measuring battery-side discharged charge against usable capacity.
A USB input energy meter measures the charger input, not battery charge.
See [TI's voltage-based SOC characterization limits](https://www.ti.com/lit/an/sluaaa1/sluaaa1.pdf).

The release incorporates user-tested X4 charging, reading discharge, sleep and
menu behavior. These observations are not an absolute capacity calibration.
The X3 gauge changes have host coverage but have not been tested on an X3 device.

## X3

SOC is read from BQ27220; values above 100 are rejected instead of becoming
false 100%. FC/DSG flags are not proof of USB presence. The available X3 wiring
has no independent verified VBUS signal: positive current can prove active
charging, but cannot prove cable absence after charge termination. The HAL
exposes that distinction instead of treating a latched FC flag as a cable.

The capacity loader retries transient idempotent I2C operations with bounds.
Security keys are ordered pairs and cannot safely be blindly retried as
individual commands. Exiting configuration must be confirmed before resealing;
sleep cleanup only depends on the gauge when a configuration/security change
may still be outstanding. Failed cleanup defers sleep with a 30-second retry
interval so the normal loop and idle power saving continue between attempts.
See [TI BQ27220 technical reference](https://www.ti.com/lit/ug/sluubd4a/sluubd4a.pdf).

## Host verification

`test/x4_battery` covers startup, USB, unplug settling, sleep, sustained low
voltage, invalid readings, time gaps, counter wrap, and rate-limit budgets.
Its injected voltage/percentage pairs exercise policy independently of the
voltage curve; they must not be mistaken for measured calibration points.
