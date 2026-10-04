#pragma once

#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

enum class AdminMessageHandleResult { NOT_HANDLED = 0, HANDLED = 1, HANDLED_WITH_RESPONSE = 2 };
