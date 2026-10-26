#include "SmartPlugShared.h"

bool readPIR() {
  return digitalRead(PIR_PIN);
}

void processPIR() {

  unsigned long now = millis();

  if (now - lastPirSample < PIR_SAMPLE_INTERVAL) return;

  lastPirSample = now;

  bool pir = readPIR();

  // New PIR trigger
  if (pir && !lastPirState) {

    Serial.println("[PIR] Motion detected");

    if (detectionDelaySeconds == 0) {

      humanDetected = true;
      lastHumanTime = now;
      detectionPending = false;

      Serial.println("[PIR] Detection accepted");

    } else {

      detectionPending = true;
      detectionStartTime = now;

      Serial.println("[PIR] Detection delay started");
    }
  }

  // Detection delay cancelled if PIR goes low
  if (detectionPending && !pir) {

    detectionPending = false;

    Serial.println("[PIR] Detection delay cancelled");
  }

  // Detection delay complete
  if (detectionPending && pir &&
      now - detectionStartTime >= detectionDelaySeconds * 1000UL) {

    detectionPending = false;
    humanDetected = true;
    lastHumanTime = now;

    Serial.println("[PIR] Detection accepted");
  }

  // Keep the human timer fresh while motion continues
  if (pir && humanDetected) {
    lastHumanTime = now;
  }

  // Human timeout
  if (!pir && humanDetected) {

    if (humanOnSeconds > 0 && lastHumanTime > 0 &&
        now - lastHumanTime >= humanOnSeconds * 1000UL) {

      humanDetected = false;

      Serial.println("[PIR] Human timeout");
    }
  }

  lastPirState = pir;
}

void calibrateACS712() {

  Serial.println();
  Serial.println("[ACS712] Checking current sensor...");
  Serial.println("[ACS712] Ensure no current is flowing");

  delay(1000);

  const int samples = 500;
  double sum = 0;

  for (int i = 0; i < samples; i++) {

    uint32_t mv = analogReadMilliVolts(ACS712_PIN);
    float adcVoltage = mv / 1000.0f;
    float sensorVoltage = adcVoltage / ADC_DIVIDER_RATIO;

    sum += sensorVoltage;
    delay(2);
  }

  acsZeroVoltage = sum / samples;

  Serial.print("[ACS712] Zero voltage: ");
  Serial.print(acsZeroVoltage, 4);
  Serial.println(" V");

  // Healthy zero-current output is about VCC / 2 (~2.5 V).
  if (acsZeroVoltage >= 2.0f && acsZeroVoltage <= 3.0f) {

    acsSensorPresent = true;
    Serial.println("[ACS712] SENSOR PRESENT");

  } else {

    acsSensorPresent = false;
    currentAmps = 0.0f;
    estimatedPowerWatts = 0.0f;

    Serial.println("[ACS712] SENSOR NOT DETECTED");
    Serial.println("[ACS712] Current monitor disabled");
  }
}

float readCurrent() {

  if (!acsSensorPresent) return 0.0f;

  // About 100 ms (five 50 Hz cycles). AC RMS is computed from the
  // variation around the measured mean.
  const int samples = 400;
  double sumVoltage = 0.0;
  double sumVoltageSquared = 0.0;

  for (int i = 0; i < samples; i++) {

    const uint32_t mv = analogReadMilliVolts(ACS712_PIN);
    const float adcVoltage = mv / 1000.0f;
    const float sensorVoltage = adcVoltage / ADC_DIVIDER_RATIO;

    sumVoltage += sensorVoltage;
    sumVoltageSquared += (double)sensorVoltage * sensorVoltage;

    delayMicroseconds(250);
  }

  const double meanVoltage = sumVoltage / samples;
  double variance = (sumVoltageSquared / samples) - (meanVoltage * meanVoltage);
  if (variance < 0.0) variance = 0.0;

  float currentRms = sqrt(variance) / ACS712_SENSITIVITY;

  if (currentRms < CURRENT_DEADBAND) {
    currentRms = 0.0f;
  }

  return currentRms;
}

void currentTask(void* param) {
  for(;;) {
    float amps=acsSensorPresent?readCurrent():0.0f;
    CurrentReading reading{amps,fabsf(amps)*NOMINAL_VOLTAGE};
    if(currentReadingsQueue) xQueueOverwrite(currentReadingsQueue,&reading);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void processCurrentSamples() {
  CurrentReading reading;
  if(currentReadingsQueue && xQueueReceive(currentReadingsQueue,&reading,0)==pdTRUE) {
    currentAmps=reading.amps;
    estimatedPowerWatts=reading.apparentVA;
  }
}
