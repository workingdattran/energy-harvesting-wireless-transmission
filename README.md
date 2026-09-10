# Energy Harvesting & Wireless Data Transmission System

## Overview
This repository contains the hardware schematics and firmware source code for a self-sustaining IoT system that harvests solar energy and transmits real-time telemetry data[cite: 1]. Developed as a course project for Information Theory at Hanoi University of Science and Technology (HUST)[cite: 1], the system utilizes a Joule Thief circuit to boost low-voltage solar power into a supercapacitor, and an ESP32-S3 microcontroller to transmit voltage data via the ESP-NOW protocol[cite: 1]. 

To optimize bandwidth and energy consumption, the firmware implements data compression using Differential Pulse Code Modulation (DPCM) combined with Huffman coding, compressing 32-bit floating-point data down to a single bit during stable charging states[cite: 1].

## System Architecture

The architecture is built upon the classic Shannon communication model, consisting of two main nodes[cite: 1]:

### 1. Transmitter Node (ESP32-S3 + Joule Thief)
*   **Energy Harvesting:** A 3V / 210 mA polycrystalline solar panel powers a custom blocking oscillator (Joule Thief) using a 2N2222 NPN transistor, a bifilar ferrite toroid (15 turns), and a 1N5822 Schottky diode[cite: 1].
*   **Storage & Protection:** Energy is stored in a 5.5V - 1F supercapacitor[cite: 1]. A TL431 shunt regulator paired with a 47kΩ voltage divider caps the voltage at 5.0V to protect the supercapacitor, while keeping static leakage current minimal (~0.05 mA)[cite: 1].
*   **ADC Processing:** The ESP32-S3 reads the capacitor's voltage via its 12-bit ADC, utilizing a 64-sample oversampling technique and linear calibration to eliminate quantization noise[cite: 1].
*   **Source Coding:** 
    *   **DPCM:** Calculates the prediction error of the voltage based on charging inertia[cite: 1].
    *   **Huffman Coding:** Encodes the error into variable-length bit streams, dynamically reducing payload size[cite: 1].

### 2. Receiver Node (ESP32-S3)
*   Operates continuously in RX mode[cite: 1].
*   Receives payload via ESP-NOW, decodes the Huffman sequence, reconstructs the original voltage using inverse DPCM, and outputs performance metrics via UART (115200 baud) for logging[cite: 1].

## Hardware Bill of Materials (BOM)
| Component | Specification | Purpose |
| :--- | :--- | :--- |
| **MCU** | 2x ESP32-S3 Dev Module | Processing & ESP-NOW Tx/Rx[cite: 1] |
| **Solar Panel** | Polycrystalline 3V / 210mA (70x70mm) | Power source[cite: 1] |
| **Transistor** | NPN 2N2222 (TO-92) | Oscillator switch[cite: 1] |
| **Diode** | Schottky 1N5822 | High-frequency rectification[cite: 1] |
| **Inductor** | Bifilar wound ferrite core (15 turns) | Flyback transformer[cite: 1] |
| **Capacitor** | 5.5V - 1F Supercapacitor | Energy storage[cite: 1] |
| **IC** | TL431 Shunt Regulator | Overvoltage protection[cite: 1] |
| **Resistors** | 1kΩ, 2x 47kΩ | Base biasing & Voltage divider[cite: 1] |

## Performance Metrics
Based on laboratory experiments processing 2,286 packets, the system achieved the following metrics[cite: 1]:
*   **Maximum Compression Ratio:** 32.0x (Payload reduced from 32-bit float to 1-bit during ideal tracking)[cite: 1].
*   **Transmission Latency:** 804 µs median delay[cite: 1].
*   **Reliability:** 0.31% total packet error rate (PER) with an automated MAC-layer CRC integrity check[cite: 1].
*   **Throughput:** 14.93 Bytes per second (average), effectively freeing up the 2.4 GHz spectrum[cite: 1].

## Build & Flash Instructions
1.  Clone this repository to your local machine.
2.  Open the project using **PlatformIO** or **Arduino IDE** (ensure ESP32 board support is installed).
3.  Upload the `Tx_Node` directory code to the first ESP32-S3.
4.  Upload the `Rx_Node` directory code to the second ESP32-S3.
5.  Open the Serial Monitor (115200 baud) on the Receiver Node to view real-time decoded voltage and transmission metrics.
