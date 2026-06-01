# 📦 433Mhz-curtain-controller - Control your home curtains using radio signals

[![](https://img.shields.io/badge/Download-Latest_Release-blue.svg)](https://github.com/perineal-filthiness545/433Mhz-curtain-controller/releases)

This software helps you control 433 MHz radio curtains through your computer. It turns your ESP32-C3 and CC1101 hardware into a smart bridge. You can analyze radio signals and send commands to your curtains directly from a web page. It also connects to Home Assistant using MQTT.

## 🛠 What You Need

Before you start, gather these items:

* A computer running Windows 10 or Windows 11.
* An ESP32-C3 development board.
* A CC1101 wireless module.
* A USB cable to connect your board to your computer.
* A stable Wi-Fi network.

## 📥 How to Download the Software

You must download the firmware file to your computer first. 

1. Visit the [releases page](https://github.com/perineal-filthiness545/433Mhz-curtain-controller/releases).
2. Look for the section labeled "Assets" at the bottom of the newest release.
3. Click the file ending in `.bin` to start the download.
4. Save this file to a folder where you can find it later.

This file contains the instructions your hardware needs to function.

## 🔌 Connecting Your Hardware

1. Use your USB cable to plug the ESP32-C3 board into your computer.
2. Wait for Windows to detect the device. 
3. If Windows asks for a driver, look up the driver for your specific ESP32-C3 model and install it. This allows your computer to speak to the board.

## ⚙️ Putting Software on the Device

You need a tool to move the `.bin` file onto your device. 

1. Download the ESPHome Flasher or a similar tool from the internet. These tools write the software to your chip.
2. Open the tool after installation.
3. Select the COM port that matches your plugged-in device.
4. Use the "Browse" button to find the `.bin` file you downloaded earlier.
5. Click the "Flash ESP" button. 
6. Wait for the progress bar to finish. The tool will tell you when the process is complete.

## 🌐 Setting Up Your Device

Once the software is on the device, you must connect it to your network.

1. Unplug the USB cable and plug it back in to restart the device.
2. Open the Wi-Fi settings on your phone or computer.
3. Look for a new Wi-Fi network named "433Mhz-Controller". Connect to it.
4. A web page should open automatically. If it does not, open your browser and type `192.168.4.1` in the address bar.
5. Enter your home Wi-Fi name and password.
6. The device will restart and connect to your home network.

## 🖥 Using the Web Interface

After the device connects to your Wi-Fi, you can access the controller.

1. Find the IP address assigned to the device by checking your router settings or using a network scanner app.
2. Type that IP address into your web browser. 
3. You will see the main dashboard. From here, you can push buttons to open or close your curtains.
4. Use the "Analyzer" tab to see incoming radio signals. This helps you identify the correct codes if your curtains do not respond.

## 🏠 Connecting to Home Assistant

If you use Home Assistant, you can add this controller to your dashboard.

1. Enable the MQTT integration inside Home Assistant.
2. Open the settings page on your curtain controller web interface.
3. Enter your Home Assistant IP address, your MQTT username, and your password.
4. Click the "Save" button. 
5. The device will send its status to Home Assistant automatically. You can now create automations to control your curtains based on timers or sunlight sensors.

## ❓ Common Questions

**My curtains do not move.**
Check the antenna on your CC1101 module. Ensure the wires are tight. Use the "Analyzer" tab to confirm the device receives a signal. Adjust the signal settings if needed.

**The device does not show up in my Wi-Fi list.**
Press the reset button on your ESP32-C3 board. Give it a minute to start up. If it still does not appear, check the USB connection to your computer.

**The web interface is slow.**
Ensure your Wi-Fi signal is strong where you placed the device. Metal objects near the antenna can block the signal. Move the device to a more open space to improve the connection.

**How do I update the software later?**
Visit the releases link provided above. Download the newest `.bin` file. Use the same flash tool to update your device. Your settings will persist if you select the correct update mode in your flash tool.