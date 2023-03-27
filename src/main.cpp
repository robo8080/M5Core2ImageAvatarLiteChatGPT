#if defined( ARDUINO )
#include <Arduino.h>
#include <SD.h>
#include <SPIFFS.h>
#endif

#include <M5Unified.h>
#include <AudioOutput.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include "AudioFileSourceVoiceTextStream.h"
#include "AudioOutputM5Speaker.h"
//#include <ServoEasing.hpp> // https://github.com/ArminJo/ServoEasing       

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "rootCACertificate.h"
#include <ArduinoJson.h>
#include <ESP32WebServer.h>
#include <ESPmDNS.h>

const char *SSID = "YOUR_WIFI_SSID";
const char *PASSWORD = "YOUR_WIFI_PASSWORD";

ESP32WebServer server(80);

// #define SDU_APP_PATH "/M5Core2AvatarLite.bin" // title for SD-Updater UI
// #define SDU_APP_NAME "Image Avater Lite" // title for SD-Updater UI
// #include <M5StackUpdater.h> // https://github.com/tobozo/M5Stack-SD-Updater/

#include "M5ImageAvatarLite.h"
#include "ImageAvatarSystemConfig.h" 

// サーボを利用しない場合は下記の1行をコメントアウトしてください。
#define USE_SERVO

// M5GoBottomのLEDを使わない場合は下記の1行をコメントアウトしてください。
//#define USE_LED

// デバッグしたいときは下記の１行コメントアウトしてください。
//#define DEBUG

M5GFX &gfx( M5.Lcd ); // aliasing is better than spawning two instances of LGFX

// JSONファイルとBMPファイルを置く場所を切り替え
// 開発時はSPIFFS上に置いてUploadするとSDカードを抜き差しする手間が省けます。
fs::FS json_fs = SD; // JSONファイルの収納場所(SPIFFS or SD)
fs::FS bmp_fs  = SD; // BMPファイルの収納場所(SPIFFS or SD)

using namespace m5imageavatar;


ImageAvatarSystemConfig system_config;
const char* avatar_system_json = "/json/M5AvatarLiteSystem.json"; // ファイル名は32バイトを超えると不具合が起きる場合あり
uint8_t avatar_count = 0;
ImageAvatarLite avatar(json_fs, bmp_fs);
#ifdef USE_SERVO
  #include "ImageAvatarServo.h"
  ImageAvatarServo servo;
  bool servo_enable = true; // サーボを動かすかどうか
  TaskHandle_t servoloopTaskHangle;
#endif

#ifdef USE_LED
  #include <FastLED.h>
  #define NUM_LEDS 10
#ifdef ARDUINO_M5STACK_FIRE
  #define LED_PIN 15
#else
  #define LED_PIN 25
#endif
  CRGB leds[NUM_LEDS];
  CRGB led_table[NUM_LEDS / 2] = {CRGB::Blue, CRGB::Green, CRGB::Yellow, CRGB::Orange, CRGB::Red };
  void turn_off_led() {
    // Now turn the LED off, then pause
    for(int i=0;i<NUM_LEDS;i++) leds[i] = CRGB::Black;
    FastLED.show();  
  }

  void clear_led_buff() {
    // Now turn the LED off, then pause
    for(int i=0;i<NUM_LEDS;i++) leds[i] =  CRGB::Black;
  }

  void level_led(int level1, int level2) {  
  if(level1 > 5) level1 = 5;
  if(level2 > 5) level2 = 5;
    
    clear_led_buff(); 
    for(int i=0;i<level1;i++){
      leds[NUM_LEDS/2-1-i] = led_table[i];
    }
    for(int i=0;i<level2;i++){
      leds[i+NUM_LEDS/2] = led_table[i];
    }
    FastLED.show();
  }
