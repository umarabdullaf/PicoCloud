#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <SuperDMZ.h>

// ============================================================
// PICOCloud v0.8
// Public website hosting + cookie-based login + storage manager + SuperDMZ
// ============================================================

// ------------------------- Wi-Fi -----------------------------
// Keep the Wi-Fi credentials that already work in your v0.5 code.
const char* WIFI_SSID = "LUTTAPPI";
const char* WIFI_PASSWORD = "umarabdullaf";

// Paste the 48-character token from your SuperDMZ tunnel dashboard here.
// Do not share this token publicly.
const char* SUPERDMZ_TOKEN = "0e35cd9461fd9b2299600df13e3631894c4779cb0f779f53";

// ---------------------- Authentication -----------------------
// Login credentials for the PicoCloud login page.
const char* AUTH_USERNAME = "jensen";
const char* AUTH_PASSWORD = "nvidia";

// ---------------------- microSD / SPI ------------------------
#define SD_CS_PIN   5
#define SD_SCK_PIN  18
#define SD_MISO_PIN 19
#define SD_MOSI_PIN 23
#define SD_FREQUENCY 4000000

// ---------------------- Server settings ----------------------
static const uint16_t HTTP_PORT = 80;
static const char* WEBSITE_ROOT = "/websites";
static const char* STORAGE_ROOT = "/storage";
static const char* SESSION_COOKIE = "pico_session=";
static const uint32_t SESSION_LIFETIME_MS = 12UL * 60UL * 60UL * 1000UL;
static const size_t MAX_ZIP_ENTRIES = 120;
static const uint8_t MAX_DELETE_DEPTH = 16;

WebServer server(HTTP_PORT);
SuperDMZ tunnel;
std::vector<String> publicWebsites;
bool sdReady = false;
bool tunnelStartAttempted = false;
String activeSessionToken;
unsigned long sessionCreatedAt = 0;

// Upload state. WebServer calls the upload callback for each multipart
// file; this state is reset whenever a new file starts.
File uploadFile;
bool uploadAuthorized = false;
bool uploadCurrentFileAccepted = false;
bool uploadAnyFileSucceeded = false;
bool uploadRequestStarted = false;
String uploadCurrentTarget;
String uploadTargetDirectory;
String uploadError;

// Website-folder upload state (separate from personal-storage uploads).
File websiteUploadFile;
bool websiteUploadAuthorized = false;
bool websiteUploadAnyFileSucceeded = false;
bool websiteUploadCurrentFileAccepted = false;
String websiteUploadCurrentTarget;
String websiteUploadSiteName;
String websiteUploadError;

// ============================================================
// SMALL UTILITIES
// ============================================================

String joinPath(const String& parent, const String& child) {
  if (parent.endsWith("/")) return parent + child;
  return parent + "/" + child;
}

String lastPathSegment(const String& path) {
  int slash = path.lastIndexOf('/');
  return (slash >= 0) ? path.substring(slash + 1) : path;
}

String fileEntryName(File& entry) {
  // ESP32 FS File::name() can return a path; use only its final segment.
  String name = String(entry.name());
  name.replace("\\", "/");
  return lastPathSegment(name);
}

bool isSafeSegment(const String& segment) {
  if (segment.length() == 0 || segment == "." || segment == "..") return false;

  for (size_t i = 0; i < segment.length(); i++) {
    uint8_t c = static_cast<uint8_t>(segment[i]);
    if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' ||
        c == '%' || c == '?' || c == '#' || c == '"' || c == '<' ||
        c == '>' || c == '|') {
      return false;
    }
  }
  return true;
}

// Converts a user-supplied relative path into a normalized relative path.
// Empty input is allowed and means the storage root.
bool normalizeRelativePath(String input, String& normalized) {
  normalized = "";
  input.replace("\\", "/");

  while (input.startsWith("/")) input.remove(0, 1);
  while (input.endsWith("/")) input.remove(input.length() - 1, 1);

  if (input.length() == 0) return true;

  int start = 0;
  while (start < input.length()) {
    int slash = input.indexOf('/', start);
    String segment = (slash < 0) ? input.substring(start) : input.substring(start, slash);

    if (!isSafeSegment(segment)) return false;

    if (normalized.length() > 0) normalized += "/";
    normalized += segment;

    if (slash < 0) break;
    start = slash + 1;
    if (start == input.length()) return false;
  }
  return true;
}

bool makeStoragePath(const String& relativePath, String& absolutePath,
                     bool allowStorageRoot = true) {
  String normalized;
  if (!normalizeRelativePath(relativePath, normalized)) return false;
  if (!allowStorageRoot && normalized.length() == 0) return false;

  absolutePath = String(STORAGE_ROOT);
  if (normalized.length() > 0) absolutePath += "/" + normalized;
  return true;
}

String jsonEscape(const String& value) {
  String result;
  result.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) < 32) result += ' ';
        else result += c;
    }
  }
  return result;
}

String htmlEscape(const String& value) {
  String result;
  result.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    switch (value[i]) {
      case '&': result += "&amp;"; break;
      case '<': result += "&lt;"; break;
      case '>': result += "&gt;"; break;
      case '"': result += "&quot;"; break;
      case '\'': result += "&#39;"; break;
      default: result += value[i];
    }
  }
  return result;
}

String getContentType(String filename) {
  filename.toLowerCase();
  if (filename.endsWith(".html") || filename.endsWith(".htm")) return "text/html; charset=utf-8";
  if (filename.endsWith(".css")) return "text/css; charset=utf-8";
  if (filename.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (filename.endsWith(".json")) return "application/json; charset=utf-8";
  if (filename.endsWith(".txt")) return "text/plain; charset=utf-8";
  if (filename.endsWith(".xml")) return "application/xml";
  if (filename.endsWith(".svg")) return "image/svg+xml";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  if (filename.endsWith(".gif")) return "image/gif";
  if (filename.endsWith(".webp")) return "image/webp";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".pdf")) return "application/pdf";
  if (filename.endsWith(".mp4")) return "video/mp4";
  if (filename.endsWith(".webm")) return "video/webm";
  if (filename.endsWith(".mp3")) return "audio/mpeg";
  if (filename.endsWith(".wav")) return "audio/wav";
  if (filename.endsWith(".zip")) return "application/zip";
  return "application/octet-stream";
}

String makeSessionToken() {
  char token[33];
  for (uint8_t i = 0; i < 4; ++i) {
    uint32_t value = esp_random();
    snprintf(token + i * 8, 9, "%08lx", static_cast<unsigned long>(value));
  }
  token[32] = '\0';
  return String(token);
}

bool hasValidSession() {
  if (activeSessionToken.length() != 32) return false;
  if (millis() - sessionCreatedAt > SESSION_LIFETIME_MS) {
    activeSessionToken = "";
    return false;
  }
  String cookie = server.header("Cookie");
  int start = cookie.indexOf(SESSION_COOKIE);
  if (start < 0) return false;
  start += strlen(SESSION_COOKIE);
  int end = cookie.indexOf(';', start);
  String supplied = end < 0 ? cookie.substring(start) : cookie.substring(start, end);
  supplied.trim();
  return supplied.length() == activeSessionToken.length() && supplied == activeSessionToken;
}

bool requireAuthentication(bool jsonResponse = false) {
  if (hasValidSession()) return true;
  if (jsonResponse) {
    server.send(401, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Please log in again\"}");
  } else {
    server.sendHeader("Location", "/login", true);
    server.send(302, "text/plain; charset=utf-8", "Login required");
  }
  return false;
}

