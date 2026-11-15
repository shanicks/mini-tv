#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <DNSServer.h>

#define TFT_CS  D8
#define TFT_RST D4
#define TFT_DC  D2

const char* AP_SSID = "MiniTV";
const char* AP_PASS = "minitv26";
const byte DNS_PORT = 53;

const char* MEDIA_PATH = "/media.sgm";
const char* TEMP_PATH = "/upload.tmp";
const char* BACKUP_PATH = "/media.old";
const uint32_t SGM_MAGIC = 0x314D4753UL;

DNSServer dnsServer;
ESP8266WebServer server(80);
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

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

static_assert(sizeof(MediaHeader) == 16, "SGM header must be 16 bytes");

File uploadFile;
bool uploadFailed = false;
String uploadError;
MediaHeader media;
bool mediaReady = false;
uint16_t currentFrame = 0;
uint32_t lastFrameAt = 0;
bool fsReady = false;

bool validateMedia(File &f, MediaHeader &h) {
  if (!f || f.size() < sizeof(MediaHeader)) return false;
  if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h)) return false;
  if (h.magic != SGM_MAGIC) return false;

  if (!((h.width == 128 && h.height == 160) ||
        (h.width == 160 && h.height == 128))) return false;

  if (h.frameCount < 1 || h.frameCount > 60) return false;
  if (h.frameDelay < 40 || h.frameDelay > 2000) return false;
  if (h.pixelFormat != 1) return false;

  uint32_t bytesPerFrame =
      (uint32_t)h.width * h.height * 2UL;

  uint32_t expected =
      sizeof(MediaHeader) + bytesPerFrame * h.frameCount;

  return f.size() == expected;
}

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

bool drawFrame(uint16_t frameNumber) {
  if (!mediaReady || !fsReady ||
      frameNumber >= media.frameCount) {
    return false;
  }

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

  // Set portrait or landscape rotation.
  tft.setRotation(media.width == 128 ? 0 : 1);

  // Begin SPI transfer.
  tft.startWrite();

  // Set drawing area.
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

  // Finish SPI transfer.
  tft.endWrite();

  f.close();
  return true;
}

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

    if (LittleFS.exists(TEMP_PATH)) {
      LittleFS.remove(TEMP_PATH);
    }

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

    if (fsReady && LittleFS.exists(TEMP_PATH)) {
      LittleFS.remove(TEMP_PATH);
    }
  }
}

void handleUploadComplete() {
  if (uploadFile) uploadFile.close();

  if (uploadFailed) {
    if (fsReady && LittleFS.exists(TEMP_PATH)) {
      LittleFS.remove(TEMP_PATH);
    }

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

  // Keep previous media until the new file is installed.
  if (LittleFS.exists(BACKUP_PATH)) {
    LittleFS.remove(BACKUP_PATH);
  }

  bool hadOld = LittleFS.exists(MEDIA_PATH);

  if (hadOld && !LittleFS.rename(MEDIA_PATH, BACKUP_PATH)) {
    LittleFS.remove(TEMP_PATH);

    server.send(500, "text/plain",
                "Could not back up existing media; insufficient filesystem space?");
    return;
  }

  if (!LittleFS.rename(TEMP_PATH, MEDIA_PATH)) {
    if (hadOld) {
      LittleFS.rename(BACKUP_PATH, MEDIA_PATH);
    }

    server.send(500, "text/plain",
                "Could not install uploaded media");
    return;
  }

  if (LittleFS.exists(BACKUP_PATH)) {
    LittleFS.remove(BACKUP_PATH);
  }

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

void redirectToHome() {
  server.sendHeader(
      "Location",
      "http://192.168.4.1/",
      true
  );

  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  delay(100);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  fsReady = LittleFS.begin();

  if (!fsReady) {
    Serial.println("ERROR: LittleFS mount failed");
  } else if (loadMedia()) {
    drawFrame(0);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  delay(100);

  Serial.print("SmartGlasses AP IP: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.start(
      DNS_PORT,
      "*",
      WiFi.softAPIP()
  );

  server.on("/", HTTP_GET, []() {
    serveFile("/media_studio.html", "text/html");
  });

  server.on("/gifuct.min.js", HTTP_GET, []() {
    serveFile("/gifuct.min.js", "application/javascript");
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

  Serial.println(
      "HTTP server started. Browse to http://192.168.4.1/"
  );
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (mediaReady && media.frameCount > 1) {
    uint32_t now = millis();

    if ((uint32_t)(now - lastFrameAt) >= media.frameDelay) {
      lastFrameAt = now;

      currentFrame++;

      if (currentFrame >= media.frameCount) {
        currentFrame = 0;
      }

      drawFrame(currentFrame);
    }
  }

  yield();
}