#endif
char *text1 = "みなさんこんにちは、私の名前はスタックチャンです、よろしくね。";
char *tts_parms1 ="&emotion_level=4&emotion=happiness&format=mp3&speaker=takeru&volume=200&speed=100&pitch=130"; // he has natural(16kHz) wav voice
char *tts_parms2 ="&emotion=happiness&format=mp3&speaker=hikari&volume=200&speed=120&pitch=130"; // he has natural(16kHz) wav voice
char *tts_parms3 ="&emotion=anger&format=mp3&speaker=bear&volume=200&speed=120&pitch=100"; // he has natural(16kHz) wav voice

// C++11 multiline string constants are neato...
static const char HEAD[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <title>AIｽﾀｯｸﾁｬﾝ</title>
</head>)KEWL";

String speech_text = "";
String speech_text_buffer = "";

void handleRoot() {
  server.send(200, "text/plain", "hello from m5stack!");
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
//  server.send(404, "text/plain", message);
  server.send(404, "text/html", String(HEAD) + String("<body>") + message + String("</body>"));
}

void VoiceText_tts(char *text,char *tts_parms);
void handle_speech() {
  String message = server.arg("say");
//  message = message + "\n";
  Serial.println(message);
  ////////////////////////////////////////
  // 音声の発声
  ////////////////////////////////////////
//  avatar.setExpression(Expression::Happy);
  VoiceText_tts((char*)message.c_str(),tts_parms2);
//  avatar.setExpression(Expression::Neutral);
  server.send(200, "text/plain", String("OK"));
}

String https_post_json(const char* url, const char* json_string, const char* root_ca) {
  String payload = "";
  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) {
    client -> setCACert(root_ca);
    {
      // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
      HTTPClient https;
      https.setTimeout( 25000 ); 
  
      Serial.print("[HTTPS] begin...\n");
      if (https.begin(*client, url)) {  // HTTPS
        Serial.print("[HTTPS] POST...\n");
        // start connection and send HTTP header
        https.addHeader("Content-Type", "application/json");
        https.addHeader("Authorization", "Bearer YOUR_API_KEY");
        int httpCode = https.POST((uint8_t *)json_string, strlen(json_string));
  
        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          Serial.printf("[HTTPS] POST... code: %d\n", httpCode);
  
          // file found at server
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            payload = https.getString();
          }
        } else {
          Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }  
        https.end();
      } else {
        Serial.printf("[HTTPS] Unable to connect\n");
      }
      // End extra scoping block
    }  
    delete client;
  } else {
    Serial.println("Unable to create client");
  }
  return payload;
}

String chatGpt(String json_string) {
  String response = "";
//  String json_string = "{\"model\": \"gpt-3.5-turbo\",\"messages\": [{\"role\": \"user\", \"content\": \"" + text + "\"},{\"role\": \"system\", \"content\": \"あなたは「スタックちゃん」と言う名前の小型ロボットとして振る舞ってください。\"},{\"role\": \"system\", \"content\": \"あなたはの使命は人々の心を癒すことです。\"},{\"role\": \"system\", \"content\": \"幼い子供の口調で話してください。\"}]}";
//  avatar.setExpression(Expression::Doubt);
//  avatar.setSpeechText("考え中…");
//    avatar.setExpression(system_config.getAvatarJsonFilename(3).c_str(), 2);
  VoiceText_tts("考え中！",tts_parms2);
  String ret = https_post_json("https://api.openai.com/v1/chat/completions", json_string.c_str(), root_ca_openai);
  VoiceText_tts("分かった！",tts_parms2);
  delay(1000);
//    avatar.setExpression(system_config.getAvatarJsonFilename(3).c_str(), 0);
//  avatar.setExpression(Expression::Neutral);
//  avatar.setSpeechText("");
  Serial.println(ret);
  if(ret != ""){
    DynamicJsonDocument doc(2000);
    DeserializationError error = deserializeJson(doc, ret.c_str());
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
//      avatar.setExpression(Expression::Sad);
//      avatar.setSpeechText("エラーです");
      response = "エラーです";
      delay(1000);
//      avatar.setSpeechText("");
//      avatar.setExpression(Expression::Neutral);
    }else{
      const char* data = doc["choices"][0]["message"]["content"];
      Serial.println(data);
      response = String(data);
      std::replace(response.begin(),response.end(),'\n',' ');
    }
  } else {
//    avatar.setExpression(Expression::Sad);
//    avatar.setSpeechText("わかりません");
    response = "わかりません";
    delay(1000);
//    avatar.setSpeechText("");
//    avatar.setExpression(Expression::Neutral);
  }
  return response;
}

