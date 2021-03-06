#include "Arduino.h"
#include "OledDisplay.h"
#include "RingBuffer.h"
#include "AudioClassV2.h"

#include "AZ3166WiFi.h"
#include "AzureIotHub.h"
#include "DevKitMQTTClient.h"

#include "config.h"
#include "utility.h"

static AudioClass &Audio = AudioClass::getInstance();
static int AUDIO_SIZE = 32000 * 3 + 45;

char readBuffer[AUDIO_CHUNK_SIZE];
int lastButtonAState;
int buttonAState;
int lastButtonBState;
int buttonBState;
static bool hasWifi = false;
static bool messageSending = true;
static bool showValues = true;

uint16_t noiseBuffer[NOISEBUFFER_SIZE];
uint16_t noiseBufferSend[NOISEBUFFER_SIZE];

static uint64_t lastSendTime;
static uint64_t displayValuesTime;

float noiseRms = 0.0;
float noiseLog = 0.0;
float noiseSmoothed = 0.0;

void setup(void)
{
  Screen.init();
  PrintTitle();
  Screen.print(1, "Init");

  Screen.print(2, " > Serial");
  Serial.begin(115200);
  Serial.println("Noise Cloud starting");

  hasWifi = false;
  InitWifi();
  if (!hasWifi)
    return;

  Screen.print(2, " > Sensors");
  SensorInit();

  Screen.print(2, " > IoT Hub");
  DevKitMQTTClient_Init(true);

  DevKitMQTTClient_SetSendConfirmationCallback(SendConfirmationCallback);
  DevKitMQTTClient_SetMessageCallback(MessageCallback);
  DevKitMQTTClient_SetDeviceTwinCallback(DeviceTwinCallback);
  DevKitMQTTClient_SetDeviceMethodCallback(DeviceMethodCallback);

  Screen.print(2, " > IO Pins");
  pinMode(USER_BUTTON_A, INPUT);
  pinMode(USER_BUTTON_B, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_AZURE, OUTPUT);
  pinMode(LED_USER, OUTPUT);

  lastButtonAState = digitalRead(USER_BUTTON_A);
  lastButtonBState = digitalRead(USER_BUTTON_B);

  lastSendTime = SystemTickCounterRead();
  displayValuesTime = SystemTickCounterRead();

  memset(noiseBuffer, 0x0, sizeof(noiseBuffer));
  memset(noiseBufferSend, 0x0, sizeof(noiseBufferSend));

  StartRecord();
}

uint loopCount = 0;
void loop(void)
{
  loopCount++;
  buttonAState = digitalRead(USER_BUTTON_A);
  buttonBState = digitalRead(USER_BUTTON_B);

  if (buttonAState == LOW && lastButtonAState == HIGH)
  {
    Serial.println("A pressed");
    displayValuesTime = SystemTickCounterRead();
    PrintTitle();
    showValues = true;
  }

  if (buttonBState == LOW && lastButtonBState == HIGH)
  {
    Serial.println("B pressed");
  }

  lastButtonAState = buttonAState;
  lastButtonBState = buttonBState;

  if (showValues)
    if ((int)(SystemTickCounterRead() - displayValuesTime) >= 30000)
    {
      showValues = false;
      delay(10);
      Screen.clean();
    }
    else
    {
      PrintNoiseData(loopCount % 20);
    }

  if (hasWifi)
  {
    if (messageSending &&
        (int)(SystemTickCounterRead() - lastSendTime) >= getInterval())
    {
      SendDataToCloud();
    }

    DevKitMQTTClient_Check();
  }

  delay(10);
}

void SendDataToCloud()
{
  memcpy(noiseBufferSend, noiseBuffer, sizeof(noiseBuffer));
  memset(noiseBuffer, 0x0, sizeof(noiseBuffer));

  float temperature = readTemperature();
  float humidity = readHumidity();

  Serial.println("Sending data");
  char messagePayload[MESSAGE_MAX_LEN];

  serializeMessage(messagePayload, noiseBufferSend, NOISEBUFFER_SIZE, temperature, humidity);
  EVENT_INSTANCE *message = DevKitMQTTClient_Event_Generate(messagePayload, MESSAGE);
  DevKitMQTTClient_SendEventInstance(message);

  lastSendTime = SystemTickCounterRead();
}