const char LOGIN_HTML[] PROGMEM = R"LOGIN(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PicoCloud Login</title>
<style>*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#f3f5f9;font-family:Arial,sans-serif;color:#182230}.card{width:min(390px,calc(100% - 32px));background:white;padding:30px;border:1px solid #e5e9ef;border-radius:16px;box-shadow:0 8px 30px #15223812}h1{margin:0 0 8px}.muted{color:#667085;font-size:14px;margin:0 0 24px}label{display:block;font-size:13px;font-weight:600;margin:14px 0 6px}input{width:100%;padding:12px;border:1px solid #ccd4df;border-radius:8px;font:inherit}button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:8px;background:#1f5eff;color:white;font-weight:650;font-size:15px;cursor:pointer}#error{color:#bd2525;font-size:13px;min-height:18px;margin-top:12px}</style></head><body><main class="card"><h1>PicoCloud</h1><p class="muted">Sign in to your private dashboard</p><form method="post" action="/login"><label for="username">Username</label><input id="username" name="username" autocomplete="username" required><label for="password">Password</label><input id="password" name="password" type="password" autocomplete="current-password" required><button type="submit">Sign in</button></form><div id="error"></div></main><script>if(new URLSearchParams(location.search).has("error"))document.getElementById("error").textContent="Incorrect username or password";</script></body></html>
)LOGIN";

void handleLoginPage() {
  if (hasValidSession()) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", "Redirecting"); return; }
  server.send_P(200, PSTR("text/html; charset=utf-8"), LOGIN_HTML);
  if (server.hasArg("error")) {
    // Login page remains static; query parameter is handled by the small script below.
  }
}

void handleLoginSubmit() {
  if (server.arg("username") == AUTH_USERNAME && server.arg("password") == AUTH_PASSWORD) {
    activeSessionToken = makeSessionToken();
    sessionCreatedAt = millis();
    server.sendHeader("Set-Cookie", String(SESSION_COOKIE) + activeSessionToken + "; Path=/; HttpOnly; SameSite=Lax");
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain; charset=utf-8", "Signed in");
  } else {
    server.sendHeader("Location", "/login?error=1", true);
    server.send(303, "text/plain; charset=utf-8", "Invalid credentials");
  }
}