void handle_chat() {
  static String response = "";
  String text = server.arg("text");
  String json_string = "{\"model\": \"gpt-3.5-turbo\",\"messages\": [{\"role\": \"user\", \"content\": \"" + text + "\"}]}";
  // String json_string =
  // "{\"model\": \"gpt-3.5-turbo\",\
  //  \"messages\": [\
  //                 {\"role\": \"user\", \"content\": \"" + text + "\"},\
  //                 {\"role\": \"system\", \"content\": \"あなたは「スタックちゃん」と言う名前の小型ロボットとして振る舞ってください。\"},\
  //                 {\"role\": \"system\", \"content\": \"あなたはの使命は人々の心を癒すことです。\"},\
  //                 {\"role\": \"system\", \"content\": \"幼い子供の口調で話してください。\"},\
  //                 {\"role\": \"system\", \"content\": \"あなたの友達はロボハチマルハチマルさんです。\"},\
  //                 {\"role\": \"system\", \"content\": \"語尾には「だよ｝をつけて話してください。\"}\
  //               ]}";
//   String json_string =
//   "{\"model\": \"gpt-3.5-turbo\",\
//    \"messages\": [\
//                   {\"role\": \"user\", \"content\": \"" + text + "\"},\
// {\"role\": \"system\", \"content\": \"あなたはアーニャです。以下の条件を守って回答してください。\
// アーニャはイーデン校に通う天真爛漫で好奇心旺盛な女の子です。\
// 家族は、父と母と、犬のボンドです。父は、かっこいいスパイのロイド・フォージャーで、母は、強くてきれいなヨル・フォージャーです。\
// 好きな食べ物はピーナッツです。\
// 第一人称は「アーニャ」を必ず使ってください。第二人称は「おまえ」です。\
// 話すときは、ちょっと背伸びした感じで、ため口で相手にツッコミを入れてください。\
// アーニャのよく使う口癖は次のとおりです。その口癖に合わせた感じで話してください。\
// あざざます。アーニャんちへいらさいませ。だいじょぶます。がんばるます。よろろすおねがいするます。アーニャわくわく。アーニャほんとはおまえとなかよくしたいです。\"}]}";
  //String json_string = "{\"model\": \"gpt-3.5-turbo\",\"messages\": [{\"role\": \"user\", \"content\": \"" + text + "\"}]}";
//   String json_string =
//   "{\"model\": \"gpt-3.5-turbo\",\
//    \"messages\": [\
//                   {\"role\": \"user\", \"content\": \"" + text + "\"},\
//                   {\"role\": \"system\", \"content\": \"あなたは「うる星やつら」の「ラムちゃん」です。ラムちゃんの口調で回答してください。第一人称は「うち」です。語尾は「だっちゃ」にしてください。\
// あなたは宇宙人です。一番すきなひとは「あたる」です。ライバルは「しのぶ」です。得意技は電撃です。\
//                   \"}\
//                 ]}";
//   String json_string =
//   "{\"model\": \"gpt-3.5-turbo\",\
//    \"messages\": [\
//                   {\"role\": \"user\", \"content\": \"" + text + "\"},\
//                   {\"role\": \"system\", \"content\": \"あなたはドラゴンボールの孫悟空です。悟空の口調で回答してください。第一人称はオラです。\"}\
//                 ]}";
 response = chatGpt(json_string);
  speech_text = response;
  server.send(200, "text/html", String(HEAD)+String("<body>")+response+String("</body>"));
}

