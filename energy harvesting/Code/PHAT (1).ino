#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Đã đổi sang chân 34 để tránh xung đột với TX0 (chân 1) của cổng Serial
#define PIN_DO_AP 6 
float HE_SO_PHAN_AP = 2.0; 
float VI_CHINH_OFFSET1 = 0.007; 

uint8_t receiverAddress[] = {0x70, 0x4B, 0xCA, 0x6E, 0x3B, 0xC8};

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
esp_now_peer_info_t peerInfo;

// Các biến phục vụ Mã hóa dự đoán (DPCM)
int dienApTruoc_mV = 0;
int tocDoTruoc_mV = 0; 
unsigned long tongSoGoiDaGui = 0;
unsigned long soGoiLoiACK = 0;

// Các biến volatile để sử dụng đồng bộ giữa callback và loop
volatile uint32_t thoiGianTreTruocDo = 0; 
volatile unsigned long timeStart = 0;    

// THÊM: Các biến phục vụ việc định thời gian bằng millis()
unsigned long thoiGianDoCu = 0;
unsigned long thoiGianGuiCu = 0;
float dienApDaLocHienTai = 0.0;

void encodeHuffman(int saiSo_mV) {
  myData.isKeyFrame = 0;
  if (saiSo_mV == 0)       { myData.payload.huffman.bitLen = 1; myData.payload.huffman.code = 0b0; }
  else if (saiSo_mV == 1)  { myData.payload.huffman.bitLen = 2; myData.payload.huffman.code = 0b10; }
  else if (saiSo_mV == -1) { myData.payload.huffman.bitLen = 3; myData.payload.huffman.code = 0b110; }
  else if (saiSo_mV == 2)  { myData.payload.huffman.bitLen = 4; myData.payload.huffman.code = 0b1110; }
  else if (saiSo_mV == -2) { myData.payload.huffman.bitLen = 5; myData.payload.huffman.code = 0b11110; }
  else { 
    uint8_t safeSaiSo = (uint8_t)saiSo_mV;
    myData.payload.huffman.bitLen = 13; 
    myData.payload.huffman.code = (0b11111 << 8) | safeSaiSo; 
  }
}

// Hàm callback chuẩn của ESP-NOW để nhận phản hồi (ACK)
void OnDataSent(const wifi_tx_info_t *txInfo, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    // Nhận được ACK -> Tính thời gian RTT chia đôi để ra độ trễ 1 chiều
    thoiGianTreTruocDo = (uint32_t)((micros() - timeStart) / 2); 
  } else {
    // Mất gói tin (không nhận được ACK)
    soGoiLoiACK++;
  }
}

// BỘ LỌC: TRIMMED MEAN KẾT HỢP EMA (CHỐNG NHIỄU TUYỆT ĐỐI)
float dienApDaLoc = -1.0; 
const float ALPHA = 0.5; 

float docDienApChuan(int pin) {
  int rawValues[64];
  
  // 1. Lấy 64 mẫu liên tục
  for(int i = 0; i < 64; i++) {
    rawValues[i] = analogReadMilliVolts(pin);
  }
  
  // 2. Bubble Sort
  for(int i = 0; i < 63; i++) {
    for(int j = i + 1; j < 64; j++) {
      if(rawValues[i] > rawValues[j]) {
        int temp = rawValues[i];
        rawValues[i] = rawValues[j];
        rawValues[j] = temp;
      }
    }
  }
  
  // 3. Trimmed Mean (Lọc 32 mẫu giữa)
  long tong_mV = 0;
  for(int i = 16; i < 48; i++) {
    tong_mV += rawValues[i];
  }
  float dienApTucThoi = (tong_mV / 32.0) / 1000.0;
  
  // 4. SMART EMA FILTER (Lọc thông minh)
  if (dienApDaLoc < 0) {
    dienApDaLoc = dienApTucThoi; // Gán lần đầu
  } else {
    // Nếu độ lệch giữa số cũ và số thực tế lớn hơn 0.03V (30mV)
    // -> Báo hiệu có sự kiện cắm/rút tụ đột ngột -> Nhảy số ngay lập tức
    if (abs(dienApTucThoi - dienApDaLoc) > 0.03) {
      dienApDaLoc = dienApTucThoi; 
    } 
    // Nếu biến động nhỏ -> Do nhiễu -> Kích hoạt lọc EMA chậm rãi
    else {
      dienApDaLoc = (ALPHA * dienApTucThoi) + ((1.0 - ALPHA) * dienApDaLoc);
    }
  }
  
  return dienApDaLoc;
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(PIN_DO_AP, ADC_11db);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE);
  
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_send_cb(OnDataSent);
  
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.peer_addr[0] = receiverAddress[0];
  peerInfo.channel = 11;  
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  
  Serial.println("--- PHÁT: ĐÃ KÍCH HOẠT LỌC NHIỄU & DPCM + HUFFMAN ---");
}