static void InitWifi()
{
  Screen.print(2, " > WiFi");
  if (WiFi.begin() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    Screen.print(3, ip.get_address());
    Serial.print("Connected to wifi ");
    Serial.print(WiFi.SSID());
    Serial.print(": ");
    Serial.println(ip.get_address());

    hasWifi = true;
  }
  else
  {
    hasWifi = false;
    Screen.print(3, "No WiFi");
    Serial.println("No WiFi");
  }
}

void PrintTitle()
{
  Screen.clean();
  Screen.print(0, "  Noise Cloud");
}

bool firstRecord;
void StartRecord()
{
  Serial.println("start recording");
  firstRecord = true;
  Audio.format(8000, 16);
  Audio.startRecord(recordCallback);
}

void recordCallback(void)
{
  if (firstRecord)
  {
    firstRecord = false;
    return;
  }

  int length = Audio.readFromRecordBuffer(readBuffer, AUDIO_CHUNK_SIZE);
  CalcLoudness(length);
}

float squareSum = 0.0;
int k = 1;
int block = 0;

void CalcLoudness(int length)
{
  for (int i = 0; i < length; i = i + 2)
  {
    short value = (short)readBuffer[i] | (short)readBuffer[i + 1] << 8;

    squareSum += value * value;
    k++;
  }

  block++;
  if (block == 16)
  {
    noiseRms = sqrt(squareSum / k);
    noiseLog = log10(noiseRms);
    UpdateSmoothed(noiseLog);

    squareSum = 0;
    k = 1;
    block = 0;

    int idx = (int)(noiseLog * 10);
    idx = min(max(idx, 0), NOISEBUFFER_SIZE);
    noiseBuffer[idx]++;
  }
}

void UpdateSmoothed(float value)
{
  float smoothAlpha = 0.1;
  noiseSmoothed = (smoothAlpha * value) + ((1 - smoothAlpha) * noiseSmoothed);
}

char buf[100];
void PrintNoiseData(int i)
{
  if (i == 0)
  {
    sprintf(buf, "%7.1f", noiseLog);
    Screen.print(2, buf);
  }
  if (i == 1)
  {
    sprintf(buf, "%7.1f", noiseSmoothed);
    Screen.print(3, buf);
  }
}

static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result)
{
  if (result == IOTHUB_CLIENT_CONFIRMATION_OK)
  {
    blinkSendConfirmation();
  }
}

static void MessageCallback(const char *payLoad, int size)
{
  blinkLED();
  Screen.print(1, payLoad, true);
}

static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payLoad, int size)
{
  char *temp = (char *)malloc(size + 1);
  if (temp == NULL)
  {
    return;
  }
  memcpy(temp, payLoad, size);
  temp[size] = '\0';
  parseTwinMessage(updateState, temp);
  free(temp);
}

static int DeviceMethodCallback(const char *methodName, const unsigned char *payload, int size, unsigned char **response, int *response_size)
{
  LogInfo("Try to invoke method %s", methodName);
  const char *responseMessage = "\"Successfully invoke device method\"";
  int result = 200;

  if (strcmp(methodName, "start") == 0)
  {
    LogInfo("Start sending temperature and humidity data");
    messageSending = true;
  }
  else if (strcmp(methodName, "stop") == 0)
  {
    LogInfo("Stop sending temperature and humidity data");
    messageSending = false;
  }
  else
  {
    LogInfo("No method %s found", methodName);
    responseMessage = "\"No method found\"";
    result = 404;
  }

  *response_size = strlen(responseMessage);
  *response = (unsigned char *)malloc(*response_size);
  strncpy((char *)(*response), responseMessage, *response_size);

  return result;
}