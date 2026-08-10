#include "/home/xingxinliao/lab/cJSON/cJSON.h"
#include <stdio.h>
#include"send.h"
#include"json.h"
#include"/home/xingxinliao/lab/cJSON/cJSON.h"


 struct msgbuf msg2;


int parse_json_data(const char *json_string) {

  if (json_string == NULL) {
    return -1;
  }
  cJSON *root = cJSON_Parse(json_string);
  if (root == NULL) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      fprintf(stderr, "JSON Parse Error before: %s\n", error_ptr);
    }
    return -1;
  }
  cJSON *Temperature = cJSON_GetObjectItemCaseSensitive(root, "Temperature");
  if (cJSON_IsNumber(Temperature)) {
    msg2.text.temperature = Temperature->valuedouble;
  } else {
    printf("Temperature is missing or not a number.\n");
  }

  cJSON *Humidity = cJSON_GetObjectItemCaseSensitive(root, "Humidity");
  if (cJSON_IsNumber(Humidity)) {
    msg2.text.humidity = Humidity->valueint;
  } else {
    printf("Humidity is missing or not a number.\n");
  }

  cJSON *Light = cJSON_GetObjectItemCaseSensitive(root, "Light");
  if (cJSON_IsNumber(Light)) {
    msg2.text.light = Light->valueint;
  } else {
    printf("Light is missing or not a number.\n");
  }

  cJSON *co2 = cJSON_GetObjectItemCaseSensitive(root, "CO₂");
  if (cJSON_IsNumber(co2)) {
    msg2.text.CO2 = co2->valueint;
  } else {
    printf("CO₂ is missing or not a number.\n");
  }
   msg2.type =1;

  cJSON_Delete(root);
  return 0;
}

