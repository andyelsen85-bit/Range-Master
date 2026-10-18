#pragma once
#include "portal_model.h"
/** Validate the whole response before publishing, retaining the old view on any error. */
bool portal_queue_parse(const char *json,PortalQueueSnapshot *out);
