
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ================= WIFI SETTINGS =================

const char* WIFI_SSID = "YOUR_HOME_WIFI";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ================= TFT SETTINGS =================

#define TFT_CS  D8
#define TFT_RST D4
#define TFT_DC  D2

ESP8266WebServer server(80);
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// ================= FILE SETTINGS =================

const char* MEDIA_PATH = "/media.sgm";
const char* TEMP_PATH = "/upload.tmp";
const char* BACKUP_PATH = "/media.old";

const uint32_t SGM_MAGIC = 0x314D4753UL;

// ================= MEDIA HEADER =================

#pragma pack(push, 1)
struct MediaHeader {
  uint32_t magic;
  uint16_t width;
  uint16_t height;
  uint16_t frameCount;
  uint16_t frameDelay;
  uint8_t pixelFormat;
  uint8_t reserved[3];
};
#pragma pack(pop)

static_assert(sizeof(MediaHeader) == 16,
              "SGM header must be 16 bytes");

// ================= GLOBAL VARIABLES =================

File uploadFile;

bool uploadFailed = false;
String uploadError;

MediaHeader media;

bool mediaReady = false;
bool fsReady = false;

uint16_t currentFrame = 0;
uint32_t lastFrameAt = 0;

// ================= VALIDATE MEDIA =================

bool validateMedia(File &f, MediaHeader &h) {
  if (!f || f.size() < sizeof(MediaHeader)) return false;

  if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h))
    return false;

  if (h.magic != SGM_MAGIC) return false;

  if (!((h.width == 128 && h.height == 160) ||
        (h.width == 160 && h.height == 128)))
    return false;

  if (h.frameCount < 1 || h.frameCount > 60)
    return false;

  if (h.frameDelay < 40 || h.frameDelay > 2000)
    return false;

  if (h.pixelFormat != 1) return false;

  uint32_t bytesPerFrame =
      (uint32_t)h.width * h.height * 2UL;

  uint32_t expected =
      sizeof(MediaHeader) + bytesPerFrame * h.frameCount;

  return f.size() == expected;
}

// ================= LOAD MEDIA =================

bool loadMedia() {
  mediaReady = false;

  if (!fsReady) return false;

  File f = LittleFS.open(MEDIA_PATH, "r");

  if (!f) return false;

  MediaHeader h;
  bool ok = validateMedia(f, h);

  f.close();

  if (!ok) return false;

  media = h;
  mediaReady = true;
  currentFrame = 0;

  return true;
}

// ================= DRAW FRAME =================

bool drawFrame(uint16_t frameNumber) {
  if (!mediaReady || !fsReady ||
      frameNumber >= media.frameCount)
    return false;

  File f = LittleFS.open(MEDIA_PATH, "r");

  if (!f) return false;

  uint32_t frameBytes =
      (uint32_t)media.width * media.height * 2UL;

  uint32_t offset =
      sizeof(MediaHeader) + frameBytes * frameNumber;

  if (!f.seek(offset, SeekSet)) {
    f.close();
    return false;
  }

  tft.setRotation(media.width == 128 ? 0 : 1);

  tft.startWrite();
  tft.setAddrWindow(0, 0, media.width, media.height);

  uint16_t pixels[64];

  uint32_t remaining =
      (uint32_t)media.width * media.height;

  while (remaining) {
    uint16_t count =
        remaining > 64 ? 64 : (uint16_t)remaining;

    int bytes = f.read(
        (uint8_t*)pixels,
        count * 2
    );

    if (bytes != count * 2) {
      tft.endWrite();
      f.close();
      return false;
    }

    for (uint16_t i = 0; i < count; i++) {
      tft.pushColor(pixels[i]);
    }

    remaining -= count;
    yield();
  }

  tft.endWrite();
  f.close();

  return true;
}

// ================= UPLOAD DATA =================

void handleUploadData() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadFailed = false;
    uploadError = "";

    if (!fsReady) {
      uploadFailed = true;
      uploadError = "LittleFS is not mounted";
      return;
    }

    if (uploadFile) uploadFile.close();

    if (LittleFS.exists(TEMP_PATH))
      LittleFS.remove(TEMP_PATH);

    uploadFile = LittleFS.open(TEMP_PATH, "w");

    if (!uploadFile) {
      uploadFailed = true;
      uploadError =
          "Cannot create temporary file; check free LittleFS space";
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {

    if (!uploadFailed && uploadFile) {
      size_t written =
          uploadFile.write(upload.buf, upload.currentSize);

      if (written != upload.currentSize) {
        uploadFailed = true;
        uploadError = "LittleFS is full or write failed";
        uploadFile.close();
      }
    }

  } else if (upload.status == UPLOAD_FILE_END) {

    if (uploadFile) uploadFile.close();

  } else if (upload.status == UPLOAD_FILE_ABORTED) {

    if (uploadFile) uploadFile.close();

    uploadFailed = true;
    uploadError = "Upload aborted";

    if (fsReady && LittleFS.exists(TEMP_PATH))
      LittleFS.remove(TEMP_PATH);
  }
}