void handleLogout() {
  activeSessionToken = "";
  sessionCreatedAt = 0;
  server.sendHeader("Set-Cookie", "pico_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  server.sendHeader("Location", "/login", true);
  server.send(303, "text/plain; charset=utf-8", "Signed out");
}

void sendJsonError(int statusCode, const String& message) {
  String body = "{\"ok\":false,\"error\":\"" + jsonEscape(message) + "\"}";
  server.send(statusCode, "application/json; charset=utf-8", body);
}


// Send larger responses in small pieces. SuperDMZ must be serviced while
// WebServer is writing, otherwise its WebSocket bridge can stop draining.
void sendStringInChunks(const String& content, const char* contentType) {
  server.setContentLength(content.length());
  server.send(200, contentType, "");
  const size_t chunkSize = 1024;
  for (size_t offset = 0; offset < content.length(); offset += chunkSize) {
    size_t count = content.length() - offset;
    if (count > chunkSize) count = chunkSize;
    String chunk = content.substring(offset, offset + count);
    server.sendContent(chunk);
    tunnel.loop();
    yield();
  }
}

void sendProgmemInChunks(const char* content, size_t contentLength,
                         const char* contentType) {
  server.setContentLength(contentLength);
  server.send(200, contentType, "");
  char chunk[1025];
  size_t offset = 0;
  while (offset < contentLength) {
    size_t count = contentLength - offset;
    if (count > 1024) count = 1024;
    for (size_t i = 0; i < count; ++i) {
      chunk[i] = static_cast<char>(pgm_read_byte(content + offset + i));
    }
    chunk[count] = '\0';
    server.sendContent(String(chunk));
    offset += count;
    tunnel.loop();
    yield();
  }
}

// Stream an SD file in bounded chunks so the reverse tunnel remains active.
void streamFileThroughTunnel(File& file, const String& filename) {
  const size_t chunkSize = 1024;
  const size_t totalSize = file.size();
  server.setContentLength(totalSize);
  server.send(200, getContentType(filename), "");
  uint8_t buffer[chunkSize];
  while (file.available()) {
    size_t count = file.read(buffer, sizeof(buffer));
    if (count == 0) break;
    server.sendContent(reinterpret_cast<const char*>(buffer), count);
    tunnel.loop();
    yield();
  }
}

// ============================================================
// WEBSITE DISCOVERY AND PUBLIC WEBSITE HOSTING
// ============================================================

bool isReservedTopLevel(const String& name) {
  return name == "storage" || name == "api" || name == "config";
}

bool isPublicWebsite(const String& name) {
  for (const String& site : publicWebsites) {
    if (site == name) return true;
  }
  return false;
}

void discoverWebsites() {
  publicWebsites.clear();
  File directory = SD.open(WEBSITE_ROOT);
  if (!directory || !directory.isDirectory()) {
    Serial.println("[WEB] /websites directory is missing or invalid.");
    if (directory) directory.close();
    return;
  }

  while (true) {
    File entry = directory.openNextFile();
    if (!entry) break;

    String name = fileEntryName(entry);
    bool isDirectory = entry.isDirectory();
    entry.close();

    String indexPath = String(WEBSITE_ROOT) + "/" + name + "/index.html";
    if (isDirectory && isSafeSegment(name) && !isReservedTopLevel(name) &&
        SD.exists(indexPath)) {
      publicWebsites.push_back(name);
      Serial.print("[WEB] Public website discovered: /");
      Serial.println(name);
    }
  }
  directory.close();
}

void handleHomePage() {
  if (!requireAuthentication()) return;

  String html;
  html.reserve(3600);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>PicoCloud</title><style>body{font-family:Arial,sans-serif;background:#f4f6f8;color:#17202a;margin:0;padding:30px}");
  html += F("main{max-width:760px;margin:auto;background:#fff;padding:28px;border-radius:14px;box-shadow:0 4px 20px #0001}");
  html += F("h1{margin-top:0}.item{display:block;padding:14px;margin:10px 0;background:#f5f7fa;border-radius:9px;color:#135bb5;text-decoration:none}");
  html += F(".muted{color:#667085;font-size:14px}.section-head{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}");
  html += F(".add{border:0;border-radius:50%;width:44px;height:44px;background:#1f5eff;color:#fff;font-size:28px;line-height:1;cursor:pointer;box-shadow:0 3px 10px #1f5eff40}");
  html += F(".add:active{transform:scale(.96)}#websiteStatus{font-size:13px;color:#667085;margin:10px 0;white-space:pre-wrap}.hidden{display:none}</style></head><body><main><h1>PicoCloud</h1>");
  html += F("<p class='muted'>Private dashboard</p><a class='item' href='/storage/'>Open personal storage</a><a class='item' href='/logout' onclick=\"event.preventDefault();fetch('/logout',{method:'POST'}).then(()=>location.href='/login')\">Log out</a>");  html += F("<div class='section-head'><h2>Hosted websites</h2><button class='add' id='addWebsite' type='button' aria-label='Upload new website folder' title='Upload new website folder'>+</button></div>");
  html += F("<input id='websiteFolder' class='hidden' type='file' webkitdirectory directory multiple>");
  html += F("<p class='muted'>Select a website folder containing index.html and its supporting files. The folder name becomes its website address.</p>");
  html += F("<div id='websiteStatus' role='status'></div>");

  if (publicWebsites.empty()) {
    html += F("<p class='muted'>No website folders found in /websites.</p>");
  } else {
    for (const String& site : publicWebsites) {
      html += "<a class='item' href='/" + htmlEscape(site) + "/'>" + htmlEscape(site) + "</a>";
    }
  }

  html += F(R"PICOJS(
<script>
const addButton=document.getElementById('addWebsite');
const folderInput=document.getElementById('websiteFolder');
const statusBox=document.getElementById('websiteStatus');
addButton.addEventListener('click',()=>folderInput.click());
folderInput.addEventListener('change',async()=>{
  const files=Array.from(folderInput.files||[]);
  if(!files.length)return;
  const firstPath=files[0].webkitRelativePath||files[0].name;
  const siteName=firstPath.split('/')[0];
  if(!siteName){statusBox.textContent='Could not determine the selected folder name.';return;}
  const hasIndex=files.some(f=>{
    const p=(f.webkitRelativePath||f.name).split('/');
    return p.length===2 && p[1].toLowerCase()==='index.html';
  });
  if(!hasIndex){statusBox.textContent='Upload cancelled: the selected folder must contain index.html directly inside it.';folderInput.value='';return;}
  if(!confirm('Upload '+files.length+' file(s) from "'+siteName+'"?')){folderInput.value='';return;}
  const form=new FormData();
  for(const file of files) form.append('websiteFiles',file,file.webkitRelativePath||file.name);
  addButton.disabled=true;
  statusBox.textContent='Uploading '+files.length+' file(s)… Keep this page open.';
  try{
    const response=await fetch('/api/websites/upload',{method:'POST',body:form});
    const raw=await response.text();let result;try{result=JSON.parse(raw);}catch(_){throw new Error(response.status===401?'Session expired. Please log in again.':'Unexpected server response ('+response.status+').');}
    if(!response.ok||!result.ok)throw new Error(result.error||'Website upload failed');
    statusBox.textContent='Website uploaded successfully. Refreshing dashboard…';
    location.reload();
  }catch(error){
    statusBox.textContent='Upload failed: '+error.message;
    addButton.disabled=false;
  }
  folderInput.value='';
});
</script>
</main></body></html>
)PICOJS");
  sendStringInChunks(html, "text/html; charset=utf-8");
}

bool servePublicWebsite(String requestPath) {
  // requestPath is already URL-decoded and starts with '/'.
  if (!requestPath.startsWith("/")) return false;
  requestPath.remove(0, 1);

  int slash = requestPath.indexOf('/');
  String siteName = (slash < 0) ? requestPath : requestPath.substring(0, slash);
  if (!isPublicWebsite(siteName)) return false;

  String relative = (slash < 0) ? "" : requestPath.substring(slash + 1);
  String normalized;
  if (!normalizeRelativePath(relative, normalized)) {
    server.send(404, "text/plain", "Not found");
    return true;
  }

  String diskPath = String(WEBSITE_ROOT) + "/" + siteName;
  if (normalized.length() > 0) diskPath += "/" + normalized;

  File file = SD.open(diskPath, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "Website resource not found");
    return true;
  }

  if (file.isDirectory()) {
    file.close();
    if (!diskPath.endsWith("/")) diskPath += "/";
    diskPath += "index.html";
    file = SD.open(diskPath, FILE_READ);
  }

  if (!file || file.isDirectory()) {
    if (file) file.close();
    server.send(404, "text/plain", "Website page not found");
    return true;
  }

  streamFileThroughTunnel(file, diskPath);
  file.close();
  return true;
}

// ============================================================
// STORAGE WEB APPLICATION
// ============================================================

const char STORAGE_HTML[] PROGMEM = R"PICOHTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PicoCloud Storage</title>
<style>
:root{font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#182230;background:#f5f7fa}
*{box-sizing:border-box}body{margin:0}.topbar{background:#fff;border-bottom:1px solid #e5e9ef;padding:17px 22px;display:flex;align-items:center;justify-content:space-between;gap:12px}
.brand{font-weight:750;font-size:20px;letter-spacing:-.4px}.brand span{color:#64748b;font-weight:500}.wrap{max-width:980px;margin:30px auto;padding:0 18px}
.panel{background:#fff;border:1px solid #e5e9ef;border-radius:15px;box-shadow:0 4px 18px #15223808;overflow:visible}
.toolbar{padding:18px 20px;border-bottom:1px solid #edf0f4;display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}
.breadcrumbs{display:flex;gap:7px;align-items:center;flex-wrap:wrap;font-size:14px;color:#64748b}.crumb{border:0;background:none;padding:4px 0;color:#2563eb;cursor:pointer;font:inherit}.crumb.current{color:#182230;font-weight:650;cursor:default}.slash{color:#c0c7d1}
.controls{display:flex;gap:9px;align-items:center}.btn{border:1px solid #d6dde6;background:#fff;color:#243244;padding:9px 13px;border-radius:9px;cursor:pointer;font:inherit;font-size:14px}.btn.primary{background:#1f5eff;color:#fff;border-color:#1f5eff}.btn:hover{filter:brightness(.97)}
.summary{padding:13px 20px;color:#738096;font-size:13px;border-bottom:1px solid #edf0f4}.file-list{padding:4px 10px 12px}.row{display:flex;align-items:center;gap:13px;min-height:62px;padding:9px 10px;border-bottom:1px solid #f0f2f5;position:relative}.row:last-child{border-bottom:0}.file-icon{width:39px;height:39px;border-radius:10px;background:#f0f4fa;display:grid;place-items:center;font-size:20px;flex:none}.file-main{min-width:0;flex:1}.file-name{display:block;border:0;background:none;padding:0;color:#192536;text-align:left;font:inherit;font-weight:600;cursor:pointer;max-width:100%;overflow-wrap:anywhere}.file-name:hover{color:#1f5eff}.file-meta{font-size:12px;color:#8993a3;margin-top:4px}.actions{position:relative}.dots{border:0;background:transparent;border-radius:8px;width:36px;height:36px;font-size:23px;line-height:1;cursor:pointer;color:#596579}.dots:hover{background:#f1f4f8}.menu{position:absolute;right:0;top:38px;z-index:5;background:#fff;border:1px solid #dfe4eb;box-shadow:0 8px 24px #14203320;border-radius:10px;min-width:155px;padding:5px}.menu button{display:block;width:100%;border:0;background:#fff;text-align:left;padding:10px;border-radius:7px;font:inherit;font-size:13px;cursor:pointer;color:#253246}.menu button:hover{background:#f2f5f9}.menu button.danger{color:#c62828}.hidden{display:none!important}.empty,.message{padding:38px 18px;text-align:center;color:#7b8797;font-size:14px}.message.error{color:#bd2525}.upload-input{display:none}.footer{font-size:12px;color:#8792a3;text-align:center;margin:18px 0 30px}
@media(max-width:560px){.topbar{padding:14px 16px}.wrap{margin:18px auto;padding:0 10px}.toolbar{padding:15px}.row{gap:10px;padding:9px 6px}.controls .btn{padding:8px 10px}.file-meta{font-size:11px}}
</style>
</head>
<body>
<header class="topbar"><div class="brand">PicoCloud <span>/ Storage</span></div><div><button class="btn" onclick="location.href='/'">Dashboard</button> <button class="btn" onclick="fetch('/logout',{method:'POST'}).then(()=>location.href='/login')">Log out</button></div></header>
<main class="wrap">
<section class="panel">
 <div class="toolbar"><nav id="breadcrumbs" class="breadcrumbs"></nav><div class="controls"><input id="filePicker" class="upload-input" type="file" multiple><button class="btn" id="newFolderButton">＋ New folder</button><button class="btn primary" id="uploadButton">＋ Upload</button></div></div>
 <div id="summary" class="summary">Loading files…</div>
 <div id="fileList" class="file-list"><div class="empty">Loading storage…</div></div>
</section>
<div class="footer">Files are stored on the PicoCloud microSD card.</div>
</main>
<script>
let currentPath = readPathFromURL();
let entries = [];
const listEl = document.getElementById('fileList');
const summaryEl = document.getElementById('summary');
const crumbsEl = document.getElementById('breadcrumbs');
const picker = document.getElementById('filePicker');

function readPathFromURL(){
  const raw = location.pathname.replace(/^\/storage\/?/, '');
  return raw.split('/').filter(Boolean).map(x=>decodeURIComponent(x)).join('/');
}
function storageURL(path){
  if(!path) return '/storage/';
  return '/storage/' + path.split('/').map(encodeURIComponent).join('/') + '/';
}
function apiURL(endpoint,path){return endpoint+'?path='+encodeURIComponent(path||'');}
async function readApiResponse(response){const text=await response.text();let data;try{data=JSON.parse(text);}catch(_){throw new Error(response.status===401?'Session expired. Please log in again.':'Server returned an unexpected response ('+response.status+').');}if(!response.ok||!data.ok)throw new Error(data.error||'Request failed');return data;}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function humanSize(n){
  if(n<1024)return n+' B'; if(n<1024*1024)return (n/1024).toFixed(1)+' KB';
  if(n<1024*1024*1024)return (n/1024/1024).toFixed(1)+' MB'; return (n/1024/1024/1024).toFixed(2)+' GB';
}
function iconFor(item){
  if(item.type==='dir')return '📁';
  const ext=item.name.split('.').pop().toLowerCase();
  if(['jpg','jpeg','png','gif','webp','svg'].includes(ext))return '🖼️';
  if(['mp4','mkv','mov','webm'].includes(ext))return '🎬';
  if(['mp3','wav','ogg','m4a'].includes(ext))return '🎵';
  if(['pdf'].includes(ext))return '📕';
  if(['zip','rar','7z','gz'].includes(ext))return '🗜️';
  if(['doc','docx','odt','txt','rtf'].includes(ext))return '📄';
  return '📦';
}
function renderBreadcrumbs(){
  crumbsEl.innerHTML='';
  const root=document.createElement('button'); root.className='crumb'+(currentPath?'':' current'); root.textContent='Storage'; root.onclick=()=>navigate(''); crumbsEl.appendChild(root);
  const parts=currentPath?currentPath.split('/'):[]; let path='';
  parts.forEach((part,index)=>{
    const sep=document.createElement('span');sep.className='slash';sep.textContent='/';crumbsEl.appendChild(sep);
    path=path?path+'/'+part:part;
    const b=document.createElement('button');b.className='crumb'+(index===parts.length-1?' current':'');b.textContent=part;
    if(index!==parts.length-1)b.onclick=()=>navigate(path);
    crumbsEl.appendChild(b);
  });
}
function render(){
  renderBreadcrumbs();
  const folders=entries.filter(x=>x.type==='dir').length;
  const files=entries.length-folders;
  summaryEl.textContent=folders+' folders · '+files+' files';
  if(!entries.length){listEl.innerHTML='<div class="empty">This folder is empty.</div>';return;}
  listEl.innerHTML=entries.map((item,index)=>{
    const isDir=item.type==='dir';
    return `<div class="row" data-index="${index}">
      <div class="file-icon">${iconFor(item)}</div>
      <div class="file-main"><button class="file-name" data-action="open">${esc(item.name)}${isDir?'/':''}</button>
      <div class="file-meta">${isDir?'Folder':humanSize(Number(item.size)||0)}</div></div>
      <div class="actions"><button class="dots" aria-label="More options" data-action="menu">⋮</button>
      <div class="menu hidden"><button data-action="download">${isDir?'Download ZIP':'Download'}</button><button class="danger" data-action="delete">Delete</button></div></div>
    </div>`;
  }).join('');
}
async function loadDirectory(){
  listEl.innerHTML='<div class="empty">Loading files…</div>';
  try{
    const response=await fetch(apiURL('/api/storage/list',currentPath),{cache:'no-store'});
    const data=await readApiResponse(response);
    entries=data.entries||[];
    entries.sort((a,b)=>a.type===b.type?a.name.localeCompare(b.name):(a.type==='dir'?-1:1));
    render();
  }catch(error){listEl.innerHTML='<div class="message error">'+esc(error.message)+'</div>';summaryEl.textContent='Unable to read storage';}
}
function navigate(path){currentPath=path;history.pushState({},'',storageURL(path));loadDirectory();}
window.addEventListener('popstate',()=>{currentPath=readPathFromURL();loadDirectory();});
function hideMenus(){document.querySelectorAll('.menu').forEach(m=>m.classList.add('hidden'));}
listEl.addEventListener('click',async event=>{
  const actionButton=event.target.closest('[data-action]');
  if(!actionButton)return;
  const row=actionButton.closest('.row'); const item=entries[Number(row.dataset.index)];
  const action=actionButton.dataset.action;
  if(action==='menu'){
    const menu=row.querySelector('.menu'); const wasOpen=!menu.classList.contains('hidden');hideMenus();if(!wasOpen)menu.classList.remove('hidden');return;
  }
  hideMenus();
  const itemPath=currentPath?currentPath+'/'+item.name:item.name;
  if(action==='open'){
    if(item.type==='dir')navigate(itemPath);
    else location.href=apiURL('/api/storage/download',itemPath);
  }else if(action==='download'){
    location.href=apiURL(item.type==='dir'?'/api/storage/zip':'/api/storage/download',itemPath);
  }else if(action==='delete'){
    const promptText=item.type==='dir'?'Delete this folder and everything inside it?':'Delete this file permanently?';
    if(!confirm(promptText+'\n\n'+item.name))return;
    try{
      const response=await fetch(apiURL('/api/storage/delete',itemPath),{method:'POST'});
      const data=await readApiResponse(response);
      await loadDirectory();
    }catch(error){alert(error.message);}
  }
});
document.addEventListener('click',event=>{if(!event.target.closest('.actions'))hideMenus();});
document.getElementById('newFolderButton').addEventListener('click',async()=>{
  const name=prompt('New folder name:');if(name===null)return;
  const clean=name.trim();if(!clean)return;
  const form=new URLSearchParams();form.set('path',currentPath);form.set('name',clean);
  try{const response=await fetch('/api/storage/mkdir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form.toString()});await readApiResponse(response);await loadDirectory();}
  catch(error){alert(error.message);}
});
document.getElementById('uploadButton').addEventListener('click',()=>picker.click());
const CHUNK = 32 * 1024;
picker.addEventListener('change', async () => {
  if (!picker.files.length) return;
  const files = Array.from(picker.files);
  try {
    for (let i = 0; i < files.length; i++) {
      const f = files[i];
      let offset = 0;
      do {
        const blob = f.slice(offset, offset + CHUNK);
        const form = new FormData();
        form.append('chunk', blob, 'chunk');
        const url = '/api/storage/upload?path=' + encodeURIComponent(currentPath) +
                    '&name=' + encodeURIComponent(f.name) + '&offset=' + offset;
        await readApiResponse(await fetch(url, { method: 'POST', body: form }));
        offset += blob.size;
        summaryEl.textContent = 'Uploading ' + f.name + ' (' + (i + 1) + '/' + files.length + ') – ' +
                                Math.round(offset * 100 / Math.max(f.size, 1)) + '%';
      } while (offset < f.size);
    }
    summaryEl.textContent = 'Upload complete';
  } catch (error) {
    summaryEl.textContent = 'Upload failed';
    alert(error.message);
  }
  picker.value = '';
  await loadDirectory();
});
loadDirectory();
</script>
</body>
</html>
)PICOHTML";

void handleStoragePage() {
  if (!requireAuthentication()) return;
  sendProgmemInChunks(STORAGE_HTML, sizeof(STORAGE_HTML) - 1,
                      "text/html; charset=utf-8");
}

void handleStorageRedirect() {
  if (!requireAuthentication()) return;
  server.sendHeader("Location", "/storage/", true);
  server.send(302, "text/plain", "Redirecting to storage");
}

// ============================================================
// STORAGE LISTING / DOWNLOAD / UPLOAD / DELETE
// ============================================================

void handleStorageList() {
  if (!requireAuthentication(true)) return;

  String absolutePath;
  if (!makeStoragePath(server.arg("path"), absolutePath, true)) {
    sendJsonError(400, "Invalid storage path");
    return;
  }

  File directory = SD.open(absolutePath, FILE_READ);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    sendJsonError(404, "Folder not found");
    return;
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  server.sendContent("{\"ok\":true,\"entries\":[");

  bool first = true;
  while (true) {
    File entry = directory.openNextFile();
    if (!entry) break;

    String name = fileEntryName(entry);
    bool isDirectory = entry.isDirectory();
    uint32_t fileSize = isDirectory ? 0 : static_cast<uint32_t>(entry.size());
    entry.close();

    if (!isSafeSegment(name)) continue;

    if (!first) server.sendContent(",");
    first = false;

    String item = "{\"name\":\"" + jsonEscape(name) + "\",\"type\":\"";
    item += isDirectory ? "dir" : "file";
    item += "\",\"size\":" + String(fileSize) + "}";
    server.sendContent(item);
    tunnel.loop();
    delay(0);
  }

  directory.close();
  server.sendContent("]}");
}

void handleStorageDownload() {
  if (!requireAuthentication(true)) return;

  String absolutePath;
  if (!makeStoragePath(server.arg("path"), absolutePath, false)) {
    sendJsonError(400, "Invalid file path");
    return;
  }

  File file = SD.open(absolutePath, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    sendJsonError(404, "File not found");
    return;
  }

  String filename = lastPathSegment(absolutePath);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  streamFileThroughTunnel(file, filename);
  file.close();
}

String extractUploadFilename(String filename) {
  filename.replace("\\", "/");
  filename = lastPathSegment(filename);
  return filename;
}

void handleStorageUploadData() {
  HTTPUpload& upload = server.upload();

  // Log only the start and end of each upload. Printing on every block at
  // 115200 baud slows uploads and starves the tunnel.
  if (upload.status == UPLOAD_FILE_START || upload.status == UPLOAD_FILE_END) {
    Serial.printf("[UPLOAD] Status: %d | URI: %s\n",
                  upload.status, server.uri().c_str());
  }

  if (upload.status == UPLOAD_FILE_START) {
    if (!uploadRequestStarted) {
      uploadRequestStarted = true;
      uploadError = "";
      uploadAnyFileSucceeded = false;
    }

    uploadAuthorized = hasValidSession();
    uploadCurrentFileAccepted = false;
    uploadCurrentTarget = "";

    if (uploadFile) uploadFile.close();

    if (!uploadAuthorized) return;

    String relativeDirectory;
    if (!normalizeRelativePath(server.arg("path"), relativeDirectory)) {
      if (uploadError.length() == 0) uploadError = "Invalid destination folder";
      return;
    }

    uploadTargetDirectory = String(STORAGE_ROOT);
    if (relativeDirectory.length() > 0) uploadTargetDirectory += "/" + relativeDirectory;

    File destination = SD.open(uploadTargetDirectory, FILE_READ);
    bool destinationIsDirectory = destination && destination.isDirectory();
    if (destination) destination.close();

    if (!destinationIsDirectory) {
      if (uploadError.length() == 0) uploadError = "Destination folder does not exist";
      return;
    }

    String requestedName = server.hasArg("name") ? server.arg("name") : String(upload.filename);
    String safeFilename = extractUploadFilename(requestedName);
    if (!isSafeSegment(safeFilename)) {
      if (uploadError.length() == 0) uploadError = "Invalid filename";
      return;
    }

    uint32_t offset = static_cast<uint32_t>(server.arg("offset").toInt());
    String target = joinPath(uploadTargetDirectory, safeFilename);

    if (offset == 0) {
      if (SD.exists(target)) {
        if (uploadError.length() == 0) uploadError = "A file with that name already exists: " + safeFilename;
        return;
      }
      uploadFile = SD.open(target, FILE_WRITE);
    } else {
      File existing = SD.open(target, FILE_READ);
      bool ok = existing && !existing.isDirectory() && existing.size() == offset;
      if (existing) existing.close();
      if (!ok) {
        if (uploadError.length() == 0) uploadError = "Chunk out of order for " + safeFilename;
        return;
      }
      uploadFile = SD.open(target, FILE_APPEND);
    }

    if (!uploadFile) {
      if (uploadError.length() == 0) uploadError = "Could not open file: " + safeFilename;
      return;
    }

    uploadCurrentTarget = target;
    uploadCurrentFileAccepted = true;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!uploadAuthorized || !uploadCurrentFileAccepted || !uploadFile) return;

    size_t written = uploadFile.write(upload.buf, upload.currentSize);
    tunnel.loop();

    if (written != upload.currentSize) {
      if (uploadError.length() == 0) uploadError = "microSD write failed";
      uploadFile.close();
      SD.remove(uploadCurrentTarget);
      uploadCurrentFileAccepted = false;
    }
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();

    if (uploadAuthorized && uploadCurrentFileAccepted) {
      uploadAnyFileSucceeded = true;
    }

    uploadCurrentFileAccepted = false;
    uploadCurrentTarget = "";
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();

    if (uploadCurrentTarget.length() > 0) {
      SD.remove(uploadCurrentTarget);
    }

    uploadCurrentTarget = "";
    uploadCurrentFileAccepted = false;

    if (uploadError.length() == 0) {
      uploadError = "Upload was interrupted";
    }
  }
}

void handleStorageUploadComplete() {
  Serial.printf("[UPLOAD COMPLETE] URI: %s\n", server.uri().c_str());
  if (!hasValidSession()) {
    if (uploadFile) uploadFile.close();
    uploadRequestStarted = false;
    uploadAnyFileSucceeded = false;
    uploadError = "";
    server.send(401, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Please log in again\"}");
    return;
  }

  if (uploadFile) uploadFile.close();
  uploadRequestStarted = false;
  if (uploadError.length() > 0) {
    String error = uploadError;
    uploadError = "";
    uploadAnyFileSucceeded = false;
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}");
    return;
  }

  if (!uploadAnyFileSucceeded) {
    uploadError = "";
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"No file was uploaded\"}");
    return;
  }

  uploadAnyFileSucceeded = false;
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Upload complete\"}");
}

// ============================================================
// AUTHENTICATED WEBSITE FOLDER UPLOAD
// ============================================================

bool ensureDirectoryTree(const String& absoluteDirectory) {
  if (SD.exists(absoluteDirectory)) {
    File existing = SD.open(absoluteDirectory, FILE_READ);
    bool isDir = existing && existing.isDirectory();
    if (existing) existing.close();
    return isDir;
  }

  int slash = absoluteDirectory.lastIndexOf('/');
  if (slash > 0) {
    String parent = absoluteDirectory.substring(0, slash);
    if (!SD.exists(parent) && !ensureDirectoryTree(parent)) return false;
  }
  return SD.mkdir(absoluteDirectory);
}

void handleWebsiteUploadData() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (websiteUploadFile) websiteUploadFile.close();
    websiteUploadAuthorized = hasValidSession();
    websiteUploadCurrentFileAccepted = false;
    websiteUploadCurrentTarget = "";
    if (!websiteUploadAuthorized) return;

    String relativeFile = String(upload.filename);
    relativeFile.replace("\\", "/");
    String normalized;
    if (!normalizeRelativePath(relativeFile, normalized)) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Invalid folder or filename in upload";
      return;
    }

    int firstSlash = normalized.indexOf('/');
    if (firstSlash <= 0 || firstSlash == normalized.length() - 1) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Select a website folder, not individual files";
      return;
    }

    String siteName = normalized.substring(0, firstSlash);
    String relativeAsset = normalized.substring(firstSlash + 1);
    if (!isSafeSegment(siteName) || isReservedTopLevel(siteName)) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Invalid or reserved website folder name";
      return;
    }

    if (websiteUploadSiteName.length() == 0) websiteUploadSiteName = siteName;
    if (websiteUploadSiteName != siteName) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Upload must contain files from exactly one selected folder";
      return;
    }

    String normalizedAsset;
    if (!normalizeRelativePath(relativeAsset, normalizedAsset) || normalizedAsset.length() == 0) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Invalid website asset path";
      return;
    }

    String siteRoot = String(WEBSITE_ROOT) + "/" + siteName;
    if (SD.exists(siteRoot) && !websiteUploadAnyFileSucceeded) {
      File existing = SD.open(siteRoot, FILE_READ);
      bool existingIsDirectory = existing && existing.isDirectory();
      if (existing) existing.close();
      if (existingIsDirectory) {
        if (websiteUploadError.length() == 0) websiteUploadError = "A website with this folder name already exists";
        return;
      }
    }

    int lastSlash = normalizedAsset.lastIndexOf('/');
    String relativeParent = (lastSlash >= 0) ? normalizedAsset.substring(0, lastSlash) : "";
    String targetDirectory = siteRoot;
    if (relativeParent.length() > 0) targetDirectory += "/" + relativeParent;
    if (!ensureDirectoryTree(targetDirectory)) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Could not create website directory on microSD";
      return;
    }

    String target = siteRoot + "/" + normalizedAsset;
    if (SD.exists(target)) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Duplicate filename in website upload";
      return;
    }

    websiteUploadFile = SD.open(target, FILE_WRITE);
    if (!websiteUploadFile) {
      if (websiteUploadError.length() == 0) websiteUploadError = "Could not create website file on microSD";
      return;
    }
    websiteUploadCurrentTarget = target;
    websiteUploadCurrentFileAccepted = true;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!websiteUploadAuthorized || !websiteUploadCurrentFileAccepted || !websiteUploadFile) return;
    size_t written = websiteUploadFile.write(upload.buf, upload.currentSize);
    tunnel.loop();
    if (written != upload.currentSize) {
      if (websiteUploadError.length() == 0) websiteUploadError = "microSD write failed";
      websiteUploadFile.close();
      SD.remove(websiteUploadCurrentTarget);
      websiteUploadCurrentFileAccepted = false;
    }
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (websiteUploadFile) websiteUploadFile.close();
    if (websiteUploadAuthorized && websiteUploadCurrentFileAccepted) websiteUploadAnyFileSucceeded = true;
    websiteUploadCurrentFileAccepted = false;
    websiteUploadCurrentTarget = "";
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (websiteUploadFile) websiteUploadFile.close();
    if (websiteUploadCurrentTarget.length() > 0) SD.remove(websiteUploadCurrentTarget);
    websiteUploadCurrentTarget = "";
    websiteUploadCurrentFileAccepted = false;
    if (websiteUploadError.length() == 0) websiteUploadError = "Website upload was interrupted";
  }
}

