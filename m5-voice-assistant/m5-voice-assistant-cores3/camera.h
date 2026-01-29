#ifndef CAMERA_H
#define CAMERA_H

#include "M5CoreS3.h"

// External references
extern int WIDTH;
extern int HEIGHT;

// Camera state
bool cameraInitialized = false;
uint8_t* lastCapturedImage = nullptr;
size_t lastCapturedImageSize = 0;

// Software mirror RGB565 frame buffer horizontally (in-place)
void mirrorRGB565Horizontal(uint8_t* buf, int width, int height) {
  uint16_t* pixels = (uint16_t*)buf;
  for (int y = 0; y < height; y++) {
    int rowStart = y * width;
    for (int x = 0; x < width / 2; x++) {
      int left = rowStart + x;
      int right = rowStart + (width - 1 - x);
      // Swap pixels
      uint16_t temp = pixels[left];
      pixels[left] = pixels[right];
      pixels[right] = temp;
    }
  }
}

// Initialize camera using M5CoreS3 library
bool initCamera() {
  if (cameraInitialized) {
    Serial.println("Camera already initialized");
    return true;
  }
  
  Serial.println("\n========== INITIALIZING CAMERA ==========");
  Serial.println("Using M5CoreS3 Camera API");
  
  if (!CoreS3.Camera.begin()) {
    Serial.println("Camera Init Failed");
    Serial.println("=========================================\n");
    return false;
  }
  
  Serial.println("Camera Init Success!");
  
  // Get sensor using ESP-IDF API directly
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    Serial.printf("Camera sensor PID: 0x%04X\n", s->id.PID);
    
    // Set frame size to QVGA (320x240) to match display
    s->set_framesize(s, FRAMESIZE_QVGA);
    
    // Grab a dummy frame to fully initialize sensor before changing settings
    if (CoreS3.Camera.get()) {
      CoreS3.Camera.free();
    }
    
    // GC0308 (0x9B) - try different mirror/flip combinations
    // The sensor may have inverted logic for hmirror
    s->set_hmirror(s, 0);  // Try 0 since 1 didn't work
    s->set_vflip(s, 0);
    
    Serial.println("Sensor settings applied: hmirror=0, vflip=0 (after dummy frame)");
  } else {
    Serial.println("WARNING: Could not get sensor handle");
    // Fallback to CoreS3.Camera.sensor
    CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);
  }
  
  cameraInitialized = true;
  Serial.println("Camera ready for image capture");
  Serial.println("=========================================\n");
  return true;
}

// Capture image using M5CoreS3 Camera API
bool captureImage() {
  if (!cameraInitialized) {
    Serial.println("Camera not initialized");
    return false;
  }
  
  Serial.println("\n========== CAPTURING IMAGE ==========");
  
  // Free previous image if exists
  if (lastCapturedImage) {
    free(lastCapturedImage);
    lastCapturedImage = nullptr;
    lastCapturedImageSize = 0;
  }
  
  // Capture frame using CoreS3.Camera
  if (!CoreS3.Camera.get()) {
    Serial.println("Camera capture failed");
    Serial.println("=====================================\n");
    return false;
  }
  
  Serial.printf("Raw frame: %dx%d, %d bytes, format=%d\n", 
                CoreS3.Camera.fb->width, 
                CoreS3.Camera.fb->height, 
                CoreS3.Camera.fb->len,
                CoreS3.Camera.fb->format);
  
  // Software mirror the frame buffer (GC0308 hmirror register doesn't work)
  mirrorRGB565Horizontal(CoreS3.Camera.fb->buf, 
                         CoreS3.Camera.fb->width, 
                         CoreS3.Camera.fb->height);
  Serial.println("Applied software horizontal mirror");
  
  // Convert to JPEG for upload (frame buffer is RGB565)
  uint8_t* jpgBuf = nullptr;
  size_t jpgLen = 0;
  
  bool converted = frame2jpg(CoreS3.Camera.fb, 80, &jpgBuf, &jpgLen);
  
  if (!converted || !jpgBuf || jpgLen == 0) {
    Serial.println("Failed to convert frame to JPEG");
    CoreS3.Camera.free();
    Serial.println("=====================================\n");
    return false;
  }
  
  Serial.printf("JPEG converted: %d bytes\n", jpgLen);
  
  // Store the JPEG data
  lastCapturedImageSize = jpgLen;
  lastCapturedImage = (uint8_t*)malloc(lastCapturedImageSize);
  if (!lastCapturedImage) {
    Serial.println("Failed to allocate memory for JPEG");
    free(jpgBuf);
    CoreS3.Camera.free();
    Serial.println("=====================================\n");
    return false;
  }
  
  memcpy(lastCapturedImage, jpgBuf, lastCapturedImageSize);
  free(jpgBuf);
  
  // Free the frame buffer
  CoreS3.Camera.free();
  
  Serial.printf("Image stored successfully: %d bytes JPEG\n", lastCapturedImageSize);
  Serial.println("=====================================\n");
  return true;
}

// Display captured image on screen
void displayCapturedImage() {
  if (!lastCapturedImage || lastCapturedImageSize == 0) {
    Serial.println("No image to display");
    return;
  }
  
  Serial.println("Displaying captured image...");
  
  // Display as JPEG
  CoreS3.Display.drawJpg(lastCapturedImage, lastCapturedImageSize, 0, 0, WIDTH, HEIGHT);
}

// Cleanup camera resources
void cleanupCamera() {
  if (lastCapturedImage) {
    free(lastCapturedImage);
    lastCapturedImage = nullptr;
    lastCapturedImageSize = 0;
  }
  
  // Note: M5CoreS3 library doesn't have explicit deinit
  cameraInitialized = false;
}

#endif // CAMERA_H
