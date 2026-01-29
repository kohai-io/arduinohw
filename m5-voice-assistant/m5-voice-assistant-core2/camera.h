#ifndef CAMERA_H
#define CAMERA_H

#include <esp_camera.h>

// External references
extern int WIDTH;
extern int HEIGHT;

// Camera state
bool cameraInitialized = false;
uint8_t* lastCapturedImage = nullptr;
size_t lastCapturedImageSize = 0;

// Camera configuration for M5Stack CoreS3/CoreS3 Lite (GC0308)
camera_config_t getCameraConfig() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 11;
  config.pin_d1 = 9;
  config.pin_d2 = 8;
  config.pin_d3 = 10;
  config.pin_d4 = 12;
  config.pin_d5 = 18;
  config.pin_d6 = 17;
  config.pin_d7 = 16;
  config.pin_xclk = 2;
  config.pin_pclk = 13;
  config.pin_vsync = 46;
  config.pin_href = 38;
  config.pin_sccb_sda = 1;  // Note: sccb not sscb
  config.pin_sccb_scl = 0;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;  // 320x240
  config.jpeg_quality = 12;
  config.fb_count = 2;  // Double buffering
  config.fb_location = CAMERA_FB_IN_PSRAM;  // Use PSRAM for frame buffer
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.sccb_i2c_port = 0;  // Use I2C port 0
  
  return config;
}

// Camera init result for async initialization
static esp_err_t camera_init_result = ESP_FAIL;
static bool camera_init_done = false;

// Task to initialize camera asynchronously
void cameraInitTask(void* param) {
  camera_config_t* config = (camera_config_t*)param;
  Serial.println("Camera init task started...");
  camera_init_result = esp_camera_init(config);
  camera_init_done = true;
  Serial.printf("Camera init task completed with result: 0x%x\n", camera_init_result);
  vTaskDelete(NULL);
}

// Initialize camera for CoreS3/CoreS3 Lite
bool initCamera() {
  if (cameraInitialized) {
    Serial.println("Camera already initialized");
    return true;
  }
  
  Serial.println("\n========== INITIALIZING CAMERA ==========");
  Serial.println("NOTE: CoreS3 Lite camera may not be supported yet");
  Serial.println("Camera requires dedicated I2C bus that may conflict with M5Unified");
  
  camera_config_t config = getCameraConfig();
  
  Serial.println("Starting camera initialization (5 second timeout)...");
  Serial.flush();
  
  // Reset init state
  camera_init_done = false;
  camera_init_result = ESP_FAIL;
  
  // Start camera init in separate task
  TaskHandle_t initTask;
  xTaskCreate(cameraInitTask, "camera_init", 4096, &config, 1, &initTask);
  
  // Wait for init to complete with timeout
  unsigned long startTime = millis();
  while (!camera_init_done && (millis() - startTime < 5000)) {
    delay(100);
    Serial.print(".");
  }
  Serial.println();
  
  if (!camera_init_done) {
    Serial.println("Camera init timed out after 5 seconds");
    Serial.println("This usually means:");
    Serial.println("  1. No camera hardware connected");
    Serial.println("  2. I2C communication failure");
    Serial.println("  3. Incorrect pin configuration");
    Serial.println("=========================================\n");
    return false;
  }
  
  esp_err_t err = camera_init_result;
  
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    
    // Common error codes
    if (err == ESP_ERR_NOT_FOUND) {
      Serial.println("Camera sensor not found (I2C communication failed)");
    } else if (err == ESP_ERR_NO_MEM) {
      Serial.println("Not enough memory for camera");
    } else if (err == ESP_ERR_INVALID_ARG) {
      Serial.println("Invalid camera configuration");
    }
    
    Serial.println("=========================================\n");
    return false;
  }
  
  Serial.println("Camera hardware initialized!");
  
  // Get sensor settings
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 = No Effect
    s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled
    s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    s->set_ae_level(s, 0);       // -2 to 2
    s->set_aec_value(s, 300);    // 0 to 1200
    s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
    s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    s->set_dcw(s, 1);            // 0 = disable , 1 = enable
    s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
  }
  
  cameraInitialized = true;
  Serial.println("Camera ready for image capture");
  Serial.println("=========================================\n");
  return true;
}

// Capture an image from the camera
bool captureImage() {
  if (!cameraInitialized) {
    Serial.println("ERROR: Camera not initialized");
    return false;
  }
  
  Serial.println("\n========== CAPTURING IMAGE ==========");
  
  // Free previous image if exists
  if (lastCapturedImage) {
    free(lastCapturedImage);
    lastCapturedImage = nullptr;
    lastCapturedImageSize = 0;
  }
  
  // Capture frame
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    Serial.println("=====================================\n");
    return false;
  }
  
  Serial.printf("Image captured: %dx%d, %d bytes\n", fb->width, fb->height, fb->len);
  
  // Store the image data
  lastCapturedImageSize = fb->len;
  lastCapturedImage = (uint8_t*)malloc(lastCapturedImageSize);
  if (!lastCapturedImage) {
    Serial.println("Failed to allocate memory for image");
    esp_camera_fb_return(fb);
    Serial.println("=====================================\n");
    return false;
  }
  
  memcpy(lastCapturedImage, fb->buf, lastCapturedImageSize);
  
  // Return the frame buffer
  esp_camera_fb_return(fb);
  
  Serial.println("Image saved to buffer");
  Serial.println("=====================================\n");
  return true;
}

// Display captured image on screen (optional preview)
void displayCapturedImage() {
  if (!lastCapturedImage || lastCapturedImageSize == 0) {
    Serial.println("No image to display");
    return;
  }
  
  // Draw JPEG to display
  M5.Display.drawJpg(lastCapturedImage, lastCapturedImageSize, 0, 0, WIDTH, HEIGHT);
}

// Cleanup camera resources
void cleanupCamera() {
  if (lastCapturedImage) {
    free(lastCapturedImage);
    lastCapturedImage = nullptr;
    lastCapturedImageSize = 0;
  }
  
  if (cameraInitialized) {
    esp_camera_deinit();
    cameraInitialized = false;
  }
}

#endif // CAMERA_H