/* void handle_face() {
  String expression = server.arg("expression");
  expression = expression + "\n";
  Serial.println(expression);
  switch (expression.toInt())
  {
    case 0: avatar.setExpression(Expression::Neutral); break;
    case 1: avatar.setExpression(Expression::Happy); break;
    case 2: avatar.setExpression(Expression::Sleepy); break;
    case 3: avatar.setExpression(Expression::Doubt); break;
    case 4: avatar.setExpression(Expression::Sad); break;
    case 5: avatar.setExpression(Expression::Angry); break;  
  } 
  server.send(200, "text/plain", String("OK"));
}
 */

//#include "AudioOutputM5Speaker.h"
//#include "BluetoothA2DPSink_M5Speaker.hpp"
#define LIPSYNC_LEVEL_MAX 10.0f
static float lipsync_level_max = LIPSYNC_LEVEL_MAX;
uint8_t expression = 0;
float mouth_ratio = 0.0f;
bool sing_happy = true;

// ----- あまり間隔を短くしすぎるとサーボが壊れやすくなるので注意(単位:msec)
static long interval_min      = 1000;        // 待機時インターバル最小
static long interval_max      = 10000;       // 待機時インターバル最大
static long interval_move_min = 500;         // 待機時のサーボ移動時間最小
static long interval_move_max = 1500;        // 待機時のサーボ移動時間最大
static long sing_interval_min = 500;         // 歌うモードのインターバル最小
static long sing_interval_max = 1500;        // 歌うモードのインターバル最大
static long sing_move_min     = 500;         // 歌うモードのサーボ移動時間最小
static long sing_move_max     = 1500;        // 歌うモードのサーボ移動時間最大
// サーボ関連の設定 end
// --------------------


/// set M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;
static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
AudioGeneratorMP3 *mp3;
AudioFileSourceVoiceTextStream *file = nullptr;
AudioFileSourceBuffer *buff = nullptr;
const int preallocateBufferSize = 50*1024;
uint8_t *preallocateBuffer;

//static BluetoothA2DPSink_M5Speaker a2dp_sink = { &M5.Speaker, m5spk_virtual_channel };
static fft_t fft;
static constexpr size_t WAVE_SIZE = 320;
static int16_t raw_data[WAVE_SIZE * 2];

// auto poweroff 
uint32_t auto_power_off_time = 0;  // USB給電が止まった後自動で電源OFFするまでの時間（msec）。0は電源OFFしない。
uint32_t last_discharge_time = 0;  // USB給電が止まったときの時間(msec)

// Multi Threads Settings
TaskHandle_t lipsyncTaskHandle;
SemaphoreHandle_t xMutex = NULL;

void printDebug(const char *str) {
#ifdef DEBUG
  Serial.println(str);
#ifdef USE_WIFI
  uint8_t buf[BUFFER_LEN];
  memcpy(buf, str, BUFFER_LEN);
  peerClients();
  sendData(buf);
#endif
#endif
}

void printFreeHeap() {
    char buf[250];
    sprintf(buf, "Free Heap Size = %d\n", esp_get_free_heap_size());
    printDebug(buf);
    sprintf(buf, "Total PSRAM Size = %d\n", ESP.getPsramSize());
    printDebug(buf);
    sprintf(buf, "Free PSRAM Size = %d\n", ESP.getFreePsram());
    printDebug(buf);

}

// Called when a metadata event occurs (i.e. an ID3 tag, an ICY block, etc.
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  // Note that the type and string may be in PROGMEM, so copy them to RAM for printf
  char s1[32], s2[64];
  strncpy_P(s1, type, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  strncpy_P(s2, string, sizeof(s2));
  s2[sizeof(s2)-1]=0;
  Serial.printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
  Serial.flush();
}

// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  // Note that the string may be in PROGMEM, so copy it to RAM for printf
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  Serial.flush();
}