void handleWebsiteUploadComplete() {
  if (!hasValidSession()) {
    if (websiteUploadFile) websiteUploadFile.close();
    websiteUploadSiteName = "";
    websiteUploadAnyFileSucceeded = false;
    websiteUploadError = "";
    server.send(401, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Please log in again\"}");
    return;
  }
  if (websiteUploadFile) websiteUploadFile.close();

  if (websiteUploadError.length() > 0) {
    String error = websiteUploadError;
    websiteUploadSiteName = "";
    websiteUploadAnyFileSucceeded = false;
    websiteUploadError = "";
    sendJsonError(400, error);
    return;
  }
  if (!websiteUploadAnyFileSucceeded || websiteUploadSiteName.length() == 0) {
    websiteUploadSiteName = "";
    websiteUploadAnyFileSucceeded = false;
    sendJsonError(400, "No website files were uploaded");
    return;
  }

  String indexPath = String(WEBSITE_ROOT) + "/" + websiteUploadSiteName + "/index.html";
  if (!SD.exists(indexPath)) {
    websiteUploadSiteName = "";
    websiteUploadAnyFileSucceeded = false;
    sendJsonError(400, "The selected folder must contain index.html at its top level. Uploaded files remain on the card; upload the missing index.html or remove the incomplete folder manually.");
    return;
  }

  Serial.print("[WEB] Website uploaded: /");
  Serial.println(websiteUploadSiteName);
  websiteUploadSiteName = "";
  websiteUploadAnyFileSucceeded = false;
  discoverWebsites();
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Website uploaded\"}");
}

