# ClickerDemo Installation Guide

To upload the code onto your M5StickCP2 you will need to have the M5Stack Board manager installed in your Arduino IDE

Here is a helpful guide provided to get your Arduino IDE setup for M5's boards and get the **drivers** to have your USB ports work with Arduino IDE.
https://docs.m5stack.com/en/quick_start/m5stickc_plus2/arduino

Once you have your board mangement setup you will need to check if you have the following libraries installed
- "M5StickCPlus2.h"
- <HTTPClient.h>
- <ArduinoMqttClient.h>

Here is how you check your installed libraries or get new libraries
![image](https://github.com/elee2045sp24/demos-lac34437/assets/111517420/a329ede3-6968-4764-8241-3e0cd8f9ff3d)


Once you have these libraries you will have to set your Arduino IDE to upload to the specific port and board. ![image](https://github.com/elee2045sp24/demos-lac34437/assets/111517420/b0a31bf0-3173-4feb-bbec-3787c0d7e86f)

Enter into this drop-down menu and you will be prompted with

![image](https://github.com/elee2045sp24/demos-lac34437/assets/111517420/15223da7-27d6-4fd7-b5d5-a230f94c9e39)

Search for your board in the list or in the search bar and then select the port your M5 is on. It may be the only one discovered or it may show more than one depending on what you have plugged in.

You should now be able to compile and upload code to the M5StickCP2 by cloning into this repository and getting the folder for demo.