// ================= UPLOAD COMPLETE =================

void handleUploadComplete() {
  if (uploadFile) uploadFile.close();

  if (uploadFailed) {
    if (fsReady && LittleFS.exists(TEMP_PATH))
      LittleFS.remove(TEMP_PATH);

    server.send(500, "text/plain", uploadError);
    return;
  }

  if (!fsReady) {
    server.send(500, "text/plain",
                "LittleFS is not mounted");
    return;
  }

  File f = LittleFS.open(TEMP_PATH, "r");

  MediaHeader candidate;
  bool valid = validateMedia(f, candidate);

  f.close();

  if (!valid) {
    LittleFS.remove(TEMP_PATH);

    server.send(400, "text/plain",
                "Invalid SGM file or incomplete upload");
    return;
  }

  if (LittleFS.exists(BACKUP_PATH))
    LittleFS.remove(BACKUP_PATH);

  bool hadOld = LittleFS.exists(MEDIA_PATH);

  if (hadOld &&
      !LittleFS.rename(MEDIA_PATH, BACKUP_PATH)) {

    LittleFS.remove(TEMP_PATH);

    server.send(500, "text/plain",
                "Could not back up existing media");
    return;
  }

  if (!LittleFS.rename(TEMP_PATH, MEDIA_PATH)) {
    if (hadOld)
      LittleFS.rename(BACKUP_PATH, MEDIA_PATH);

    server.send(500, "text/plain",
                "Could not install uploaded media");
    return;
  }

  if (LittleFS.exists(BACKUP_PATH))
    LittleFS.remove(BACKUP_PATH);

  if (!loadMedia()) {
    server.send(500, "text/plain",
                "File installed but could not be loaded");
    return;
  }

  currentFrame = 0;
  drawFrame(0);
  lastFrameAt = millis();

  server.send(200, "text/plain",
              "Media installed. It will remain stored after power-off.");
}

// ================= SERVE FILES =================

void serveFile(const char* path, const char* contentType) {
  if (!fsReady) {
    server.send(500, "text/plain",
                "LittleFS is not mounted");
    return;
  }

  File f = LittleFS.open(path, "r");

  if (!f) {
    server.send(404, "text/plain",
                String("Missing file: ") + path);
    return;
  }

  server.sendHeader("Cache-Control", "no-store");

  server.streamFile(f, contentType);
  f.close();
}

// ================= REDIRECT =================

void redirectToHome() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize TFT
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  // Initialize LittleFS
  fsReady = LittleFS.begin();

  if (!fsReady) {
    Serial.println("ERROR: LittleFS mount failed");
  } else if (loadMedia()) {
    drawFrame(0);
  }

  // Connect to home Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.println();
  Serial.print("Connecting to home Wi-Fi");

  uint32_t wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStart < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected!");

    Serial.print("MiniTV IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connection failed!");
    Serial.println("Check Wi-Fi name and password.");
  }

  // HTTP routes
  server.on("/", HTTP_GET, []() {
    serveFile("/media_studio.html", "text/html");
  });

  server.on("/gifuct.min.js", HTTP_GET, []() {
    serveFile("/gifuct.min.js",
              "application/javascript");
  });

  server.on("/generate_204", HTTP_GET, redirectToHome);
  server.on("/gen_204", HTTP_GET, redirectToHome);
  server.on("/hotspot-detect.html", HTTP_GET, redirectToHome);
  server.on("/connecttest.txt", HTTP_GET, redirectToHome);
  server.on("/ncsi.txt", HTTP_GET, redirectToHome);

  server.on(
      "/upload",
      HTTP_POST,
      handleUploadComplete,
      handleUploadData
  );

  server.onNotFound(redirectToHome);

  server.begin();

  Serial.println("HTTP server started.");
}

// ================= MAIN LOOP =================

void loop() {
  server.handleClient();

  if (mediaReady && media.frameCount > 1) {
    uint32_t now = millis();

    if ((uint32_t)(now - lastFrameAt) >= media.frameDelay) {
      lastFrameAt = now;

      currentFrame++;

      if (currentFrame >= media.frameCount)
        currentFrame = 0;

      drawFrame(currentFrame);
    }
  }

  yield();
}