void handleStorageMakeDirectory() {
  if (!requireAuthentication(true)) return;
  String parentPath;
  if (!makeStoragePath(server.arg("path"), parentPath, true)) { sendJsonError(400, "Invalid parent folder"); return; }
  String name = server.arg("name");
  name.trim();
  if (!isSafeSegment(name)) { sendJsonError(400, "Invalid folder name"); return; }
  File parent = SD.open(parentPath, FILE_READ);
  bool parentIsDirectory = parent && parent.isDirectory();
  if (parent) parent.close();
  if (!parentIsDirectory) { sendJsonError(404, "Parent folder not found"); return; }
  String target = joinPath(parentPath, name);
  if (SD.exists(target)) { sendJsonError(409, "An item with that name already exists"); return; }
  if (!SD.mkdir(target)) { sendJsonError(500, "Could not create folder on microSD"); return; }
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

bool deleteRecursively(const String& path, uint8_t depth = 0) {
  if (depth > MAX_DELETE_DEPTH) return false;

  File node = SD.open(path, FILE_READ);
  if (!node) return false;

  if (!node.isDirectory()) {
    node.close();
    return SD.remove(path);
  }
  node.close();

  // Reopen the directory for each child. This avoids holding a large number
  // of open SD file handles on deeply nested folders.
  while (true) {
    File directory = SD.open(path, FILE_READ);
    if (!directory || !directory.isDirectory()) {
      if (directory) directory.close();
      return false;
    }

    File child = directory.openNextFile();
    if (!child) {
      directory.close();
      break;
    }

    String childName = fileEntryName(child);
    bool childIsDirectory = child.isDirectory();
    child.close();
    directory.close();

    if (!isSafeSegment(childName)) return false;
    String childPath = joinPath(path, childName);

    bool removed = childIsDirectory
      ? deleteRecursively(childPath, depth + 1)
      : SD.remove(childPath);
    if (!removed) return false;
    yield();
  }

  return SD.rmdir(path);
}

void handleStorageDelete() {
  if (!requireAuthentication(true)) return;

  String absolutePath;
  if (!makeStoragePath(server.arg("path"), absolutePath, false)) {
    sendJsonError(400, "Invalid path. The storage root cannot be deleted.");
    return;
  }

  if (!SD.exists(absolutePath)) {
    sendJsonError(404, "Item not found");
    return;
  }

  if (!deleteRecursively(absolutePath)) {
    sendJsonError(500, "Could not delete the item. Check the SD card and folder contents.");
    return;
  }

  server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

// ============================================================
// ZIP STREAMING (STORE / NO COMPRESSION)
// ============================================================

struct ZipEntry {
  String diskPath;
  String zipName;
  uint32_t size = 0;
  uint32_t crc = 0;
  uint32_t localOffset = 0;
  bool isDirectory = false;
};

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
  }
  return crc;
}

