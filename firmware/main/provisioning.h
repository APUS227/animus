#pragma once
#include <stdbool.h>
#include <stddef.h>

/*
 * Provisioning owns the answer to one question: "which WiFi should this
 * device join?" — the same way safety.c owns GPIO. Credential sources,
 * in priority order:
 *
 *   1. NVS (saved by the setup portal)
 *   2. Kconfig (developer fallback; leave empty for end-user builds)
 *   3. none -> the setup portal takes over
 */

/* Fills ssid/pass and returns true if credentials are available. */
bool provisioning_get_credentials(char *ssid, size_t ssid_len,
                                  char *pass, size_t pass_len);

/* Starts the captive setup portal (SoftAP "animus-setup" + DNS catch-all
 * + config page at http://192.168.4.1/). Never returns — the device
 * reboots after the user saves their network. */
void provisioning_start_portal(void) __attribute__((noreturn));

/* Flags the next boot to enter the setup portal, then reboots. Called
 * when joining the saved network keeps failing. */
void provisioning_request_portal_and_reboot(void) __attribute__((noreturn));
