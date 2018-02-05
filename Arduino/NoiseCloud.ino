#include "Arduino.h"
#include "OledDisplay.h"
#include "RingBuffer.h"
#include "AudioClassV2.h"

static AudioClass& Audio = AudioClass::getInstance();
static int AUDIO_SIZE = 32000 * 3 + 45;

char readBuffer[AUDIO_CHUNK_SIZE];
int lastButtonAState;
int buttonAState;
int lastButtonBState;
int buttonBState;

void setup(void)
{
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);

  Serial.println("Helloworld in Azure IoT DevKits!");

  // initialize the button pin as a input
  pinMode(USER_BUTTON_A, INPUT);
  lastButtonAState = digitalRead(USER_BUTTON_A);
  pinMode(USER_BUTTON_B, INPUT);
  lastButtonBState = digitalRead(USER_BUTTON_B);

  

  printIdleMessage();

  StartRecord();
}

void loop(void)
{
  buttonAState = digitalRead(USER_BUTTON_A);
  buttonBState = digitalRead(USER_BUTTON_B);
    
  if (buttonAState == LOW && lastButtonAState == HIGH)
  {
    Serial.println("A pressed");
 
    //StartRecord();
  }

  if (buttonBState == LOW && lastButtonBState == HIGH)
  {
    Serial.println("B pressed");
 
    //StopRecord();
  }

  lastButtonAState = buttonAState;
  lastButtonBState = buttonBState;

  delay(20);
}

void printIdleMessage()
{
  Screen.clean();
  Screen.print(0, "AZ3166 Audio  ");

}

bool firstRecord;
void StartRecord()
{
  delay(500);
  Serial.println("start recording");
  firstRecord = true;
  Audio.format(8000, 16);
  Audio.startRecord(recordCallback);
}


void recordCallback(void)
{
  if (firstRecord){
    firstRecord = false;
    return;
  }

  int length = Audio.readFromRecordBuffer(readBuffer, AUDIO_CHUNK_SIZE);
  CalcLoudness(length);
}

float M = 0.0;
float S = 0.0;
int k = 1;
int block = 0;

void CalcLoudness(int length)
{
  for(int i = 0; i < length; i=i+2)
  {
    short value =   (short)readBuffer[i] | (short)readBuffer[i+1] << 8;
    float tmpM = M;
    M += (value - tmpM) / k;
    S += (value - tmpM) * (value - M);
    k++;
  }

  block++;
  if (block == 16)
  {
    float sd = log10(sqrt(S / (k-2)));
    float smoothed = Smooth(sd);

    PrintLoudness(sd, smoothed);

    M = 0.0;
    S = 0.0;
    k = 1;
    block = 0;
  }
}

float smoothAlpha = 0.1;
float smoothValue = 0;

float Smooth(float value)
{
  smoothValue = (smoothAlpha * value) + ((1-smoothAlpha) * smoothValue);
  return smoothValue;
}


void PrintLoudness(float sd, float smoothed)
{
  char buf[100];
  sprintf(buf, "%7.1f", sd);
  Screen.print(2, buf);

  sprintf(buf, "%7.1f", smoothed);
  Screen.print(3, buf);
}
