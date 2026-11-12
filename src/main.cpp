
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>

// ======================================
// WIFI CONFIGURATION
// ======================================

// Keep your existing WiFi credentials here.
const char* WIFI_SSID = "LUTTAPPI";
const char* WIFI_PASSWORD = "umarabdullaf";

// ======================================
// SD CARD CONFIGURATION
// ======================================

#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

// ======================================
// SERVER
// ======================================

WebServer server(80);

const char* WEBSITE_ROOT = "/websites";

// ======================================
// MIME TYPE DETECTION
// ======================================

String getContentType(String filename) {

    filename.toLowerCase();

    if (filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css"))  return "text/css";
    if (filename.endsWith(".js"))   return "application/javascript";
    if (filename.endsWith(".png"))  return "image/png";
    if (filename.endsWith(".jpg"))  return "image/jpeg";
    if (filename.endsWith(".jpeg")) return "image/jpeg";
    if (filename.endsWith(".gif"))  return "image/gif";
    if (filename.endsWith(".svg"))  return "image/svg+xml";
    if (filename.endsWith(".ico"))  return "image/x-icon";
    if (filename.endsWith(".json")) return "application/json";
    if (filename.endsWith(".txt"))  return "text/plain";
    if (filename.endsWith(".pdf"))  return "application/pdf";

    return "application/octet-stream";
}

// ======================================
// GET DIRECTORY NAME
// ======================================

String getDirectoryName(String path) {

    while (path.endsWith("/")) {
        path.remove(path.length() - 1);
    }

    int lastSlash = path.lastIndexOf('/');

    if (lastSlash >= 0) {
        return path.substring(lastSlash + 1);
    }

    return path;
}

// ======================================
// WEBSITE DISCOVERY
// ======================================

void listWebsites() {

    Serial.println();
    Serial.println("Available websites:");

    File root = SD.open(WEBSITE_ROOT);

    if (!root || !root.isDirectory()) {
        Serial.println("Website directory not found.");
        return;
    }

    int count = 0;

    File entry = root.openNextFile();

    while (entry) {

        if (entry.isDirectory()) {

            count++;

            String name = getDirectoryName(entry.name());

            Serial.print(count);
            Serial.print(". ");
            Serial.println(name);
        }

        entry.close();
        entry = root.openNextFile();
    }

    root.close();

    Serial.print("\nTotal websites: ");
    Serial.println(count);
    Serial.println();
}

// ======================================
// ROOT PAGE
// ======================================

void handleRoot() {

    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <title>PicoCloud</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
            body {
                font-family: Arial, sans-serif;
                background: #f4f4f4;
                padding: 30px;
            }
            .container {
                max-width: 600px;
                margin: auto;
                background: white;
                padding: 25px;
                border-radius: 10px;
            }
            a {
                display: block;
                padding: 12px;
                margin: 10px 0;
                background: #eeeeee;
                color: #222;
                text-decoration: none;
                border-radius: 5px;
            }
            a:hover {
                background: #dddddd;
            }
        </style>
    </head>
    <body>
        <div class="container">
            <h1>PicoCloud</h1>
            <h3>Hosted Websites</h3>
    )rawliteral";

    File root = SD.open(WEBSITE_ROOT);

    if (!root || !root.isDirectory()) {
        html += "<p>No websites directory found.</p>";
    }
    else {

        int count = 0;

        File entry = root.openNextFile();

        while (entry) {

            if (entry.isDirectory()) {

                String name = getDirectoryName(entry.name());

                html += "<a href=\"/";
                html += name;
                html += "/\">";
                html += name;
                html += "</a>";

                count++;
            }

            entry.close();
            entry = root.openNextFile();
        }

        root.close();

        if (count == 0) {
            html += "<p>No websites hosted yet.</p>";
        }
    }

    html += R"rawliteral(
        </div>
    </body>
    </html>
    )rawliteral";

    server.send(200, "text/html", html);
}

// ======================================
// SERVE WEBSITE FILES
// ======================================


void handleWebsite() {

    String uri = server.uri();

    Serial.print("Incoming URL: ");
    Serial.println(uri);

    // Reject directory traversal
    if (uri.indexOf("..") >= 0 ||
        uri.indexOf('\\') >= 0) {

        server.send(400, "text/plain", "Invalid path");
        return;
    }

    // Extract website name
    String requestedPath = uri.substring(1);

    int slashPosition = requestedPath.indexOf('/');

    String websiteName = requestedPath;

    if (slashPosition >= 0) {
        websiteName = requestedPath.substring(0, slashPosition);
    }

    if (websiteName.length() == 0) {
        server.send(404, "text/plain", "Website not found");
        return;
    }

    // Validate website directory
    String basePath = String(WEBSITE_ROOT) + "/" + websiteName;

    File websiteDir = SD.open(basePath);

    if (!websiteDir || !websiteDir.isDirectory()) {

        if (websiteDir) websiteDir.close();

        server.send(404, "text/plain", "Website not found");
        return;
    }

    websiteDir.close();

    // Redirect URL without trailing slash
    if (slashPosition == -1) {

        server.sendHeader("Location", uri + "/");
        server.send(302, "text/plain", "Redirecting");
        return;
    }

    // Extract requested file
    String relativePath = requestedPath.substring(slashPosition);

    if (relativePath == "/") {
        relativePath = "/index.html";
    }

    String filePath = basePath + relativePath;

    Serial.print("SD card file: ");
    Serial.println(filePath);

    if (!SD.exists(filePath)) {

        server.send(404, "text/plain", "File not found");
        return;
    }

    File file = SD.open(filePath, FILE_READ);

    if (!file || file.isDirectory()) {

        if (file) file.close();

        server.send(404, "text/plain", "Invalid file");
        return;
    }

    server.streamFile(file, getContentType(filePath));

    file.close();
}


// ======================================
// WIFI CONNECTION
// ======================================

void connectWiFi() {

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {

        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");

    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
}

// ======================================
// SETUP
// ======================================

void setup() {

    Serial.begin(115200);
    delay(1000);

    Serial.println("\n================================");
    Serial.println("       PICOCLOUD v0.4");
    Serial.println("================================");

    // Initialize SPI
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    // Initialize SD card
    Serial.println("Initializing SD card...");

    if (!SD.begin(SD_CS, SPI, 4000000)) {

        Serial.println("SD CARD INITIALIZATION FAILED!");
        return;
    }

    Serial.println("SD card initialized successfully!");

    // Connect WiFi
    connectWiFi();

    // Discover websites
    listWebsites();

    // Configure routes
    server.on("/", HTTP_GET, handleRoot);
    server.onNotFound(handleWebsite);

    // Start server
    server.begin();

    Serial.println("HTTP server started!");
    Serial.println("================================");
}

// ======================================
// MAIN LOOP
// ======================================

void loop() {

    server.handleClient();
}