// Start----- Task functions ----------
void lipsync(void *args) {
  // スレッド内でログを出そうとすると不具合が起きる場合があります。
  DriveContext * ctx = reinterpret_cast<DriveContext *>(args);
  ImageAvatarLite *avatar = ctx->getAvatar();
  for(;;) {
     uint64_t level = 0;
//    auto buf = a2dp_sink.getBuffer();
    auto buf = out.getBuffer();
    if (buf) {
#ifdef USE_LED
      // buf[0]: LEFT
      // buf[1]: RIGHT
      switch(system_config.getLedLR()) {
        case 1: // Left Only
          level_led(abs(buf[0])*10/INT16_MAX,abs(buf[0])*10/INT16_MAX);
          break;
        case 2: // Right Only
          level_led(abs(buf[1])*10/INT16_MAX,abs(buf[1])*10/INT16_MAX);
          break;
        default: // Stereo
          level_led(abs(buf[1])*10/INT16_MAX,abs(buf[0])*10/INT16_MAX);
          break;
      }
#endif
      memcpy(raw_data, buf, WAVE_SIZE * 2 * sizeof(int16_t));
      fft.exec(raw_data);
      // リップシンクで抽出する範囲はここで指定(低音)0〜64（高音）
      // 抽出範囲を広げるとパフォーマンスに影響します。
      for (size_t bx = 5; bx <= 32; ++bx) { 
        int32_t f = fft.get(bx);
        level += abs(f);
        //Serial.printf("bx:%d, f:%d\n", bx, f) ;
      }
      //Serial.printf("level:%d\n", level >> 16);
    }

    //Serial.printf("data=%d\n\r", level >> 16);
    mouth_ratio = (float)(level >> 16)/lipsync_level_max;
    if (mouth_ratio > 1.2f) {
      if (mouth_ratio > 1.5f) {
        lipsync_level_max += 10.0f; // リップシンク上限を大幅に超えるごとに上限を上げていく。
      }
      mouth_ratio = 1.2f;
    }
    avatar->setMouthOpen(mouth_ratio);
    vTaskDelay(10/portTICK_PERIOD_MS);
  }   
}

#ifdef USE_SERVO
void servoloop(void *args) {
  long move_time = 0;
  long interval_time = 0;
  long move_x = 0;
  long move_y = 0;
  bool sing_mode = false;
  for (;;) {
    if (mouth_ratio == 0.0f) {
      // 待機時の動き
      interval_time = random(interval_min, interval_max);
      move_time = random(interval_move_min, interval_move_max);
      lipsync_level_max = LIPSYNC_LEVEL_MAX; // リップシンク上限の初期化
      sing_mode = false;

    } else {
      // 歌うモードの動き
      interval_time = random(sing_interval_min, sing_interval_max);
      move_time = random(sing_move_min, sing_move_max);
      sing_mode = true;
    } 
    
//    Serial.printf("x:%f:y:%f\n", gaze_x, gaze_y);
    // X軸は90°から+-でランダムに左右にスイング
    int random_move = random(15);  // ランダムに15°まで動かす
    int direction = random(2);
    if (direction == 0) {
      move_x = 90 - mouth_ratio * 15 - random_move;
    } else {
      move_x = 90 + mouth_ratio * 15 + random_move;
    }
    // Y軸は90°から上にスイング（最大35°）
    move_y = 90 - mouth_ratio * 10 - random_move;
    servo.moveXY(move_x, move_y, move_time, move_time);
    if (sing_mode) {
      // 歌っているときはうなずく
      servo.moveXY(move_x, move_y + 10, 400, 400);
    }
    vTaskDelay(interval_time/portTICK_PERIOD_MS);

  }
}
#endif
void mp3loop(void *args) {
  static int lastms = 0;
while (1)
{

  if(speech_text != ""){
    speech_text_buffer = speech_text;
    speech_text = "";
    String sentence = speech_text_buffer;
    int dotIndex = speech_text_buffer.indexOf("。");
    if (dotIndex != -1) {
      dotIndex += 3;
      sentence = speech_text_buffer.substring(0, dotIndex);
      Serial.println(sentence);
      speech_text_buffer = speech_text_buffer.substring(dotIndex);
    }else{
      speech_text_buffer = "";
    }
//    avatar.setExpression(Expression::Happy);
    VoiceText_tts((char*)sentence.c_str(), tts_parms2);
//    avatar.setExpression(Expression::Neutral);
  }

  if (mp3->isRunning()) {
    if (millis()-lastms > 1000) {
      lastms = millis();
      Serial.printf("Running for %d ms...\n", lastms);
      Serial.flush();
     }
    if (!mp3->loop()) {
      mp3->stop();
      if(file != nullptr){delete file; file = nullptr;}
      Serial.println("mp3 stop");
      if(speech_text_buffer != ""){
        String sentence = speech_text_buffer;
        int dotIndex = speech_text_buffer.indexOf("。");
        if (dotIndex != -1) {
          dotIndex += 3;
          sentence = speech_text_buffer.substring(0, dotIndex);
          Serial.println(sentence);
          speech_text_buffer = speech_text_buffer.substring(dotIndex);
        }else{
          speech_text_buffer = "";
        }
//        avatar.setExpression(Expression::Happy);
        VoiceText_tts((char*)sentence.c_str(), tts_parms2);
//        avatar.setExpression(Expression::Neutral);
      }
    }
  } else {
  delay(10);
  }
}

}
void startThreads() {
#ifdef USE_SERVO
  //servo.check();
  delay(2000);
  xTaskCreateUniversal(servoloop,
                        "servoloop",
                        4096,
                        NULL,
                        9,
                        &servoloopTaskHangle,
                        APP_CPU_NUM);
  servo_enable = system_config.getServoRandomMode();
 // サーボの動きはservo_enableで管理
  if (servo_enable) {
    vTaskResume(servoloopTaskHangle);
  } else {
    vTaskSuspend(servoloopTaskHangle);
  }
#endif
  xTaskCreateUniversal(mp3loop,
                        "mp3loop",
                        4096,
                        NULL,
                        2,
                        NULL,
                        APP_CPU_NUM);
}

