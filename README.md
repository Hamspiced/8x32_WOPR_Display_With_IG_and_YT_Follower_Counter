# 🧠 8x32 WOPR Display — Instagram & YouTube Follower Counter

A DIY **scrolling LED matrix display** that mimics the classic **WOPR computer** from *WarGames* — but instead of running nuclear simulations, it shows your **Instagram** and **YouTube** follower counts in real time.

---

## 🧩 Overview

This project was inspired by the **HackerBoxes WOPR kit**, which includes an ESP32 and MAX7219-based LED matrix panels.

Original kit link:  
🔗 [HackerBox #0114 — WOPR](https://hackerboxes.com/products/hackerbox-0114-wopr)

Unfortunately, the ESP32 board included in the kit uses pins that conflict with the Wi-Fi radio (shared clock line), so Wi-Fi performance can fail or drop out.  
To solve this, I built my **own ESP32-based version** with proper pin assignments and support for fetching live data.

---

## 🛠️ Hardware Setup

### Components Used
- **(3x)** 8x32 LED Matrix Panels (MAX7219-based)  
  🔗 [AliExpress Listing](https://www.aliexpress.us/item/2251832463832355.html)
- **ESP32 Dev Kit**
- Jumper wires / breadboard

### Wiring Diagram
| Matrix Pin | ESP32 Pin | Description |
|-------------|------------|--------------|
| VCC | 3V3 | Power (5V also works, but 3.3V is safer for long runs) |
| GND | GND | Common Ground |
| DIN | D23 | Data Input |
| CS | D5 | Chip Select |
| CLK | D18 | Clock |

Make sure the **DIN/DOUT direction** between panels is consistent, and connect them in a daisy chain (OUT → IN).

---

## 💻 Software Configuration

### 1. Clone and Open
Clone this repository and open the main sketch in **Arduino IDE**.

### 2. Required Libraries
Install these from the Arduino Library Manager:
- `MD_Parola`
- `MD_MAX72xx`
- `ArduinoJson`
- `WiFi`
- `HTTPClient`

### 3. Update Credentials
At the top of the sketch, fill in your own credentials and keys:
```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* YOUTUBE_API_KEY    = "YOUR_YT_API_KEY";
const char* YOUTUBE_CHANNEL_ID = "YOUR_CHANNEL_ID";

const char* IG_ACCESS_TOKEN = "YOUR_IG_GRAPH_API_TOKEN";
const char* IG_USER_ID      = "YOUR_IG_USER_ID";
```
### 4. Configuration
You can go to the web interface of the unit from the Static IP that is set or the DHCP assigned address and adjust intensity, scroll speed, and your API keys and usernames.   From here you should be able to save and reboot the device.

### 5. Help

Sorry, i cannot provide any because this took me a while to setup as well.  The code is correct however getting the access tokens, ID's and generated keys take a while.  What i can do is give you some pointers.    

For Instagram you need to make an app, give it the proper permissions, and generate a token.  Use the Graph API explorer to generate your token and find your User ID.
```https://developers.facebook.com/tools/explorer/```
Once you get your token generated you need to convert it to an extended token here:
```https://developers.facebook.com/tools/debug/accesstoken/?access_token="Your_Token_ID"```

You can test your Instagram UserID and Access Token with the URL below just change the UserID and AccessToken below.
https://graph.instagram.com/v19.0/{USERID}?fields=followers_count&access_token={AccessToken}

Then every 60 days youll have to go to :  https://developers.facebook.com/

Go to the API Setup with Instagram Login:  generate a new token
Then go to the above URL and convert it to an extended token.

This is about as much help as i can give as this was a struggle for me.
