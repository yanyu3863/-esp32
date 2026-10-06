#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#define BLINKER_WIFI
#include <Blinker.h>
#define GPIO_OUT_PIN 15

//定义GPIO引脚和状态变量
BlinkerButton Button1("btn-abc");  // 新建按键对象，键名与 App 中一致

bool pinActive = false;           // 标记 GPIO 是否处于激活状态
unsigned long pinActivateTime = 0; // 记录拉高时刻
const unsigned long PIN_HOLD_MS = 1000; // 保持 1 秒

// 热点配置
const char* AP_SSID = "test";
const char* AP_PASSWORD = ""; // 空为开放热点，建议设8位以上密码

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// 配网参数存储
String configSsid, configPswd, configAuth;
bool shouldConnect = false;

// 配网页 HTML
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

//按钮回调函数
void button1_callback(const String & state) {
    BLINKER_LOG("get button state: ", state);
    
    // 拉高 GPIO15，记录时间
    digitalWrite(GPIO_OUT_PIN, HIGH);
    pinActive = true;
    pinActivateTime = millis();
    
    // 可选：反馈状态到 App
    Button1.print("on");
}

void handleRoot() {
  server.send(200, "text/html", configPage);
}

void handleConfigure() {
  // 读取表单参数
  configSsid = server.arg("ssid");
  configPswd = server.arg("password");
  configAuth = server.arg("auth");

  // 保存到 NVS
  preferences.begin("wifi", false);
  preferences.putString("ssid", configSsid);
  preferences.putString("pswd", configPswd);
  preferences.putString("auth", configAuth);
  preferences.end();

  server.send(200, "text/html", "<h2>保存成功，设备正在连接...</h2>");
  shouldConnect = true;
}

void setup() {
  Serial.begin(115200);

  // 1. 创建热点
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP); // 强制跳转

  // 2. 启动 Web 服务
  server.on("/", handleRoot);
  server.on("/configure", handleConfigure);
  server.begin();

  Serial.println("配网热点已开启: test");
  Serial.print("访问地址: http://");
  Serial.println(apIP);

  //初始化引脚和绑定回调
  // ... 你已有的配网逻辑 ...
    
  pinMode(GPIO_OUT_PIN, OUTPUT);
  digitalWrite(GPIO_OUT_PIN, LOW);  // 默认低电平
    
  // ... 其他初始化 ...
    
  Button1.attach(button1_callback);  // 绑定按键回调
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (shouldConnect) {
    shouldConnect = false;
    server.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    delay(500);

    // 3. 接入 Blinker
    Blinker.begin(configAuth.c_str(), configSsid.c_str(), configPswd.c_str());
    Serial.println("正在接入点灯科技...");
  }

  //检查超时并拉低
  // ... 你已有的 dnsServer、server、shouldConnect 等逻辑 ...
    
  // 非阻塞延时：检查 GPIO 是否需要恢复低电平
  if (pinActive && (millis() - pinActivateTime >= PIN_HOLD_MS)) {
      digitalWrite(GPIO_OUT_PIN, LOW);
      pinActive = false;
      Button1.print("off");  // 可选：反馈状态
  }

  // Blinker 运行时循环
  Blinker.run();
}