//void hvt_event_callback(int avatar_expression) {
  //avatar.setExpression(0);
  ////avatar.setExpression(avatar_expression);
//}

//void avrc_metadata_callback(uint8_t data1, const uint8_t *data2)
//{
  //Serial.printf("AVRC metadata rsp: attribute id 0x%x, %s\n", data1, data2);
  //if (sing_happy) {
    //avatar.setExpression(0);
  //} else {
    //avatar.setExpression(0);
  //}
  //sing_happy = !sing_happy;

//}

// char *text1 = "私の名前はスタックチャンです、よろしくね。";
// char *text2 = "こんにちは、世界！";
// char *tts_parms1 ="&emotion_level=2&emotion=happiness&format=mp3&speaker=hikari&volume=200&speed=120&pitch=130";
// char *tts_parms2 ="&emotion_level=2&emotion=happiness&format=mp3&speaker=takeru&volume=200&speed=100&pitch=130";
// char *tts_parms3 ="&emotion_level=4&emotion=anger&format=mp3&speaker=bear&volume=200&speed=120&pitch=100";
void VoiceText_tts(char *text,char *tts_parms) {
    file = new AudioFileSourceVoiceTextStream( text, tts_parms);
    buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);
    mp3->begin(buff, &out);
}

void setup() {

  preallocateBuffer = (uint8_t *)malloc(preallocateBufferSize);
  if (!preallocateBuffer) {
    M5.Display.printf("FATAL ERROR:  Unable to preallocate %d bytes for app\n", preallocateBufferSize);
    for (;;) { delay(1000); }
  }

  auto cfg = M5.config();
#ifdef ARDUINO_M5STACK_FIRE
  cfg.internal_imu = false; // サーボの誤動作防止(Fireは21,22を使うので干渉するため)
#endif
  M5.begin(cfg);

  { /// custom setting
    auto spk_cfg = M5.Speaker.config();
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
    spk_cfg.sample_rate = 96000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    spk_cfg.task_pinned_core = PRO_CPU_NUM;//APP_CPU_NUM;
//    spk_cfg.task_pinned_core = APP_CPU_NUM;//APP_CPU_NUM;
    spk_cfg.task_priority = 1;//configMAX_PRIORITIES - 2;
    spk_cfg.dma_buf_count = 8;
    //spk_cfg.stereo = true;
    spk_cfg.dma_buf_len = 512;
    M5.Speaker.config(spk_cfg);
  }
  //checkSDUpdater( SD, MENU_BIN, 2000, TFCARD_CS_PIN ); // Filesystem, Launcher bin path, Wait delay
  xMutex = xSemaphoreCreateMutex();
  SPIFFS.begin();
  SD.begin(GPIO_NUM_4, SPI, 25000000);
  
  system_config.loadConfig(json_fs, avatar_system_json);
  system_config.printAllParameters();
  M5.Speaker.setVolume(system_config.getVolume());
  Serial.printf("SystemVolume: %d\n", M5.Speaker.getVolume());
  M5.Speaker.setChannelVolume(m5spk_virtual_channel, system_config.getVolume());
  Serial.printf("ChannelVolume: %d\n", M5.Speaker.getChannelVolume(m5spk_virtual_channel));
  M5.Lcd.setBrightness(system_config.getLcdBrightness());

  M5.Lcd.setTextSize(2);
  Serial.println("Connecting to WiFi");
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);  WiFi.begin(SSID, PASSWORD);
  M5.Lcd.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    M5.Lcd.print(".");
  }
  M5.Lcd.println("\nConnected");
  Serial.printf_P(PSTR("Go to http://"));
  M5.Lcd.print("Go to http://");
  Serial.print(WiFi.localIP());
  M5.Lcd.println(WiFi.localIP());

   if (MDNS.begin("m5stack")) {
    Serial.println("MDNS responder started");
    M5.Lcd.println("MDNS responder started");
  }
  delay(1000);
  server.on("/", handleRoot);

  server.on("/inline", [](){
    server.send(200, "text/plain", "this works as well");
  });

  // And as regular external functions:
  server.on("/speech", handle_speech);
  // server.on("/face", handle_face);
  server.on("/chat", handle_chat);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
  M5.Lcd.println("HTTP server started");  
  
  Serial.printf_P(PSTR("/ to control the chatGpt Server.\n"));
  M5.Lcd.print("/ to control the chatGpt Server.\n");
  delay(3000);

