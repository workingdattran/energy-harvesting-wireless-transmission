#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- THÔNG TIN WIFI VÀ GOOGLE SHEETS ---
const char* ssid = "depzailatengoi";
const char* password = "tranducdat";
const char* scriptUrl = "https://script.google.com/macros/s/AKfycbxbDeGb_7TwyR9PDk_l7dfxE3b5P4eicFt4YnIJgVGBVs9WbP96MlSlnLDNgulAzeRiyw/exec";

unsigned long lastPrintTime = 0;
const unsigned long printInterval = 250;

#pragma pack(push, 1)
typedef struct struct_message {
  uint32_t packetID;
  uint8_t isKeyFrame; 
  float lossRate;     
  uint32_t txDelay;   
  union {
    float dienApGoc;
    struct {
      uint16_t code;
      uint8_t bitLen;
    } huffman;
  } payload;
} struct_message;
#pragma pack(pop)

struct_message myData;

float dienApHienTai = 0.0;
int dienApHienTai_mV = 0;
int tocDoTruoc_mV = 0;

float phat_lossRate = 0.0;
uint32_t phat_txDelay = 0;

uint32_t expectedPacketID = 0;
unsigned long tongSoGoiBiMat = 0;
unsigned long tongSoGoiDaNhan = 0;
unsigned long tongSoByteDaNhan = 0;
unsigned long thoiGianBatDauNhan = 0;

float packetLossRate = 0.0;
float throughput = 0.0;

volatile bool coDuLieuMoi = false; 
volatile bool laGoiGoc = false; 

int decodeHuffman(uint16_t code, uint8_t bitLen) {
  if (bitLen == 1 && code == 0b0) return 0;
  if (bitLen == 2 && code == 0b10) return 1;
  if (bitLen == 3 && code == 0b110) return -1;
  if (bitLen == 4 && code == 0b1110) return 2;
  if (bitLen == 5 && code == 0b11110) return -2;
  if (bitLen == 13) return (int8_t)(code & 0xFF); 
  return 0; 
}

void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
  unsigned long thoiGianHienTai = millis();
  memcpy(&myData, incomingData, sizeof(myData));
  
  if (tongSoGoiDaNhan == 0) thoiGianBatDauNhan = thoiGianHienTai;

  laGoiGoc = myData.isKeyFrame; 
  phat_lossRate = myData.lossRate; 
  phat_txDelay = myData.txDelay;   

  // 1. GIẢI MÃ QUÁN TÍNH DPCM
  if (laGoiGoc) {
    dienApHienTai = myData.payload.dienApGoc;
    dienApHienTai_mV = round(dienApHienTai * 1000.0);
    tocDoTruoc_mV = 0; 
  } else {
    int saiSo_mV = decodeHuffman(myData.payload.huffman.code, myData.payload.huffman.bitLen);
    
    int dienApDuDoan_mV = dienApHienTai_mV + tocDoTruoc_mV;
    int dienApMoi_mV = dienApDuDoan_mV + saiSo_mV;
    int dienApCu_mV = dienApHienTai_mV; 
    
    dienApHienTai_mV = dienApMoi_mV;
    dienApHienTai = dienApHienTai_mV / 1000.0;
    
    tocDoTruoc_mV = dienApHienTai_mV - dienApCu_mV; 
  }

  // 2. THỐNG KÊ TRUYỀN TIN
  if (tongSoGoiDaNhan == 0) expectedPacketID = myData.packetID;
  if (myData.packetID > expectedPacketID) tongSoGoiBiMat += (myData.packetID - expectedPacketID);
  
  expectedPacketID = myData.packetID + 1;
  tongSoGoiDaNhan++;

  packetLossRate = ((float)tongSoGoiBiMat / (tongSoGoiDaNhan + tongSoGoiBiMat)) * 100.0;

  tongSoByteDaNhan += len;
  float thoiGianChay_s = (thoiGianHienTai - thoiGianBatDauNhan) / 1000.0;
  if (thoiGianChay_s > 0) throughput = tongSoByteDaNhan / thoiGianChay_s;

  coDuLieuMoi = true;
}

// HÀM ĐẨY DỮ LIỆU LÊN GOOGLE SHEETS
void sendToGoogleSheets(String loaiGoiStr) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // --- 1. GỬI DỮ LIỆU CỦA NODE PHÁT (TX) ---
    http.begin(scriptUrl);
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<200> docTX;
    docTX["nodeType"] = "TX";
    docTX["packetID"] = myData.packetID;
    docTX["dienAp"] = dienApHienTai;
    docTX["loaiGoi"] = loaiGoiStr;
    docTX["txLoss"] = phat_lossRate;
    docTX["txDelay"] = phat_txDelay;
    
    String reqTX; serializeJson(docTX, reqTX);
    http.POST(reqTX);
    http.end();

    // --- 2. GỬI DỮ LIỆU CỦA NODE NHẬN (RX) ---
    http.begin(scriptUrl);
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<200> docRX;
    docRX["nodeType"] = "RX";
    docRX["packetID"] = myData.packetID;
    docRX["dienAp"] = dienApHienTai;
    docRX["loaiGoi"] = loaiGoiStr;
    docRX["rxLoss"] = packetLossRate;
    docRX["bangThong"] = throughput;
    
    String reqRX; serializeJson(docRX, reqRX);
    http.POST(reqRX);
    http.end();
  } else {
    Serial.println("Lỗi kết nối WiFi, không thể gửi lên Sheets");
  }
}

void setup() {
  Serial.begin(115200);
  
  // 1. Kết nối WiFi để lấy Internet
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  delay(1000);
  
  // Lấy kênh WiFi hiện tại của Router
  int wifiChannel = WiFi.channel();
  
  // 2. Khởi tạo ESP-NOW ĐỒNG BỘ với kênh WiFi của Router
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Lỗi khởi tạo ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.print("--- KÊNH HOẠT ĐỘNG: "); Serial.print(wifiChannel); Serial.println(" ---");
  Serial.println("--- NHẬN: ĐÃ KÍCH HOẠT GIẢI MÃ PREDICTIVE CODING ---");
}

void loop() {
  // Tạo biến static để đếm thời gian riêng cho việc gửi lên Sheets
  static unsigned long lastSendTime = 0;
  const unsigned long sendInterval = 5000; // Thay đổi số này (ms) để chỉnh tần suất gửi Sheets (ví dụ 5000 = 5 giây)

  if (millis() - lastPrintTime >= printInterval) { 
    lastPrintTime = millis();
    
    if (coDuLieuMoi) {
      coDuLieuMoi = false; 
      String loaiGoiStr = laGoiGoc ? "GOC" : "HUF";

      // 1. In log ra Serial (Chạy mượt mà theo đúng printInterval)
      Serial.print("Ap: ");
      Serial.print(dienApHienTai, 3); Serial.print("V | ");
      Serial.print("GOI: "); Serial.print(loaiGoiStr);
      Serial.print(" | MAT GOI RX: "); Serial.print(packetLossRate, 1); Serial.print("% | ");
      Serial.print("BANG THONG: "); Serial.print(throughput, 1); Serial.println(" Bps");

      // 2. Gửi dữ liệu lên Sheets (Được tách riêng rẽ, chỉ chạy khi đủ thời gian sendInterval)
      if (millis() - lastSendTime >= sendInterval) {
        lastSendTime = millis();
        sendToGoogleSheets(loaiGoiStr);
      }
    }
  }
}