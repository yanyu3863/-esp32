#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#define BLINKER_WIFI
#include <Blinker.h>

#define GPIO_OUT_PIN 15
#define BOOT_BTN_PIN 13        // ESP32 重置 按键

// ===== 按键与 GPIO 状态 =====
BlinkerButton Button1("btn-abc");
bool pinActive = false;
unsigned long pinActivateTime = 0;
const unsigned long PIN_HOLD_MS = 1000;

// ===== 热点配置 =====
const char* AP_SSID = "test";
const char* AP_PASSWORD = "";

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// ===== 配网参数 =====
String configSsid, configPswd, configAuth;
bool shouldConnect = false;

// ===== 启动状态机 =====
enum BootState {
  BOOT_CHECK_CONFIG,
  BOOT_TRY_CONNECT,
  BOOT_AP_CONFIG,
  BOOT_RUNNING
};
BootState bootState = BOOT_CHECK_CONFIG;

unsigned long connectStartTime = 0;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000; // 10 秒超时
int retryCount = 0;                              // 增强1：重试计数
const int MAX_RETRY = 1;

// ===== 配网页 HTML =====
const char* configPage = R"rawliteral(
<!DOCTYPE html><html><body>
<h2>ESP32 配网</h2>
<form action="/configure" method="GET">
  WiFi名称：<input type="text" name="ssid" required><br>
  WiFi密码：<input type="password" name="password"><br>
  Blinker密钥：<input type="text" name="auth" required><br>
  <input type="submit" value="保存并连接">
</form></body></html>
)rawliteral";

// ===== 按键回调 =====
void button1_callback(const String & state) {
  BLINKER_LOG("get button state: ", state);
  digitalWrite(GPIO_OUT_PIN, HIGH);
  pinActive = true;
  pinActivateTime = millis();
  Button1.print("on");
}

void handleRoot() {
  server.send(200, "text/html", configPage);
}

void handleConfigure() {
  configSsid = server.arg("ssid");
  configPswd = server.arg("password");
  configAuth = server.arg("auth");

  preferences.begin("wifi", false);
  preferences.putString("ssid", configSsid);
  preferences.putString("pswd", configPswd);
  preferences.putString("auth", configAuth);
  preferences.end();

  server.send(200, "text/html", "<h2>保存成功，设备正在连接...</h2>");
  shouldConnect = true;
}

// ===== 进入热点配网模式 =====
void startAPConfig() {
  bootState = BOOT_AP_CONFIG;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/configure", handleConfigure);
  server.begin();

  Serial.println("配网热点已开启: test");
  Serial.print("访问地址: http://");
  Serial.println(apIP);
}

// ===== 增强2：长按 BOOT 清除配置 =====
void checkBootButtonReset() {
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  if (digitalRead(BOOT_BTN_PIN) == LOW) {
    unsigned long pressStart = millis();
    Serial.println("检测到 重置 按下，长按 3 秒可清除配置...");

    while (digitalRead(BOOT_BTN_PIN) == LOW) {
      if (millis() - pressStart >= 3000) {
        Serial.println("长按确认，清除配置并重启...");
        preferences.begin("wifi", false);
        preferences.clear();
        preferences.end();
        delay(200);
        ESP.restart();
      }
      delay(10);
    }
    Serial.println("未达到 3 秒，忽略。");
  }
}

void setup() {
  Serial.begin(115200);

  // //测试短接
  // pinMode(13, INPUT_PULLUP);
  // delay(1000);

  // Serial.print("D13 初始状态: ");
  // Serial.println(digitalRead(13));

  // while (true) {
  //   Serial.print("D13 = ");
  //   Serial.println(digitalRead(13));
  //   delay(500);
  // }

  

  // GPIO 初始化
  pinMode(GPIO_OUT_PIN, OUTPUT);
  digitalWrite(GPIO_OUT_PIN, LOW);

  // 读取已保存的配网信息
  preferences.begin("wifi", true);
  configSsid = preferences.getString("ssid", "");
  configPswd = preferences.getString("pswd", "");
  configAuth = preferences.getString("auth", "");
  preferences.end();

  if (configSsid.length() > 0 && configAuth.length() > 0) {
    bootState = BOOT_TRY_CONNECT;
    WiFi.mode(WIFI_STA);
    WiFi.begin(configSsid.c_str(), configPswd.c_str());
    connectStartTime = millis();
    Serial.println("检测到已保存配置，尝试连接 WiFi...");
  } else {
    Serial.println("未检测到配置，进入配网模式");
    startAPConfig();
  }
}

void loop() {

  // 增强2：启动时检测 gpio13
  checkBootButtonReset();

  switch (bootState) {

    // ---------- 尝试直连 ----------
    case BOOT_TRY_CONNECT:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi 连接成功");
        Blinker.begin(configAuth.c_str(), configSsid.c_str(), configPswd.c_str());
        Button1.attach(button1_callback);
        bootState = BOOT_RUNNING;
      }
      else if (millis() - connectStartTime >= WIFI_CONNECT_TIMEOUT_MS) {
        // 增强1：重试一次
        if (retryCount < MAX_RETRY) {
          retryCount++;
          Serial.println("连接超时，重试一次...");
          WiFi.disconnect();
          delay(100);
          WiFi.begin(configSsid.c_str(), configPswd.c_str());
          connectStartTime = millis();
        } else {
          Serial.println("重试仍失败，进入配网模式");
          WiFi.disconnect(true);
          startAPConfig();
        }
      }
      break;

    // ---------- 热点配网 ----------
    case BOOT_AP_CONFIG:
      dnsServer.processNextRequest();
      server.handleClient();

      if (shouldConnect) {
        shouldConnect = false;
        server.stop();
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        delay(500);

        Blinker.begin(configAuth.c_str(), configSsid.c_str(), configPswd.c_str());
        Button1.attach(button1_callback);
        Serial.println("正在接入点灯科技...");
        bootState = BOOT_RUNNING;
      }
      break;

    // ---------- 正常运行 ----------
    case BOOT_RUNNING:
      if (pinActive && (millis() - pinActivateTime >= PIN_HOLD_MS)) {
        digitalWrite(GPIO_OUT_PIN, LOW);
        pinActive = false;
        Button1.print("off");
      }
      Blinker.run();
      break;

    default:
      break;
  }
}