bool calculateFileCRC(const String& path, uint32_t& fileSize, uint32_t& resultCRC) {
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    return false;
  }

  uint64_t size64 = file.size();
  if (size64 > 0xFFFFFFFFULL) {
    file.close();
    return false;
  }

  fileSize = static_cast<uint32_t>(size64);
  uint32_t crc = 0xFFFFFFFFUL;
  uint8_t buffer[512];
  uint64_t totalRead = 0;

  while (file.available()) {
    size_t count = file.read(buffer, sizeof(buffer));
    if (count == 0) break;
    crc = crc32Update(crc, buffer, count);
    totalRead += count;
    tunnel.loop();
    yield();
  }

  file.close();
  if (totalRead != size64) return false;
  resultCRC = crc ^ 0xFFFFFFFFUL;
  return true;
}

bool collectZipDirectory(const String& diskDirectory,
                         const String& zipDirectory,
                         std::vector<ZipEntry>& entries,
                         uint8_t depth) {
  if (depth > MAX_DELETE_DEPTH || entries.size() >= MAX_ZIP_ENTRIES) return false;

  // Collect child names first, then close this directory before descending.
  // This keeps the number of simultaneously open SD files very low.
  struct ChildInfo { String name; bool isDirectory; };
  std::vector<ChildInfo> children;

  File directory = SD.open(diskDirectory, FILE_READ);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return false;
  }

  while (true) {
    File child = directory.openNextFile();
    if (!child) break;
    ChildInfo info;
    info.name = fileEntryName(child);
    info.isDirectory = child.isDirectory();
    child.close();
    if (!isSafeSegment(info.name)) {
      directory.close();
      return false;
    }
    children.push_back(info);
    if (children.size() + entries.size() >= MAX_ZIP_ENTRIES) {
      directory.close();
      return false;
    }
  }
  directory.close();

  for (const ChildInfo& child : children) {
    String childDiskPath = joinPath(diskDirectory, child.name);
    if (child.isDirectory) {
      ZipEntry directoryEntry;
      directoryEntry.diskPath = childDiskPath;
      directoryEntry.zipName = zipDirectory + child.name + "/";
      if (directoryEntry.zipName.length() > 240) return false;
      directoryEntry.isDirectory = true;
      entries.push_back(directoryEntry);
      if (!collectZipDirectory(childDiskPath, directoryEntry.zipName, entries, depth + 1)) return false;
    } else {
      if (entries.size() >= MAX_ZIP_ENTRIES) return false;
      ZipEntry fileEntry;
      fileEntry.diskPath = childDiskPath;
      fileEntry.zipName = zipDirectory + child.name;
      if (fileEntry.zipName.length() > 240) return false;
      if (!calculateFileCRC(childDiskPath, fileEntry.size, fileEntry.crc)) return false;
      entries.push_back(fileEntry);
    }
  }
  return true;
}

