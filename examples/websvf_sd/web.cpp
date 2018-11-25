#include "web.h"
#include "storage.h"
#include "jtag.h"
#include "hardware.h"

char http_username[32] = "admin";
char http_password[32] = "admin";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

// command response to user typing (websocket)
String CommandLine(String user)
{
  char jtag_id[100];
  jtag.scan();
  sprintf(jtag_id, "0x%08X", jtag.id());
  if(user == "p")
  {
    spiffs_program_activate = 1; // fixed filename
  }
  else
  if(user.length() > 4)
  {
    sd_program_activate = 1;
    sd_file_name_svf = user;
  }
  return String(jtag_id) + " CommandLine typed: " + user;
}


void handle_dir(AsyncWebServerRequest *request)
{
    AsyncResponseStream *response = request->beginResponseStream("text/json");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
          DirPath=p->value();
    }
    init_oled_show_ip();
    mount_read_directory();
    root["path"]=DirPath;
    int ndirs = 0;
    JsonArray& data = root.createNestedArray("files");
    for(int i = 0; i < DirN; i++)
    {
      if(DirEntries[i].type) ndirs++; // count how many are dirs before files
      data.add(DirEntries[i].name);
    }
    root["ndirs"]=String(ndirs);
    root.printTo(*response);
    request->send(response);
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  digitalWrite(LED_WIFI, HIGH);
  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        // client->text("I got your text message in single frame");
        client->text(CommandLine(msg));
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            // client->text("I got your text message in multiple frames");
            client->text(CommandLine(msg));
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
  digitalWrite(LED_WIFI, LOW);
}


