#ifndef IMAGE_UPLOAD_H
#define IMAGE_UPLOAD_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// External references from camera.h
extern uint8_t* lastCapturedImage;
extern size_t lastCapturedImageSize;

// Store last uploaded file info
String lastUploadedFileId = "";
String lastUploadedFilePath = "";
String lastUploadedFileName = "";
size_t lastUploadedFileSize = 0;

// Upload image to Open WebUI files API
// Returns file ID on success, empty string on failure
String uploadImageToOWUI(const uint8_t* imageData, size_t imageSize, const char* filename = "camera.jpg") {
  if (!imageData || imageSize == 0) {
    Serial.println("ERROR: No image data to upload");
    return "";
  }
  
  Serial.println("\n========== UPLOADING IMAGE TO OWUI ==========");
  Serial.printf("Image size: %d bytes\n", imageSize);
  Serial.printf("Filename: %s\n", filename);
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(OWUI_BASE_URL) + "/api/v1/files/";
  Serial.printf("Upload URL: %s\n", url.c_str());
  
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(60000);
  
  // Build multipart/form-data request
  String boundary = "----ESP32ImageBoundary";
  
  String bodyStart = "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"" + String(filename) + "\"\r\n";
  bodyStart += "Content-Type: image/jpeg\r\n\r\n";
  
  String bodyEnd = "\r\n--" + boundary + "--\r\n";
  
  int contentLength = bodyStart.length() + imageSize + bodyEnd.length();
  Serial.printf("Total content length: %d bytes\n", contentLength);
  
  // Allocate buffer for full body
  uint8_t* fullBody = (uint8_t*)malloc(contentLength);
  if (!fullBody) {
    Serial.println("ERROR: Failed to allocate upload buffer");
    Serial.println("=============================================\n");
    return "";
  }
  
  // Build full body
  int offset = 0;
  memcpy(fullBody + offset, bodyStart.c_str(), bodyStart.length());
  offset += bodyStart.length();
  memcpy(fullBody + offset, imageData, imageSize);
  offset += imageSize;
  memcpy(fullBody + offset, bodyEnd.c_str(), bodyEnd.length());
  
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  
  Serial.println("Uploading image...");
  int httpCode = http.POST(fullBody, contentLength);
  free(fullBody);
  
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  String fileId = "";
  if (httpCode == 200 || httpCode == 201) {
    String response = http.getString();
    Serial.println("Upload response:");
    Serial.println(response);
    
    // Parse file ID from response
    // Expected format: {"id": "string", ...}
    int idIdx = response.indexOf("\"id\"");
    if (idIdx >= 0) {
      int start = response.indexOf('"', idIdx + 4);
      if (start >= 0) {
        start++;
        int end = response.indexOf('"', start);
        if (end >= 0) {
          fileId = response.substring(start, end);
          Serial.printf("File ID: %s\n", fileId.c_str());
          lastUploadedFileId = fileId;
        }
      }
    }
    
    // Parse path from response
    int pathIdx = response.indexOf("\"path\"");
    if (pathIdx >= 0) {
      int start = response.indexOf('"', pathIdx + 6);
      if (start >= 0) {
        start++;
        int end = response.indexOf('"', start);
        if (end >= 0) {
          lastUploadedFilePath = response.substring(start, end);
          Serial.printf("File path: %s\n", lastUploadedFilePath.c_str());
        }
      }
    }
    
    // Store filename and size
    lastUploadedFileName = String(filename);
    lastUploadedFileSize = imageSize;
    
    if (fileId.length() == 0) {
      Serial.println("ERROR: Could not parse file ID from response");
    }
  } else {
    Serial.printf("ERROR: Upload failed with code %d\n", httpCode);
    String errorResponse = http.getString();
    Serial.println("Error response:");
    Serial.println(errorResponse.substring(0, 500)); // First 500 chars
  }
  
  http.end();
  Serial.println("=============================================\n");
  
  return fileId;
}

// Upload the last captured image
String uploadLastCapturedImage() {
  if (!lastCapturedImage || lastCapturedImageSize == 0) {
    Serial.println("ERROR: No captured image to upload");
    return "";
  }
  
  // Generate unique filename with timestamp
  unsigned long timestamp = millis();
  String filename = "m5camera_" + String(timestamp) + ".jpg";
  
  return uploadImageToOWUI(lastCapturedImage, lastCapturedImageSize, filename.c_str());
}

#endif // IMAGE_UPLOAD_H