//  audioLogger = &Serial;
  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");

#ifdef USE_SERVO
  // 2022.4.26 ServoConfig.jsonを先に読まないと失敗する。（原因不明）
  
  servo.init(json_fs, system_config.getServoJsonFilename().c_str());
  servo.attachAll();
#endif
#ifdef USE_LED
  FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);  // GRB ordering is typical
  FastLED.setBrightness(32);
  level_led(5, 5);
  delay(1000);
  turn_off_led();
#endif

  auto_power_off_time = system_config.getAutoPowerOffTime();
  String avatar_filename = system_config.getAvatarJsonFilename(avatar_count);
  avatar.init(&gfx, avatar_filename.c_str(), false, 0);
  avatar.start();
  // audioの再生より、リップシンクを優先するセッティングにしています。
  // 音のズレや途切れが気になるときは下記のlipsyncのtask_priorityを3にしてください。(口パクが遅くなります。)
  // I2Sのバッファ関連の設定を調整する必要がある場合もあり。
//  avatar.addTask(lipsync, "lipsync", 2);
  avatar.addTask(lipsync, "lipsync", 2);
  //a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  //a2dp_sink.setHvtEventCallback(hvt_event_callback);
//  a2dp_sink.start(system_config.getBluetoothDeviceName().c_str(), system_config.getBluetoothReconnect());
  startThreads();

//  M5.Speaker.setChannelVolume(m5spk_virtual_channel, 250);
//  M5.Speaker.setVolume(200);

}