void web_server_init()
{
  MDNS.addService("http","tcp",80);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);

  #ifdef ESP8266
  server.addHandler(new SPIFFSEditor(http_username,http_password));
  #endif
  #ifdef ESP32
  server.addHandler(new SPIFFSEditor(SPIFFS,http_username,http_password));
  #endif

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  // http://192.168.4.1/dir?path=/path/to/directory
  server.on("/dir", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("text/json");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
          DirPath=p->value();
    }
    init_oled_show_ip();
    mount_read_directory();
    root["path"]=DirPath;
    int ndirs = 0;
    JsonArray& data = root.createNestedArray("files");
    for(int i = 0; i < DirN; i++)
    {
      if(DirEntries[i].type) ndirs++; // count how many are dirs before files
      data.add(DirEntries[i].name);
    }
    root["ndirs"]=String(ndirs);
    root.printTo(*response);
    request->send(response);
  });

  // http://192.168.4.1/svf?path=/path/to/sd_card/file.svf
  server.on("/svf", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          sd_file_name_svf = p->value();
          sd_program_activate = 1;
        }
    }
    // currently web interface doesn't know did it actually succeed
    request->send(200, "text/plain", "requested");
  });

  // http://192.168.4.1/bin?path=/path/to/sd_card/file.bin
  server.on("/bin", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          sd_file_name_svf = sd_file_name_f32c_svf;
          sd_file_name_bin = p->value();
          sd_binary_file_activate = 1;
        }
    }
    // currently web interface doesn't know did it actually succeed
    request->send(200, "text/plain", "requested");
  });


  // http://192.168.4.1/mkdir?path=/path/to/new_directory
  server.on("/mkdir", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount()>=0)
          {
            if(SD.mkdir(p->value()))
            {
              isuccess = 1;
              // todo: switch to parent directory (path go up one level)
            }
            sd_unmount();
          }
        }
    }
    if(isuccess)
    {
      init_oled_show_ip();
      mount_read_directory();
    }
    String message = isuccess ? "success" : "fail";
    request->send(200, "text/plain", message);
  });

  // http://192.168.4.1/rmdir?path=/path/to/junk_directory
  // directory should be empty to be removed
  server.on("/rmdir", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount()>=0)
          {
            if(SD.rmdir(p->value()))
            {
              isuccess = 1;
              // todo: switch to parent directory (path go up one level)
            }
            sd_unmount();
          }
        }
    }
    if(isuccess)
    {
      init_oled_show_ip();
      mount_read_directory();
    }
    String message = isuccess ? "success" : "fail";
    request->send(200, "text/plain", message);
  });

  // http://192.168.4.1/rm?path=/path/to/junk.file
  server.on("/rm", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount()>=0)
          {
            if(SD.remove(p->value()))
            {
              isuccess = 1;
            }
            sd_unmount();
          }
        }
    }
    if(isuccess)
    {
      init_oled_show_ip();
      mount_read_directory();
    }
    String message = isuccess ? "success" : "fail";
    request->send(200, "text/plain", message);
  });

  // Download File from SD card
  // http://192.168.4.1/dl?path=/path/to/download.file
  server.on("/dl", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount() >= 0)
          {
            AsyncWebServerResponse *response = request->beginResponse(SD, p->value(), String(), true);
            request->send(response);
            // sd_unmount(); // can't immediately unmount - file will be sent later
          }
          else
          {
            request->send(200, "text/plain", "SD card mount failed");
          }
        }
    }
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    request->send(404);
  });

  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    static int packet_counter = 0;
    static int out_of_order = 0;
    static int write_error = 0;
    static size_t expect_index = 0;
    static char report[256];
    static File save_file;
    if(!index)
    {
      Serial.printf("UploadStart: %s\n", filename.c_str());
      packet_counter=0;
      expect_index = index; // for out-of-order detection
      out_of_order = 0;
      write_error = 0;
      report[0] = '\0';
      digitalWrite(LED_WIFI, HIGH);
#if 1
      int params = request->params();
      // Serial.printf("request params %d\n", params);
      String save_file_name = "";
      for(int i=0;i<params;i++)
      {
        AsyncWebParameter* p = request->getParam(i);
        if(p->isFile()){
          //Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
        } else if(p->isPost()){
          //Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        } else {
          //Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
          if(p->name() == "path") // directory path specified -> no jtag upload but save to SD
          {
            if(p->value())
            {
              Serial.printf("save %s to directory %s\n", filename.c_str(), p->value().c_str());
              save_dirname = p->value();
              if(!save_dirname.startsWith("/"))
                save_dirname = "/"+save_dirname;
              if(!save_dirname.endsWith("/"))
                save_dirname = save_dirname+"/";
              save_file_name = save_dirname+filename;
              // we want later to cd to this directory
              // if not root, we must cut off trailing "/"
              if(save_dirname != "/")
                while(save_dirname.endsWith("/"))
                  save_dirname = save_dirname.substring(0,save_dirname.length()-1);
            }
          }
        }
      }
      if(save_file_name != "" && save_file_name != "/")
      {
        if(sd_mount() >= 0)
        {
          save_file = SD.open(save_file_name.c_str(), FILE_WRITE);
          if(save_file)
            Serial.printf("file writing to %s starts\n", save_file_name.c_str());
          else
          {
            Serial.printf("file writing to: %s failed\n", save_file_name.c_str());
            sd_unmount();
          }
        }
        else
          Serial.printf("SD card mount failed\n");
      }
#endif
    }
    #if 0
      Serial.printf("%s", (const char*)data);
      if((packet_counter % 100) == 0 || packet_counter < 4 || final != 0)
        Serial.printf("packet %d len=%d\n", packet_counter, len); // the content
      packet_counter++;
    #endif
    if(index != expect_index)
      out_of_order++;
    expect_index = index + len;
    if(out_of_order == 0)
    {
      if(save_file)
      {
        if(len != save_file.write(data, len))
          write_error++;
      }
      else
        jtag.play_svf_packet(index, data, len, final, report);
    }
    if(final)
    {
      if(save_file) // FIXME: track also write error (disk full)
      {
        if(write_error == 0)
          strcpy(report,"success");
        else
          strcpy(report,"fail"); // disk full probably
      }
      if(out_of_order != 0)
        request->send(200, "text/plain", "received" + String(out_of_order) + " out-of-order packets");
      else
        request->send(200, "text/plain", report);
      if(save_file)
      {
        save_file.close();
        #if 0
        sd_unmount();
        // disabled because the same here will be done
        // when web page reloads
        // then it will init OLED to read directory content
        init_oled_show_ip();
        // cd to the uploaded file's directory
        DirPath=save_dirname;
        mount_read_directory();
        #endif
      }
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
      digitalWrite(LED_WIFI, LOW);
    }
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });

  server.begin();

}