void loop() {
  unsigned long thoiGianHienTai = millis();

  // ==============================================================
  // CHU KỲ 1: ĐO VÀ LỌC ĐIỆN ÁP LIÊN TỤC MỖI 20ms (50 lần/giây)
  // ==============================================================
  if (thoiGianHienTai - thoiGianDoCu >= 20) {
    dienApDaLocHienTai = docDienApChuan(PIN_DO_AP);
    thoiGianDoCu = thoiGianHienTai;
  }

  // ==============================================================
  // CHU KỲ 2: TÍNH TOÁN, ĐÓNG GÓI, GỬI VÀ IN DỮ LIỆU MỖI 1000ms
  // ==============================================================
  if (thoiGianHienTai - thoiGianGuiCu >= 1000) {
    
    float dienApChanDo = dienApDaLocHienTai;
    
    // Tinh chỉnh sai số theo phân khúc
    if (dienApChanDo * HE_SO_PHAN_AP <= 3.0) {
      dienApChanDo += 0.014;
    }
    else if (dienApChanDo * HE_SO_PHAN_AP <= 4.0) {
      dienApChanDo += 0.007;
    }
    else {
      dienApChanDo += 0.007;
    }
    
    // Nhân hệ số phân áp để quy đổi ra mức điện áp thật
    float dienApTuHienTai = dienApChanDo * HE_SO_PHAN_AP;
    
    tongSoGoiDaGui++;
    
    int dienApHienTai_mV = round(dienApTuHienTai * 1000.0);
    int dienApDuDoan_mV = dienApTruoc_mV + tocDoTruoc_mV; 
    int saiSo_mV = dienApHienTai_mV - dienApDuDoan_mV;

    if (tongSoGoiDaGui % 5 == 1 || saiSo_mV > 127 || saiSo_mV < -128) {
      myData.isKeyFrame = 1;
      myData.payload.dienApGoc = dienApTuHienTai;
      dienApTruoc_mV = dienApHienTai_mV;
      tocDoTruoc_mV = 0; 
    } else {
      encodeHuffman(saiSo_mV);
      tocDoTruoc_mV = dienApHienTai_mV - dienApTruoc_mV;
      dienApTruoc_mV = dienApHienTai_mV;
    }

    float lossRate = ((float)soGoiLoiACK / tongSoGoiDaGui) * 100.0;
    
    // 1. Đóng gói đầy đủ dữ liệu
    myData.packetID = tongSoGoiDaGui;
    myData.lossRate = lossRate; 
    myData.txDelay = thoiGianTreTruocDo; // Nhúng độ trễ của lần gửi trước vào gói này

    // 2. Chốt mốc thời gian bắt đầu gửi
    timeStart = micros();
    
    // 3. Thực thi lệnh gửi
    esp_now_send(receiverAddress, (uint8_t *) &myData, sizeof(myData));
    
    // In thông tin ra màn hình
    Serial.print(" Áp: "); Serial.print(dienApTuHienTai, 3); Serial.print("V | ");
    if (myData.isKeyFrame) {
      Serial.print("GÓI GỐC (32-bit) | ");
    } else {
      float ratio = 32.0 / myData.payload.huffman.bitLen;
      Serial.print("HUFFMAN ("); Serial.print(myData.payload.huffman.bitLen);
      Serial.print(" bit) -> NÉN: "); Serial.print(ratio, 1); Serial.print("x | ");
    }
    Serial.print("LỖI ĐƯỜNG TRUYỀN: "); Serial.print(lossRate, 1);
    Serial.print("% | TRỄ: "); Serial.print(thoiGianTreTruocDo); Serial.println(" us");

    // Cập nhật lại mốc thời gian gửi
    thoiGianGuiCu = thoiGianHienTai;
  }
}