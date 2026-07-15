#pragma once
#include "esp_err.h"

/* Starts the MCP Streamable HTTP endpoint at http://<ip>/mcp */
esp_err_t mcp_server_start(void);
