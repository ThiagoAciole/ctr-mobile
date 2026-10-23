#ifndef NATIVE_GUEST_REF_H
#define NATIVE_GUEST_REF_H

#include <stddef.h>
#include <stdint.h>

#define NATIVE_GUEST_REF_REGION_SHIFT 24u
#define NATIVE_GUEST_REF_OFFSET_MASK  0x00ffffffu
#define NATIVE_GUEST_REF_REGION_CAP   255u
#define NATIVE_GUEST_REF_REGION_SIZE  0x01000000u

/*
 * A guest reference contains no native address bits. Zero is null; nonzero
 * values contain an eight-bit region tag and a 24-bit byte offset.
 */
struct NativeGuestRef32
{
	uint32_t bits;
};

enum NativeGuestRefStatus
{
	NATIVE_GUEST_REF_OK = 0,
	NATIVE_GUEST_REF_NULL,
	NATIVE_GUEST_REF_INVALID_ARGUMENT,
	NATIVE_GUEST_REF_INVALID_REGION,
	NATIVE_GUEST_REF_REGION_IN_USE,
	NATIVE_GUEST_REF_REGION_OVERLAP,
	NATIVE_GUEST_REF_REGION_TOO_LARGE,
	NATIVE_GUEST_REF_HOST_RANGE_OVERFLOW,
	NATIVE_GUEST_REF_UNREGISTERED_REGION,
	NATIVE_GUEST_REF_OUT_OF_RANGE,
	NATIVE_GUEST_REF_MISALIGNED,
	NATIVE_GUEST_REF_HOST_POINTER_NOT_FOUND,
};

struct NativeGuestRefError
{
	enum NativeGuestRefStatus status;
	struct NativeGuestRef32 reference;
	uint32_t regionTag;
	uint32_t offset;
	size_t accessSize;
	size_t alignment;
	const char *regionLabel;
	const char *context;
};

void NativeGuestRef_Reset(void);

/*
 * The registered range is borrowed rather than owned. hostBase and label must
 * remain valid until the tag is unregistered or the registry is reset.
 */
int NativeGuestRef_RegisterRegion(uint32_t regionTag, void *hostBase, size_t size, const char *label, struct NativeGuestRefError *error);
void NativeGuestRef_UnregisterRegion(uint32_t regionTag);

/*
 * Non-null conversions require a nonzero access size. Resolution also
 * requires a power-of-two alignment. Output values are cleared on failure.
 */
int NativeGuestRef_FromRegionOffset(uint32_t regionTag, uint32_t offset, size_t accessSize, struct NativeGuestRef32 *referenceOut,
				    struct NativeGuestRefError *error, const char *context);
int NativeGuestRef_FromHostPointer(const void *hostPointer, size_t accessSize, struct NativeGuestRef32 *referenceOut,
				   struct NativeGuestRefError *error, const char *context);
int NativeGuestRef_ResolveOptional(struct NativeGuestRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut,
				   struct NativeGuestRefError *error, const char *context);
int NativeGuestRef_ResolveRequired(struct NativeGuestRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut,
				   struct NativeGuestRefError *error, const char *context);
const char *NativeGuestRef_StatusName(enum NativeGuestRefStatus status);
int NativeGuestRef_RunSelfTest(void);

_Static_assert(sizeof(struct NativeGuestRef32) == 4, "Guest references must remain four bytes");

#endif
