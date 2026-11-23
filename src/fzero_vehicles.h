#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool FzeroVehiclesActive(void);
bool FzeroVehiclesLoad(const uint8_t *stock, size_t size, char *error,
                       size_t cap);
bool FzeroVehiclesPrepare(uint8_t **rom, size_t *size, char *error, size_t cap);
void FzeroVehiclesSync(void);
void FzeroVehiclesLoaded(void);
void FzeroVehiclesInstallHooks(void);
const char *FzeroVehicleIdentity(void);
const char *FzeroVehicleRecordIdentity(unsigned index);
const char *FzeroVehicleRecordName(unsigned index);
bool FzeroVehicleRecordIcon(unsigned index, uint32_t pixels[16 * 16]);
unsigned FzeroVehicleCount(void);
unsigned FzeroVehicleSelected(void);
unsigned FzeroVehicleGhostSource(void);
void FzeroVehicleGhostCycle(bool previous);
unsigned FzeroVehicleAcceleration(unsigned slot, unsigned speed);
unsigned FzeroVehicleTurn(unsigned speed);
struct Ppu;
void FzeroVehiclesRaster(struct Ppu *ppu, const uint8_t *ram, unsigned line);
void FzeroVehiclesBeginFrame(struct Ppu *ppu, const uint8_t *ram);
void FzeroVehiclesEndFrame(struct Ppu *ppu);

/* Artwork, cards and handling tables consumed by host-native VS. The caller
 * owns the returned resources. */
typedef struct FzeroVehicleResources FzeroVehicleResources;
FzeroVehicleResources *FzeroVehicleVersusResources(const uint8_t *stock, size_t size,
                                                  unsigned group, char *error, size_t cap);
bool FzeroVehicleResourceByte(const FzeroVehicleResources *resources, unsigned offset, uint8_t *value);
