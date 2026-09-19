#ifndef GEYLCA_MQTT_SECURITY_H
#define GEYLCA_MQTT_SECURITY_H

#include <Arduino.h>

String calcularHMAC(String payload, String secret);
bool checkRateLimit(String clientId);
void resetRateLimit();
int roleRank(String role);
bool hasRolePermission(String requiredRole, String userRole);

#endif