template <typename ClientType>
bool writeClientFully(ClientType& client, const uint8_t* data, size_t length) {
  size_t sent = 0;
  unsigned long lastProgress = millis();
  while (sent < length) {
    if (!client.connected()) return false;
    size_t n = client.write(data + sent, length - sent);
    if (n > 0) {
      sent += n;
      lastProgress = millis();
      tunnel.loop();
    } else {
      tunnel.loop();
      if (millis() - lastProgress > 5000) return false;
      delay(1);
    }
  }
  return true;
}

template <typename ClientType>
bool writeU16(ClientType& client, uint16_t value) {
  uint8_t b[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  return writeClientFully(client, b, sizeof(b));
}

template <typename ClientType>
bool writeU32(ClientType& client, uint32_t value) {
  uint8_t b[4] = {
    static_cast<uint8_t>(value),
    static_cast<uint8_t>(value >> 8),
    static_cast<uint8_t>(value >> 16),
    static_cast<uint8_t>(value >> 24)
  };
  return writeClientFully(client, b, sizeof(b));
}

template <typename ClientType>
bool writeZipFileData(ClientType& client, const ZipEntry& entry) {
  if (entry.isDirectory || entry.size == 0) return true;

  File file = SD.open(entry.diskPath, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    return false;
  }

  uint8_t buffer[512];
  uint32_t totalSent = 0;
  bool success = true;
  while (totalSent < entry.size) {
    size_t wanted = sizeof(buffer);
    uint32_t remaining = entry.size - totalSent;
    if (remaining < wanted) wanted = remaining;
    size_t count = file.read(buffer, wanted);
    if (count == 0 || !writeClientFully(client, buffer, count)) {
      success = false;
      break;
    }
    totalSent += count;
    tunnel.loop();
    yield();
  }
  file.close();
  return success && totalSent == entry.size;
}

template <typename ClientType>
bool streamZipArchive(ClientType& client,
                      const String& rootName,
                      std::vector<ZipEntry>& entries,
                      uint32_t archiveSize,
                      uint32_t centralDirectorySize) {
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + rootName + ".zip\"");
  server.setContentLength(archiveSize);
  server.send(200, "application/zip", "");

  client.setNoDelay(true);

  uint32_t offset = 0;
  const uint16_t flags = 0x0800; // UTF-8 file names
  const uint16_t dosTime = 0;
  const uint16_t dosDate = 0x0021; // 1980-01-01

  // Local file headers and file data.
  for (ZipEntry& entry : entries) {
    entry.localOffset = offset;
    uint16_t nameLength = static_cast<uint16_t>(entry.zipName.length());

    bool ok = writeU32(client, 0x04034B50UL) &&
              writeU16(client, 20) &&
              writeU16(client, flags) &&
              writeU16(client, 0) &&
              writeU16(client, dosTime) &&
              writeU16(client, dosDate) &&
              writeU32(client, entry.crc) &&
              writeU32(client, entry.size) &&
              writeU32(client, entry.size) &&
              writeU16(client, nameLength) &&
              writeU16(client, 0) &&
              writeClientFully(client, reinterpret_cast<const uint8_t*>(entry.zipName.c_str()), nameLength);
    if (!ok) return false;

    offset += 30 + nameLength;
    if (!writeZipFileData(client, entry)) return false;
    offset += entry.size;
  }

  // Central directory.
  uint32_t centralDirectoryOffset = offset;
  for (const ZipEntry& entry : entries) {
    uint16_t nameLength = static_cast<uint16_t>(entry.zipName.length());
    // ZIP host is DOS (version-made-by = 20), so directory attribute is 0x10.
    uint32_t externalAttributes = entry.isDirectory ? 0x10UL : 0;

    bool ok = writeU32(client, 0x02014B50UL) &&
              writeU16(client, 20) &&
              writeU16(client, 20) &&
              writeU16(client, flags) &&
              writeU16(client, 0) &&
              writeU16(client, dosTime) &&
              writeU16(client, dosDate) &&
              writeU32(client, entry.crc) &&
              writeU32(client, entry.size) &&
              writeU32(client, entry.size) &&
              writeU16(client, nameLength) &&
              writeU16(client, 0) &&
              writeU16(client, 0) &&
              writeU16(client, 0) &&
              writeU16(client, 0) &&
              writeU32(client, externalAttributes) &&
              writeU32(client, entry.localOffset) &&
              writeClientFully(client, reinterpret_cast<const uint8_t*>(entry.zipName.c_str()), nameLength);
    if (!ok) return false;
    offset += 46 + nameLength;
    yield();
  }

  // End of central directory record.
  uint32_t actualCentralSize = offset - centralDirectoryOffset;
  if (actualCentralSize != centralDirectorySize) return false;

  bool endOk = writeU32(client, 0x06054B50UL) &&
               writeU16(client, 0) &&
               writeU16(client, 0) &&
               writeU16(client, static_cast<uint16_t>(entries.size())) &&
               writeU16(client, static_cast<uint16_t>(entries.size())) &&
               writeU32(client, centralDirectorySize) &&
               writeU32(client, centralDirectoryOffset) &&
               writeU16(client, 0);
  return endOk;
}

