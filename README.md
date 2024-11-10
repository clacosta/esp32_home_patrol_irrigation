# Smart Irrigation System with ESP32 and Solenoid Control

## Description
This project implements a smart irrigation system using an ESP32 board. It monitors soil moisture and controls **a solenoid valve** to regulate water flow, allowing configuration via a Bluetooth app. Data is stored in Firebase Realtime Database for remote monitoring and analysis.

## Features
* **Soil moisture monitoring:** Uses a capacitive sensor to measure soil moisture levels.
* **Solenoid control:** Activates a solenoid valve to open or close the water flow, based on moisture levels and user-defined settings.
* **Bluetooth configuration:** Allows users to configure the system through a mobile app.
* **Firebase integration:** Stores data in Firebase Realtime Database for remote monitoring and analysis.

## TODO
* **Bluetooth configuration:** Implement the Bluetooth interface to allow remote system configuration.
