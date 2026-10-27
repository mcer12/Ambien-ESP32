#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>

// Restarts the SCD4x measurement: its settings can only be read while it is idle
String diagnosticsHtml();

#endif // DIAGNOSTICS_H