void handleStorageZipDownload() {
  if (!requireAuthentication(true)) return;

  String relativePath;
  if (!normalizeRelativePath(server.arg("path"), relativePath) || relativePath.length() == 0) {
    sendJsonError(400, "Invalid folder path");
    return;
  }

  String rootPath = String(STORAGE_ROOT) + "/" + relativePath;
  File root = SD.open(rootPath, FILE_READ);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    sendJsonError(404, "Folder not found");
    return;
  }
  root.close();

  String rootName = lastPathSegment(relativePath);
  if (!isSafeSegment(rootName)) {
    sendJsonError(400, "Invalid folder name");
    return;
  }

  std::vector<ZipEntry> entries;
  entries.reserve(24);

  ZipEntry rootEntry;
  rootEntry.diskPath = rootPath;
  rootEntry.zipName = rootName + "/";
  rootEntry.isDirectory = true;
  entries.push_back(rootEntry);

  if (!collectZipDirectory(rootPath, rootEntry.zipName, entries, 0)) {
    sendJsonError(400, "Folder is too large or contains an unsupported path. ZIP limit: 120 entries.");
    return;
  }

  uint64_t localSize = 0;
  uint64_t centralSize = 0;
  for (const ZipEntry& entry : entries) {
    uint64_t nameLength = entry.zipName.length();
    localSize += 30 + nameLength + entry.size;
    centralSize += 46 + nameLength;
  }
  uint64_t totalSize = localSize + centralSize + 22;
  if (entries.size() > 65535 || totalSize > 0xFFFFFFFFULL || centralSize > 0xFFFFFFFFULL) {
    sendJsonError(400, "ZIP archive exceeds the supported ZIP32 size limit");
    return;
  }

  auto& client = server.client();
  if (!streamZipArchive(client, rootName, entries,
                        static_cast<uint32_t>(totalSize),
                        static_cast<uint32_t>(centralSize))) {
    Serial.println("[ZIP] Client disconnected or ZIP stream failed.");
  }
}

// ============================================================
// ROUTING AND ACCESS CONTROL
// ============================================================

bool isPathOrChildOf(const String& path, const String& base) {
  return path == base || path.startsWith(base + "/");
}

void handleNotFound() {
  Serial.printf("[HTTP ERROR] Method: %d | URI: %s\n",
              server.method(), server.uri().c_str());
  String requestPath = WebServer::urlDecode(server.uri());
  if (!requestPath.startsWith("/")) requestPath = "/" + requestPath;

  // Protected storage application, including every nested browser URL.
  if (isPathOrChildOf(requestPath, "/storage")) {
    if (!requireAuthentication()) return;
    if (server.method() != HTTP_GET) {
      server.send(405, "text/plain", "Method not allowed");
      return;
    }
    if (requestPath == "/storage") {
      server.sendHeader("Location", "/storage/", true);
      server.send(302, "text/plain", "Redirecting to storage");
      return;
    }
sendProgmemInChunks(
  STORAGE_HTML,
  sizeof(STORAGE_HTML) - 1,
  "text/html; charset=utf-8"
);    return;
  }

  // Protect the complete API namespace, including unknown API endpoints.
  if (isPathOrChildOf(requestPath, "/api") || isPathOrChildOf(requestPath, "/config")) {
    if (!requireAuthentication(true)) return;
    sendJsonError(404, "Not found");
    return;
  }

  // Only discovered folders under /websites are public. Their assets are
  // served without prompting for a username/password.
  if (server.method() == HTTP_GET) {
    if (servePublicWebsite(requestPath)) return;
  }

  // Everything else is private, even if the path does not exist.
  if (!requireAuthentication()) return;
  server.send(404, "text/plain", "Not found");
}

void registerRoutes() {
  server.on("/login", HTTP_GET, handleLoginPage);
  server.on("/login", HTTP_POST, handleLoginSubmit);
  server.on("/logout", HTTP_POST, handleLogout);
  server.on("/", HTTP_GET, handleHomePage);

  server.on("/storage", HTTP_GET, handleStorageRedirect);
  server.on("/storage/", HTTP_GET, handleStoragePage);

  server.on("/api/storage/list", HTTP_GET, handleStorageList);
  server.on("/api/storage/download", HTTP_GET, handleStorageDownload);
  server.on("/api/storage/zip", HTTP_GET, handleStorageZipDownload);
  server.on("/api/storage/delete", HTTP_POST, handleStorageDelete);
  server.on("/api/storage/mkdir", HTTP_POST, handleStorageMakeDirectory);
  server.on("/api/storage/upload", HTTP_POST,
            handleStorageUploadComplete, handleStorageUploadData);

  server.on("/api/websites/upload", HTTP_POST,
            handleWebsiteUploadComplete, handleWebsiteUploadData);

  server.onNotFound(handleNotFound);
}

// ============================================================
// SETUP / LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("====================================");
  Serial.println("       PicoCloud v0.8 starting");
  Serial.println("====================================");

  // Start SPI using the wiring used by the PicoCloud prototype.
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  if (!SD.begin(SD_CS_PIN, SPI, SD_FREQUENCY)) {
    Serial.println("[SD] Card mount failed. Check wiring, card format and power.");
  } else {
    sdReady = true;
    Serial.println("[SD] Card mounted successfully.");

    if (!SD.exists(WEBSITE_ROOT)) SD.mkdir(WEBSITE_ROOT);
    if (!SD.exists(STORAGE_ROOT)) SD.mkdir(STORAGE_ROOT);
    if (!SD.exists("/config")) SD.mkdir("/config");

    discoverWebsites();
  }

  if (!sdReady) {
    Serial.println("[FATAL] PicoCloud cannot start without a mounted microSD card.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[Wi-Fi] Connecting to hotspot");
  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 25000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[Wi-Fi] Connected.");
    Serial.print("[Wi-Fi] PicoCloud address: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[Wi-Fi] Initial connection timed out. The device will keep trying.");
  }

  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);
  registerRoutes();
  server.begin();
  Serial.println("[HTTP] Web server started on port 80.");

  tunnel.onStatus([](bool online, const char* publicUrl) {
    if (online) {
      Serial.print("[SuperDMZ] ONLINE: ");
      Serial.println(publicUrl);
    } else {
      Serial.println("[SuperDMZ] Tunnel offline; the library will reconnect automatically.");
    }
  });

  if (String(SUPERDMZ_TOKEN) == "PASTE_YOUR_48_CHARACTER_TOKEN_HERE") {
    Serial.println("[SuperDMZ] TOKEN NOT SET. Add your 48-character tunnel token in SUPERDMZ_TOKEN.");
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[SuperDMZ] Starting reverse tunnel...");
    tunnelStartAttempted = true;
    if (!tunnel.begin(SUPERDMZ_TOKEN, HTTP_PORT)) {
      Serial.println("[SuperDMZ] Could not start tunnel. Check token and tunnel configuration.");
    }
  } else {
    Serial.println("[SuperDMZ] Wi-Fi is not connected yet; tunnel will start after reconnection.");
  }

  Serial.println("[AUTH] Website folders are public; dashboard and storage require login sessions.");
}

void loop() {
  if (!sdReady) {
    delay(1000);
    return;
  }

  static unsigned long lastReconnectAttempt = 0;

  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttempt > 10000) {
    lastReconnectAttempt = millis();
    Serial.println("[Wi-Fi] Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (WiFi.status() == WL_CONNECTED && !tunnelStartAttempted &&
      String(SUPERDMZ_TOKEN) != "PASTE_YOUR_48_CHARACTER_TOKEN_HERE") {
    tunnelStartAttempted = true;
    Serial.println("[SuperDMZ] Starting reverse tunnel after Wi-Fi reconnection...");
    if (!tunnel.begin(SUPERDMZ_TOKEN, HTTP_PORT)) {
      Serial.println("[SuperDMZ] Initial tunnel start failed; check the token.");
    }
  }

  server.handleClient();
  tunnel.loop();
  delay(1);
}