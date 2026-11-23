# 48 kHz ADC stream and FFT: implementation proposal

The application now has separate `Production` and `Test` target types.
`board_production.c` owns the physical capture peripherals, while
`board_test.c` supplies synthetic 1.5 kHz samples. Both use the shared
`main.c` DSP logic through `board.h`. The Test target passed 100/100
synthetic frames with a 1500 Hz, 0.500 full-scale peak.

Implementation is in `main.c` and `fir_coeffs.c`. The measured target
configuration uses a 120 MHz CPU PLL and a separate 48 MHz sampling clock.
The 192-tap FIR gives about 64 dB worst-case attenuation from 24 kHz upward;
the passband ripple is below 0.006 dB. The current AC6 build occupies about 88.5 KiB RAM.
With A0 near ground, 4,119 consecutive FFT frames completed with zero overruns
or DMA/FIFO errors and about 1.41 million processing cycles per frame, below
the 2.56 million cycle deadline. Analog sine and alias tests require an
external conditioned input and remain to be performed.

## Goal and proposed defaults

Acquire one 12-bit, single-ended analog signal on the PIC32CK2051GC01144
Curiosity Ultra board, produce a continuous 48,000-sample/s stream, and calculate
a 1024-point real FFT for each block. The first output will be the latest
spectrum and strongest frequency/magnitude in RAM for debugger inspection.
Streaming the full spectrum to a PC is outside this first implementation.

| Decision | Proposal |
| --- | --- |
| Input | Arduino A0 header, PA12 / ADC_VINP8 (144-pin package pin 36) |
| ADC mode | 12-bit, single-ended, unsigned, AVDD reference; calibrate at startup |
| Raw capture | 192,000 samples/s, one channel, timer-triggered |
| Output rate | FIR low-pass and decimate by 4 to 48,000 samples/s |
| FFT | 1024-point `arm_rfft_fast_f32`, non-overlapping Hann-windowed blocks |
| Processing result | 513 unique magnitude bins, peak bin/frequency, frame counter, overrun counter |

The 192 ksample/s capture choice respects the datasheet's `F_TPR >= 100 ksps`
minimum ADC throughput note and leaves an exact integer decimation path. The
ADC configuration must still calculate a theoretical throughput of at least
192 ksample/s. A 1024-sample output block spans 21.333 ms and has 46.875 Hz
bin spacing. Its FFT bins run from DC (0 Hz) to the 24 kHz Nyquist bin, but the
proposed filter specifies a usable passband only through approximately 20 kHz.
The 20-24 kHz interval is the filter transition band: its amplitudes are not
specified as accurate. Frequencies at or above 24 kHz must be suppressed before
decimation to prevent aliasing. Useful analog bandwidth also depends on the
input circuit.

## Data path

```text
48 MHz timer clock -> TCC0 overflow at 192 kHz
                  -> EVSYS asynchronous channel -> ADC VINP8 trigger
                  -> ADC 16-entry FIFO -> DMA trigger 61 -> ping-pong RAM buffers
                  -> 192 kHz to 48 kHz FIR decimator -> DC removal + Hann window
                  -> CMSIS-DSP real FFT -> magnitude / peak result
```

Use a timer clock that divides exactly to 192 kHz (48 MHz / 250 is one candidate)
and configure the core for a measured, supported higher clock, targeting 120 MHz.
The pack's current `SystemInit()` sets `SystemCoreClock` to 12 MHz and does not
configure these clocks. Route the timer overflow through an **asynchronous**
EVSYS path; the ADC chapter calls that path necessary for deterministic trigger
timing. Configure the ADC FIFO for the selected channel and request DMA whenever
it is nonempty. DMA reads whole 32-bit `PFFDATA` words; the sample is in bits
15:0 and the channel ID in bits 19:16. Check the channel ID while bringing up
the pipeline, then strip the sample to 12-bit values.

Use two DMA buffers of 4096 32-bit FIFO words each. A completed buffer yields
1024 output samples after decimation. The DMA completion interrupt only swaps
buffer ownership and records overruns; the main loop processes the completed
buffer while DMA fills the other. If the consumer still owns a buffer when DMA
needs it, count and report a dropped block rather than process overwritten data.

