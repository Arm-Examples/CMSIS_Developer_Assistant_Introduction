# CMSIS Developer Assistant Introduction

CMSIS Developer Assistant (CDA) gives an AI agent access to the CMSIS build System, debugger, project documentation,
target registers and connected hardware. It supports development from project setup through delivery with CI workflows,
debugging and documentation retrieval from software packs.

## Quick Start

Start using CDA in Vs Code with these three easy steps:

1. Install [Keil Studio](https://marketplace.visualstudio.com/items?itemName=Arm.keil-studio-pack).
2. Install [CMSIS Developer Assistant](https://marketplace.visualstudio.com/items?itemName=Arm.cmsis-developer-assistant)
3. [Configure it](https://github.com/Open-CMSIS-Pack/CMSIS-Developer-Assistant#get-started) for your AI agent.

## Webinar Recording and Collaterals

[![CMSIS Developer Assistant Webinar Recording](./docs/preview-image.png)](https://armkeil.blob.core.windows.net/developer/Files/videos/KeilStudio/20261119_CDA_Intro.mp4)

### Slides

Review the [webinar slides](./docs/20261119_Getting_Started_with_CDA.pdf) to get access to the links provided in the
webinar.

### Prompts

The following prompts were used to create the [example](#example):

#### Project Creation

```txt
I have a PIC32CK GC Curiosity Ultra Development Board connected to my PC. Based on the documentation available here: https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/UserGuides/PIC32CK-SG01-GC01-Curiosity-Ultra-User-Guide-DS70005529.pdf, create a blinky application for it.
```

#### Ask about Peripherals

```txt
Which peripherals are available in my device?
```

#### Define Intent

```txt
Can I use one of them to capture a 12-bit analog input at 48 kHz sampling rate and then analyze the input stream with an FFT?
```

#### Create Specification

```txt
Create an implementation proposal and let me review it.
```

#### Check Technical Detail

```txt
The proposal looks good. Selected pin is ok. Usage of RAM is OK. Resolution and frame period sound ok. Is the frequency bans 0 - 24 kHz?
```

#### Implement

```txt
Start the implementation
```

#### Restructuring the Application

```txt
Create board layers that isolates peripherals from application logic. One board layer for production, one board layer for testing. And add two targets.
```

## Example

This example turns the [PIC32CK GC Curiosity Ultra board](https://www.microchip.com/en-us/development-tool/ev44p93a) into a real-time
spectrum analyzer. It captures a 12-bit analog signal, reduces the sample
rate to 48 ksample/s, and computes a 1024-point FFT. A second target feeds
the same processing code with a known sine wave so the signal-processing
path can be checked on the board without an external signal generator. See
the [application block diagram](BLOCK_DIAGRAM.md) for both data paths.

The CMSIS solution targets the PIC32CK2051GC01144 on the Curiosity Ultra
board. Connect a single-ended signal to Arduino A0 (PA12/ADC_VINP8) with
voltage within AVSS..AVDD. A bipolar source needs biasing and input
protection. Use an analog input filter to limit content above 96 kHz.

The firmware samples at 192 ksample/s using TCC0, EVSYS, the 12-bit ADC FIFO,
and DMA. A 192-tap FIR decimates by four to 48 ksample/s. Each 1024-sample
frame is DC-subtracted, Hann-windowed, and processed by a CMSIS-DSP real FFT.
Frames span 21.333 ms; bins are 46.875 Hz apart. Bins through 20 kHz form
the specified passband. The 20–24 kHz FFT interval is the filter transition
band. The FIR has at least 64 dB attenuation from 24 kHz upward.

Open `Blank.csolution.yml` in the CMSIS Solution extension. Select one of
two target types, then Build and Load and Debug:

| Target | Board layer | Input and behavior |
| --- | --- | --- |
| `Production` | `board_production.c` | A0/PA12 ADC, TCC0, EVSYS, DMA; LED0 (PD20) toggles per processed frame |
| `Test` | `board_test.c` | 100 repeatable synthetic frames of a 1.5 kHz, 0.5 full-scale sine; then sleeps |

The shared DSP application is `main.c`. It uses only the functions in
`board.h`; it does not access peripheral registers. The CMSIS layer files
`BoardProduction.clayer.yml` and `BoardTest.clayer.yml` select exactly one
board implementation per target. Stop an active debug session before
switching targets, then build the selected target before loading it.

### Project files

| File | Purpose |
| --- | --- |
| `Blank.csolution.yml` | Defines the device, CMSIS packs, compiler, build variants, and `Production` and `Test` targets. |
| `Blank.cproject.yml` | Lists shared application sources and CMSIS components; attaches one board layer to each target. |
| `BoardProduction.clayer.yml` | Adds `board_production.c` to the `Production` target. |
| `BoardTest.clayer.yml` | Adds `board_test.c` to the `Test` target. |
| `board.h` | Shared board interface, frame and status types, and capture/FFT size constants. |
| `board_production.c` | Configures clocks, A0/PA12 ADC capture, TCC0 event triggering, DMA double buffering, and LED0; supplies raw frames to the application. |
| `board_test.c` | Supplies 100 synthetic 1.5 kHz frames and checks each FFT peak and amplitude. |
| `main.c` | Shared frame-processing loop: ADC-code conversion, FIR decimation, DC removal, Hann window, FFT, magnitude spectrum, and peak search. |
| `fir_coeffs.c`, `fir_coeffs.h` | The 192-tap low-pass filter coefficients and their declaration. |
| `BLOCK_DIAGRAM.md` | Diagram of the two input paths and shared DSP pipeline. |
| `IMPLEMENTATION_PROPOSAL.md` | Design choices and calculations behind the implementation. |
| `blinky_baseline.c` | Earlier LED-only example, retained for reference and excluded from both targets. |
| `vcpkg-configuration.json` | Toolchain and CMSIS Toolbox dependencies used by the development environment. |
| `RTE/Device/PIC32CK2051GC01144/` | Device startup/system files and the AC6 scatter file used for the PIC32CK memory layout. |

CMSIS Toolbox also creates build indexes, generated RTE headers, and build
output directories; these are not application source files.

Pause the target to inspect:

| Symbol | Meaning |
| --- | --- |
| `spectrum[0..512]` | Magnitude in approximate peak full-scale units |
| `peak_bin`, `peak_hz`, `peak_amplitude_fs` | Largest bin from 0–20 kHz |
| `dc_offset_fs` | Signed DC level relative to midscale |
| `board_status.capture_frames`, `fft_frames` | Acquired and processed frames |
| `fft_cycles` | CPU cycles for one frame; Production budget is 2,560,000 |
| `board_status.capture_overruns`, `board_status.dma_errors`, `board_status.adc_fifo_overflows` | Production capture health |
| `sample_min`, `sample_max`, `board_status.bad_channel_words` | Sample checks |
| `board_status.test_pass_frames`, `board_status.test_fail_frames` | Test target result; expected 100 and 0 |
| `setup_error` | Zero on successful clock and peripheral setup |

In Production, GCLK0 runs the CPU at 120 MHz from PLL0; GCLK1 keeps ADC and
TCC0 at 48 MHz. Test uses the 48 MHz reset CPU clock and does not run the
ADC or DMA.
The AC6 scatter file in `RTE/Device/PIC32CK2051GC01144` places vectors in
boot flash at `0x08000000`; program code is in program flash.

After the board-layer refactor, Production captured over 280 frames with
zero overrun, DMA, FIFO, or channel-ID errors and about 1.49 million
processing cycles per frame. Test passed all 100 synthetic frames, reporting
bin 32, 1500 Hz, and 0.500 full-scale amplitude. A conditioned external
sine input can be used later to validate analog gain and alias rejection.
