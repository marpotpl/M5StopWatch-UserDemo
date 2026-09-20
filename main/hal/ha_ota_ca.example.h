#pragma once

// Copy this file to ha_ota_ca.h (ignored by Git) and replace the empty value
// with the PEM certificate of the dedicated local OTA CA. Never put a private
// key here. The OTA server certificate must contain the Mac's LAN IPv4 address
// in subjectAltName.
#define HA_OTA_CA_PEM ""
