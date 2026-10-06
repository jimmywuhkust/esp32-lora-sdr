# Bandwidth/window experiment

Separate Arduino build for 203.125, 406.25 and 812.5 kHz LoRa bandwidths.
It scales down-chirp and quarter-SFD windows with the actual symbol duration,
and uses full-word rings when they fit. USB commands include `BW` in Hz.

Initial independent LR2021 checks:

| SF | Bandwidth | Up-chirp window | Complete CRC packets |
|---:|---:|---:|---:|
| 7 | 203.125 kHz | 15,000 samples | 3/3 |
| 7 | 406.25 kHz | 7,500 samples | 3/3 |
| 7 | 812.5 kHz | 3,750 samples | 3/3 |
| 8 | 406.25 kHz | 15,000 samples | 0/3 |
| 9 | 812.5 kHz | 15,000 samples | 0/3 |

An initial SF7/406.25 kHz run mistakenly retained a 15,000-sample window longer
than the symbol itself. It had 108 late updates and 0/3 receptions; the failed
log is preserved. Successful wider-band SF7 checks do not establish SF8/SF9
support or weak-signal recovery at the same bandwidth.

Build from this directory with `pio run`. This snapshot uses the historical
`xiao-stream-research` environment name, but its transport is windowed DAC.
