# MiniTV 📺

A DIY miniature display powered by an ESP8266 NodeMCU and a 1.8-inch ST7735 TFT screen. MiniTV lets you display custom photos and animated GIFs on a tiny screen using your phone — no Bluetooth, USB connection, or dedicated mobile app required.

Upload your favourite wallpapers, animations, and GIFs directly over Wi-Fi using a browser-based media studio.

![](https://github.com/shanicks/mini-tv/image.gif)

## ✨ Features

- **Photo Display:** Convert and display custom images on the TFT screen.
- **Animated GIF Playback:** Convert GIF animations into RGB565 frames and play them on the display.
- **Wireless Upload:** Transfer media directly from your phone over Wi-Fi.
- **Browser-Based Editor:** Prepare images and GIFs without installing a mobile application.
- **Image Customization:**
  - Portrait and landscape orientation
  - Crop and fit options
  - Image rotation
  - Brightness adjustment
  - Contrast adjustment
  - Saturation adjustment
  - Zoom and repositioning
  - View lock to prevent accidental changes
- **Persistent Storage:** Uploaded media is stored in the ESP8266's LittleFS filesystem and remains available after restarting the device.
- **One-Button Upload:** Convert and send media directly to MiniTV.

## 🛠️ Hardware Requirements

| Component | Description |
|---|---|
| Microcontroller | NodeMCU ESP8266 |
| Display | 1.8-inch ST7735 TFT |
| Resolution | 128 × 160 pixels |
| Storage | ESP8266 LittleFS |
| Connectivity | Wi-Fi access point |

## 🔌 Wiring

Connect the ST7735 display to the NodeMCU ESP8266 as follows:

| TFT Display | NodeMCU ESP8266 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCK / SCL | D5 |
| SDA / MOSI | D7 |
| RST | D4 |
| DC / A0 | D2 |
| CS | D8 |
| LED / BL | 3V3 (if supported) |

**Important:** Use a 3.3V-compatible display module. Check your particular TFT board's power requirements before connecting it.

## 💻 Software Requirements

- Arduino IDE
- ESP8266 board package
- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- ESP8266WiFi
- ESP8266WebServer
- LittleFS
- DNSServer

The browser-based GIF converter uses `gifuct-js`, bundled into a local JavaScript file named `gifuct.min.js`.

## 📁 Project Structure

```text
mini-tv/
│
├── minitv.ino
│
└── data/
    ├── media_studio.html
    └── gifuct.min.js
```

- `minitv.ino` — ESP8266 firmware, Wi-Fi access point, web server, file upload and TFT playback.
- `media_studio.html` — Browser-based photo and GIF editor.
- `gifuct.min.js` — Local GIF decoder used by the media editor.

## ⚙️ Installation

### 1. Install the ESP8266 Board Package

In Arduino IDE:

1. Open Boards Manager.
2. Search for ESP8266.
3. Install the ESP8266 Community board package.
4. Select your NodeMCU ESP8266 board.

### 2. Install Required Libraries

Install:

- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library

The ESP8266 networking and filesystem libraries are provided by the ESP8266 board package.

### 3. Prepare the LittleFS Data Folder

Make sure your project has this structure:

```text
mini-tv/
├── minitv.ino
└── data/
    ├── media_studio.html
    └── gifuct.min.js
```

### 4. Upload the Firmware

1. Connect the NodeMCU to your computer.
2. Select the correct ESP8266 board and COM port.
3. Upload `minitv.ino` using Arduino IDE.

### 5. Upload LittleFS Files

Use the ESP8266 LittleFS filesystem uploader to upload the contents of the `data` folder.

**Note:** Uploading LittleFS replaces the filesystem contents. Do not do this if you need to preserve previously uploaded media without backing it up.

## 📱 How to Use MiniTV

1. Power on MiniTV.
2. Open the Wi-Fi settings on your phone.
3. Connect to the Wi-Fi network:

   **SSID:** `MiniTV` or the configured firmware access-point name.

   **Password:** `minitv26` or the configured firmware password.

4. Open a browser and visit:

   `http://192.168.4.1/`

5. Open the Media Studio.
6. Select a photo or GIF.
7. Adjust the image using the available editing controls.
8. Press **Send to MiniTV**.
9. Wait for the upload to finish.
10. Your media will appear on the TFT display.

If Android opens a captive-portal sign-in window, use the browser-opening option if available, or open the MiniTV address in your regular browser.

## 🖼️ Supported Media

### Images

- JPG / JPEG
- PNG
- Other browser-supported image formats

### Animated GIFs

- Animated GIF files
- Adjustable playback frame delay
- Configurable maximum frame count
- Automatic frame conversion to RGB565

## 🎨 Image Processing

The browser performs image processing before uploading the media to the ESP8266.

The conversion pipeline:

1. Load the selected image or GIF.
2. Decode GIF frames in the browser.
3. Composite individual GIF frames, including transparency and disposal handling.
4. Apply crop, scaling, rotation and image adjustments.
5. Resize to the selected display resolution.
6. Convert pixels to RGB565.
7. Package the frames into the MiniTV media format.
8. Upload the resulting file to the ESP8266 over Wi-Fi.

This approach keeps the ESP8266's processing and memory requirements low.

## 📦 Media File Format

MiniTV uses a custom binary media format called **SGM (Smart Graphics Media)**.

The file contains:

| Field | Description |
|---|---|
| Magic | SGM1 identifier |
| Width | Display frame width |
| Height | Display frame height |
| Frame Count | Number of frames |
| Frame Delay | Playback delay in milliseconds |
| Pixel Format | RGB565 little-endian |
| Pixel Data | Image data for every frame |

Each RGB565 pixel occupies 2 bytes.

- Portrait: 128 × 160 pixels
- Landscape: 160 × 128 pixels
- One portrait frame: 40,960 bytes

## 💾 Storage

MiniTV uses the ESP8266 LittleFS filesystem to store uploaded media.

Once uploaded, media remains stored even after power is disconnected. The firmware reads the stored frames and displays them on the TFT.

Available storage depends on the selected ESP8266 flash layout and filesystem partition.

## 🔧 Troubleshooting

| Problem | Possible Solution |
|---|---|
| Display stays white or black | Check wiring, TFT initialization and display power |
| Wi-Fi network does not appear | Restart the NodeMCU and check the firmware |
| Webpage does not open | Connect to the MiniTV Wi-Fi and visit `http://192.168.4.1/` |
| File picker does not open | Try using a regular browser instead of Android's captive portal |
| GIF frames appear black | Check GIF frame compositing and transparency handling |
| Upload fails | Check Wi-Fi connection and available LittleFS space |
| Display shows incorrect colours | Check RGB565 conversion and byte order |
| Media disappears after restart | Verify that the upload completed and LittleFS storage is working |

## 🚀 Future Improvements

- Better GIF playback optimization
- More image adjustment options
- Multiple saved wallpapers
- Media selection directly on the device
- Improved upload and storage management
- Custom boot screen
- Additional display animations

## 📜 License

This project is intended for learning, experimentation and personal DIY electronics development.

Feel free to modify and improve it for your own projects. Check the licenses of third-party libraries before redistributing them.

---

**MiniTV — A tiny screen for your favourite pictures and animations.** 📺