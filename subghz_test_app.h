#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <gui/elements.h>

#include "subghz_test_app_icons.h"

struct SubghzTestApp {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    InputEvent event;
    FuriMutex* mutex;
    int base_key;
    uint key;
    int pager_number;
    int sending;
    int key_segment_number;
};

typedef struct SubghzTestApp SubghzTestApp;