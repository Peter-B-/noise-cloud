#ifndef UTILITY_H
#define UTILITY_H

void parseTwinMessage(DEVICE_TWIN_UPDATE_STATE, const char *);
void readMessage(int, char *, uint16_t* noiseData, int noiseDataSize);

void SensorInit(void);

void blinkLED(void);
void blinkSendConfirmation(void);
int getInterval(void);

#endif /* UTILITY_H */
