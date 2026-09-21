#pragma once

namespace ha_tesla_rest {

// The REST request runs in its own task, never in the WebSocket callback/task.
bool init();
bool request_initial();

}  // namespace ha_tesla_rest