Design the decimation FIR for a passband through roughly 20 kHz and a stopband
starting at 24 kHz, targeting at least 60 dB alias rejection. Choose the tap
count from a measured response and cycle budget; do not hard-code a guessed
filter. Use CMSIS-DSP `arm_fir_decimate_f32` and `arm_rfft_fast_f32` (installed
pack 1.16.0), with conversion from ADC codes to centered `float32_t` samples.
Remove residual DC, apply a Hann window, compute magnitudes for bins 0..512,
and normalize reported amplitude for the window's coherent gain.

Budget roughly 80-100 KiB of RAM for DMA words, floating-point input and FIR
state, FFT input/output, window coefficients, and result storage. The device has
512 KiB SRAM, but the actual allocation and linker placement will be checked
after building. At 120 MHz each 1024-point output frame has about 2.56 million
core cycles available; benchmark conversion, decimation and FFT on target rather
than treating that estimate as a performance guarantee.

## Input circuit and limits

Single-ended ADC input must remain between AVSS and VREF; with AVDD selected
as VREF, use a signal conditioned to the board's analog supply range. A bipolar
audio or sensor signal needs bias and protection before A0. Add an analog
anti-alias filter ahead of the ADC for content above 96 kHz; the digital FIR
then rejects content above the 48 kHz stream's 24 kHz Nyquist frequency. A0/
PA12 is also labelled `USB0_DRD` in the board schematic, so keep that USB
function disabled and check the board loading before making accuracy claims.
The proposal uses A0 rather than PA09/A1 or PA10/A2, which are named in an AC/
ADC mux-short silicon erratum when the comparator is configured in a specific
way.

## Planned repository changes

1. Keep the working blinky as a recoverable baseline. Replace its `main.c`
   application loop only when implementing this proposal.
2. Add small modules for clocks/timer/EVSYS/ADC/DMA setup, buffer ownership,
   decimation and FFT analysis. Keep interrupt handlers limited to buffer
   bookkeeping.
3. Add `ARM::CMSIS:DSP` from the installed CMSIS-DSP pack to the CMSIS solution
   and add only required source/configuration files. Preserve the project-local
   boot-flash scatter correction that puts vectors at `0x08000000`.
4. Expose one `volatile` result structure with frame count, peak frequency,
   peak magnitude, FIFO/DMA overrun counts, processing cycle count, and a
   bounded spectrum array for live debugger reads.

## Verification and acceptance

1. Build and confirm vectors remain at `0x08000000`; load and stop in `main`.
2. With a known DC level, verify ADC range, channel ID, FIFO/DMA order and
   absence of overruns. Check the configured clock frequencies and actual
   192 kHz trigger cadence on the board.
3. Feed a conditioned sine wave (for example 1 kHz), then two tones. Confirm
   48 kHz output cadence, peak-bin location, amplitude scaling and stable
   results over at least one minute. Check a near-24 kHz and an out-of-band
   input for filter/alias behaviour.
4. Measure worst-case processing cycles per frame and RAM use. Accept only if
   processing completes before the next DMA buffer is full with margin and the
   overrun counters remain zero. Leave the target running when done.

## Review outcome

- Accepted: A0/PA12 input pin and the proposed RAM budget.
- Accepted: 1024-point FFT, 46.875 Hz bin spacing, and 21.333 ms frame period.
- Accepted: a specified 0-20 kHz passband. The FFT includes bins through
  24 kHz, but amplitudes in the 20-24 kHz transition band are not specified.
- Electrical input details still need to be checked before connecting a signal:
  its voltage range and source impedance must suit single-ended,
  AVDD-referenced sampling on A0. The proposed result remains peak
  frequency/magnitude plus a spectrum in RAM.

## Documentation used

- Microchip, *PIC32CK SG01/GC01 Curiosity Ultra User's Guide*, DS70005529C,
  p. 12 (Arduino A0 = PA12), p. 20 (A0/USB0_DRD board net):
  https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/UserGuides/PIC32CK-SG01-GC01-Curiosity-Ultra-User-Guide-DS70005529.pdf
- Microchip, *PIC32CK SG01/SG00/GC01/GC00 Family Data Sheet*, DS60001795H:
  pp. 65, 73 (PA12/VINP8); pp. 1754-1757 (ADC capabilities and event timing);
  p. 1766 (FIFO/DMA trigger 61); pp. 1781-1782 (throughput note);
  p. 1805 (32-bit FIFO word); p. 2140 (analog input range).
- Microchip, *PIC32CK SG01/GC01 Silicon Errata*, DS80001066E, p. 8
  (AC/ADC mux-short condition on PA09/PA10).
