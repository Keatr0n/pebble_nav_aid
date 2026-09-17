// Enough of the Pebble SDK to compile the pure-logic parts of the app on the
// host. Only the types and constants nav.h and compass.c actually name.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct { int16_t x, y; } GPoint;
typedef struct { GPoint origin; struct { int16_t w, h; } size; } GRect;
typedef uint8_t GColor;
typedef struct GContext GContext;
typedef struct Layer Layer;
typedef uint8_t GTextAlignment;

#define TRIG_MAX_ANGLE 0x10000
#define DEG_TO_TRIGANGLE(d) (((d) * TRIG_MAX_ANGLE) / 360)

// Ordering matters: compass_usable() is a >= test, and Unavailable sorts
// below everything precisely so that test excludes it.
typedef enum {
  CompassStatusUnavailable = -1,
  CompassStatusDataInvalid = 0,
  CompassStatusCalibrating,
  CompassStatusCalibrated
} CompassStatus;

typedef struct {
  int32_t magnetic_heading;
  int32_t true_heading;
  CompassStatus compass_status;
  bool is_declination_valid;
} CompassHeadingData;

typedef void (*CompassHeadingHandler)(CompassHeadingData heading);

void compass_service_set_heading_filter(int32_t filter);
void compass_service_subscribe(CompassHeadingHandler handler);
void compass_service_unsubscribe(void);

// Set by the test rather than the firmware.
bool clock_is_24h_style(void);