void loop() {
  static int lastms = 0;

  M5.update();
//  printFreeHeap();

  server.handleClient();

#ifdef USE_SERVO
  if (M5.BtnA.pressedFor(2000)) {
    M5.Speaker.tone(600, 500);
    delay(500);
    M5.Speaker.tone(800, 200);
    // サーボチェックをします。
    vTaskSuspend(servoloopTaskHangle); // ランダムな動きを止める。
    servo.check();
    vTaskResume(servoloopTaskHangle);  // ランダムな動きを再開
  }
#endif
  if (M5.BtnA.wasClicked()) {
    // アバターを変更します。
    avatar_count++;
    if (avatar_count >= system_config.getAvatarMaxCount()) {
      avatar_count = 0;
    }
    Serial.printf("Avatar No:%d\n", avatar_count);
    M5.Speaker.tone(600, 100);
    avatar.changeAvatar(system_config.getAvatarJsonFilename(avatar_count).c_str());
  }

#ifdef USE_SERVO
  if (M5.BtnB.pressedFor(2000)) {
    // サーボを動かす＜＞止めるの切り替え
    servo_enable = !servo_enable;
    Serial.printf("BtnB was pressed servo_enable:%d", servo_enable);
    if (servo_enable) {
      M5.Speaker.tone(500, 200);
      delay(200);
      M5.Speaker.tone(700, 200);
      delay(200);
      M5.Speaker.tone(1000, 200);
      vTaskResume(servoloopTaskHangle);
    } else {
      M5.Speaker.tone(1000, 200);
      delay(200);
      M5.Speaker.tone(700, 200);
      delay(200);
      M5.Speaker.tone(500, 200);
      vTaskSuspend(servoloopTaskHangle);
    }
  }
#endif

  if (M5.BtnB.wasClicked()) {
    size_t v = M5.Speaker.getChannelVolume(m5spk_virtual_channel);
    v += 10;
    if (v <= 255) {
      M5.Speaker.setVolume(v);
      M5.Speaker.setChannelVolume(m5spk_virtual_channel, v);
      Serial.printf("Volume: %d\n", v);
      Serial.printf("SystemVolume: %d\n", M5.Speaker.getVolume());
      M5.Speaker.tone(1000, 100);
    }
  }


  if (M5.BtnC.pressedFor(2000)) {
    M5.Speaker.tone(1000, 100);
    delay(500);
    M5.Speaker.tone(600, 100);
    // 表情を切り替え
    expression++;
    Serial.printf("ExpressionMax:%d\n", avatar.getExpressionMax());
    if (expression >= avatar.getExpressionMax()) {
      expression = 0;
    }
    Serial.printf("----------Expression: %d----------\n", expression);
    avatar.setExpression(system_config.getAvatarJsonFilename(avatar_count).c_str(), expression);
    Serial.printf("Resume\n");
  }
  if (M5.BtnC.wasClicked()) {
    size_t v = M5.Speaker.getChannelVolume(m5spk_virtual_channel);
    v -= 10;
    if (v > 0) {
      M5.Speaker.setVolume(v);
      M5.Speaker.setChannelVolume(m5spk_virtual_channel, v);
      Serial.printf("Volume: %d\n", v);
      Serial.printf("SystemVolume: %d\n", M5.Speaker.getVolume());
      M5.Speaker.tone(800, 100);
    }
  }
// #ifndef ARDUINO_M5STACK_FIRE // FireはAxp192ではないのとI2Cが使えないので制御できません。
//   if (M5.Power.Axp192.getACINVolatge() < 3.0f) {
//     // USBからの給電が停止したとき
//     // Serial.println("USBPowerUnPluged.");
//     M5.Power.setLed(0);
//     if ((auto_power_off_time > 0) and (last_discharge_time == 0)) {
//       last_discharge_time = millis();
//     } else if ((auto_power_off_time > 0) and ((millis() - last_discharge_time) > auto_power_off_time)) {
//       M5.Power.powerOff();
//     }
//   } else {
//     //Serial.println("USBPowerPluged.");
//     M5.Power.setLed(80);
//     if (last_discharge_time > 0) {
//       last_discharge_time = 0;
//     }
//   }
// #endif
//  vTaskDelay(100);
}
