#include "platform/native_guest_ref.h"

#include <macros.h>

#include <stdio.h>
#include <string.h>

struct NativeGuestRegion
{
	uintptr_t hostStart;
	uintptr_t hostEnd;
	uint32_t size;
	const char *label;
	int registered;
};

global_variable struct NativeGuestRegion s_nativeGuestRegions[NATIVE_GUEST_REF_REGION_CAP + 1u];

internal void NativeGuestRef_SetError(struct NativeGuestRefError *error, enum NativeGuestRefStatus status, struct NativeGuestRef32 reference,
				      uint32_t regionTag, uint32_t offset, size_t accessSize, size_t alignment,
				      const char *regionLabel, const char *context)
{
	if (error == NULL)
	{
		return;
	}

	error->status = status;
	error->reference = reference;
	error->regionTag = regionTag;
	error->offset = offset;
	error->accessSize = accessSize;
	error->alignment = alignment;
	error->regionLabel = regionLabel;
	error->context = context;
}

internal struct NativeGuestRef32 NativeGuestRef_Null(void)
{
	const struct NativeGuestRef32 reference = {0};
	return reference;
}

internal int NativeGuestRef_AlignmentValid(size_t alignment)
{
	return (alignment != 0) && ((alignment & (alignment - 1u)) == 0);
}

internal int NativeGuestRef_AccessFits(uint32_t offset, size_t accessSize, uint32_t regionSize)
{
	if (offset >= regionSize)
	{
		return 0;
	}

	return accessSize <= (size_t)(regionSize - offset);
}

internal int NativeGuestRef_HostRangesOverlap(uintptr_t leftStart, uintptr_t leftEnd, uintptr_t rightStart, uintptr_t rightEnd)
{
	return (leftStart < rightEnd) && (rightStart < leftEnd);
}

void NativeGuestRef_Reset(void)
{
	memset(s_nativeGuestRegions, 0, sizeof(s_nativeGuestRegions));
}

int NativeGuestRef_RegisterRegion(uint32_t regionTag, void *hostBase, size_t size, const char *label, struct NativeGuestRefError *error)
{
	const struct NativeGuestRef32 nullReference = NativeGuestRef_Null();
	uintptr_t hostStart;
	uintptr_t hostEnd;
	struct NativeGuestRegion *region;

	if ((regionTag == 0) || (regionTag > NATIVE_GUEST_REF_REGION_CAP) || (hostBase == NULL) || (size == 0))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_INVALID_ARGUMENT, nullReference, regionTag, 0, size, 1, label, "register region");
		return 0;
	}
	if (size > NATIVE_GUEST_REF_REGION_SIZE)
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_REGION_TOO_LARGE, nullReference, regionTag, 0, size, 1, label, "register region");
		return 0;
	}

	region = &s_nativeGuestRegions[regionTag];
	if (region->registered)
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_REGION_IN_USE, nullReference, regionTag, 0, size, 1, region->label, "register region");
		return 0;
	}

	hostStart = (uintptr_t)hostBase;
	hostEnd = hostStart + (uintptr_t)size;
	if (hostEnd < hostStart)
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_HOST_RANGE_OVERFLOW, nullReference, regionTag, 0, size, 1, label, "register region");
		return 0;
	}

	for (uint32_t existingTag = 1; existingTag <= NATIVE_GUEST_REF_REGION_CAP; existingTag++)
	{
		const struct NativeGuestRegion *existing = &s_nativeGuestRegions[existingTag];

		if (existing->registered && NativeGuestRef_HostRangesOverlap(hostStart, hostEnd, existing->hostStart, existing->hostEnd))
		{
			NativeGuestRef_SetError(error, NATIVE_GUEST_REF_REGION_OVERLAP, nullReference, existingTag, 0, size, 1,
					       existing->label, "register region");
			return 0;
		}
	}

	region->hostStart = hostStart;
	region->hostEnd = hostEnd;
	region->size = (uint32_t)size;
	region->label = label;
	region->registered = 1;
	NativeGuestRef_SetError(error, NATIVE_GUEST_REF_OK, nullReference, regionTag, 0, size, 1, label, "register region");
	return 1;
}

void NativeGuestRef_UnregisterRegion(uint32_t regionTag)
{
	if ((regionTag == 0) || (regionTag > NATIVE_GUEST_REF_REGION_CAP))
	{
		return;
	}

	memset(&s_nativeGuestRegions[regionTag], 0, sizeof(s_nativeGuestRegions[regionTag]));
}

int NativeGuestRef_FromRegionOffset(uint32_t regionTag, uint32_t offset, size_t accessSize, struct NativeGuestRef32 *referenceOut,
				    struct NativeGuestRefError *error, const char *context)
{
	const struct NativeGuestRef32 nullReference = NativeGuestRef_Null();
	const struct NativeGuestRegion *region;
	struct NativeGuestRef32 reference;

	if (referenceOut != NULL)
	{
		*referenceOut = nullReference;
	}
	if ((referenceOut == NULL) || (accessSize == 0))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_INVALID_ARGUMENT, nullReference, regionTag, offset, accessSize, 1, NULL, context);
		return 0;
	}
	if ((regionTag == 0) || (regionTag > NATIVE_GUEST_REF_REGION_CAP))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_INVALID_REGION, nullReference, regionTag, offset, accessSize, 1, NULL, context);
		return 0;
	}

	region = &s_nativeGuestRegions[regionTag];
	if (!region->registered)
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_UNREGISTERED_REGION, nullReference, regionTag, offset, accessSize, 1, NULL, context);
		return 0;
	}
	if ((offset > NATIVE_GUEST_REF_OFFSET_MASK) || !NativeGuestRef_AccessFits(offset, accessSize, region->size))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_OUT_OF_RANGE, nullReference, regionTag, offset, accessSize, 1, region->label, context);
		return 0;
	}

	reference.bits = (regionTag << NATIVE_GUEST_REF_REGION_SHIFT) | offset;
	*referenceOut = reference;
	NativeGuestRef_SetError(error, NATIVE_GUEST_REF_OK, reference, regionTag, offset, accessSize, 1, region->label, context);
	return 1;
}

int NativeGuestRef_FromHostPointer(const void *hostPointer, size_t accessSize, struct NativeGuestRef32 *referenceOut,
				   struct NativeGuestRefError *error, const char *context)
{
	const struct NativeGuestRef32 nullReference = NativeGuestRef_Null();
	const uintptr_t hostAddress = (uintptr_t)hostPointer;

	if (referenceOut != NULL)
	{
		*referenceOut = nullReference;
	}
	if ((referenceOut == NULL) || (accessSize == 0))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_INVALID_ARGUMENT, nullReference, 0, 0, accessSize, 1, NULL, context);
		return 0;
	}
	if (hostPointer == NULL)
	{
		*referenceOut = nullReference;
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_NULL, nullReference, 0, 0, accessSize, 1, NULL, context);
		return 1;
	}

	for (uint32_t regionTag = 1; regionTag <= NATIVE_GUEST_REF_REGION_CAP; regionTag++)
	{
		const struct NativeGuestRegion *region = &s_nativeGuestRegions[regionTag];

		if (region->registered && (hostAddress >= region->hostStart) && (hostAddress < region->hostEnd))
		{
			const uint32_t offset = (uint32_t)(hostAddress - region->hostStart);
			return NativeGuestRef_FromRegionOffset(regionTag, offset, accessSize, referenceOut, error, context);
		}
	}

	NativeGuestRef_SetError(error, NATIVE_GUEST_REF_HOST_POINTER_NOT_FOUND, nullReference, 0, 0, accessSize, 1, NULL, context);
	return 0;
}

internal int NativeGuestRef_Resolve(struct NativeGuestRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut,
				    struct NativeGuestRefError *error, const char *context, int nullAllowed)
{
	const uint32_t regionTag = reference.bits >> NATIVE_GUEST_REF_REGION_SHIFT;
	const uint32_t offset = reference.bits & NATIVE_GUEST_REF_OFFSET_MASK;
	const struct NativeGuestRegion *region;
	uintptr_t hostAddress;

	if (hostPointerOut != NULL)
	{
		*hostPointerOut = NULL;
	}
	if ((hostPointerOut == NULL) || (accessSize == 0) || !NativeGuestRef_AlignmentValid(alignment))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_INVALID_ARGUMENT, reference, regionTag, offset, accessSize, alignment, NULL, context);
		return 0;
	}
	if (reference.bits == 0)
	{
		*hostPointerOut = NULL;
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_NULL, reference, 0, 0, accessSize, alignment, NULL, context);
		return nullAllowed;
	}
	if ((regionTag == 0) || (regionTag > NATIVE_GUEST_REF_REGION_CAP))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_INVALID_REGION, reference, regionTag, offset, accessSize, alignment, NULL, context);
		return 0;
	}

	region = &s_nativeGuestRegions[regionTag];
	if (!region->registered)
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_UNREGISTERED_REGION, reference, regionTag, offset, accessSize, alignment, NULL, context);
		return 0;
	}
	if (!NativeGuestRef_AccessFits(offset, accessSize, region->size))
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_OUT_OF_RANGE, reference, regionTag, offset, accessSize, alignment, region->label, context);
		return 0;
	}

	hostAddress = region->hostStart + offset;
	if ((hostAddress & (alignment - 1u)) != 0)
	{
		NativeGuestRef_SetError(error, NATIVE_GUEST_REF_MISALIGNED, reference, regionTag, offset, accessSize, alignment, region->label, context);
		return 0;
	}

	*hostPointerOut = (void *)hostAddress;
	NativeGuestRef_SetError(error, NATIVE_GUEST_REF_OK, reference, regionTag, offset, accessSize, alignment, region->label, context);
	return 1;
}

int NativeGuestRef_ResolveOptional(struct NativeGuestRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut,
				   struct NativeGuestRefError *error, const char *context)
{
	return NativeGuestRef_Resolve(reference, accessSize, alignment, hostPointerOut, error, context, 1);
}

int NativeGuestRef_ResolveRequired(struct NativeGuestRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut,
				   struct NativeGuestRefError *error, const char *context)
{
	return NativeGuestRef_Resolve(reference, accessSize, alignment, hostPointerOut, error, context, 0);
}

const char *NativeGuestRef_StatusName(enum NativeGuestRefStatus status)
{
	switch (status)
	{
	case NATIVE_GUEST_REF_OK:
		return "ok";
	case NATIVE_GUEST_REF_NULL:
		return "null";
	case NATIVE_GUEST_REF_INVALID_ARGUMENT:
		return "invalid-argument";
	case NATIVE_GUEST_REF_INVALID_REGION:
		return "invalid-region";
	case NATIVE_GUEST_REF_REGION_IN_USE:
		return "region-in-use";
	case NATIVE_GUEST_REF_REGION_OVERLAP:
		return "region-overlap";
	case NATIVE_GUEST_REF_REGION_TOO_LARGE:
		return "region-too-large";
	case NATIVE_GUEST_REF_HOST_RANGE_OVERFLOW:
		return "host-range-overflow";
	case NATIVE_GUEST_REF_UNREGISTERED_REGION:
		return "unregistered-region";
	case NATIVE_GUEST_REF_OUT_OF_RANGE:
		return "out-of-range";
	case NATIVE_GUEST_REF_MISALIGNED:
		return "misaligned";
	case NATIVE_GUEST_REF_HOST_POINTER_NOT_FOUND:
		return "host-pointer-not-found";
	}

	return "unknown";
}

int NativeGuestRef_RunSelfTest(void)
{
	uint32_t regionWords[16] = {0};
	uint32_t otherWords[4] = {0};
	struct NativeGuestRef32 reference;
	struct NativeGuestRef32 nullReference = {0};
	struct NativeGuestRef32 staleReference;
	struct NativeGuestRefError error;
	void *resolved = regionWords;

	NativeGuestRef_Reset();
	if (!NativeGuestRef_RegisterRegion(3, regionWords, sizeof(regionWords), "self-test-region", &error))
	{
		return 1;
	}
	if (NativeGuestRef_RegisterRegion(3, otherWords, sizeof(otherWords), "duplicate-tag", &error) ||
	    (error.status != NATIVE_GUEST_REF_REGION_IN_USE))
	{
		return 1;
	}
	if (NativeGuestRef_RegisterRegion(4, &regionWords[4], sizeof(otherWords), "overlap", &error) ||
	    (error.status != NATIVE_GUEST_REF_REGION_OVERLAP))
	{
		return 1;
	}
	reference.bits = UINT32_MAX;
	if (NativeGuestRef_FromRegionOffset(3, 0, 0, &reference, &error, "zero-size encode") ||
	    (error.status != NATIVE_GUEST_REF_INVALID_ARGUMENT) || (reference.bits != 0))
	{
		return 1;
	}
	reference.bits = UINT32_MAX;
	if (NativeGuestRef_FromRegionOffset(0, 0, sizeof(uint32_t), &reference, &error, "invalid tag") ||
	    (error.status != NATIVE_GUEST_REF_INVALID_REGION) || (reference.bits != 0))
	{
		return 1;
	}
	if (!NativeGuestRef_FromHostPointer(&regionWords[5], sizeof(regionWords[5]), &reference, &error, "self-test value") ||
	    (reference.bits != 0x03000014u) ||
	    !NativeGuestRef_ResolveRequired(reference, sizeof(regionWords[5]), _Alignof(uint32_t), &resolved, &error, "self-test value") ||
	    (resolved != &regionWords[5]))
	{
		return 1;
	}

	staleReference = reference;
	if (NativeGuestRef_FromRegionOffset(3, (uint32_t)sizeof(regionWords) - 2u, sizeof(uint32_t), &reference, &error, "overflow probe") ||
	    (error.status != NATIVE_GUEST_REF_OUT_OF_RANGE))
	{
		return 1;
	}
	resolved = regionWords;
	if (NativeGuestRef_ResolveRequired(reference, 0, _Alignof(uint32_t), &resolved, &error, "zero-size resolve") ||
	    (error.status != NATIVE_GUEST_REF_INVALID_ARGUMENT) || (resolved != NULL))
	{
		return 1;
	}

	reference.bits = 0x03000002u;
	if (NativeGuestRef_ResolveRequired(reference, 1, _Alignof(uint32_t), &resolved, &error, "alignment probe") ||
	    (error.status != NATIVE_GUEST_REF_MISALIGNED))
	{
		return 1;
	}
	if (!NativeGuestRef_ResolveOptional(nullReference, 1, 1, &resolved, &error, "optional null") || (resolved != NULL) ||
	    (error.status != NATIVE_GUEST_REF_NULL))
	{
		return 1;
	}
	if (NativeGuestRef_ResolveRequired(nullReference, 1, 1, &resolved, &error, "required null") ||
	    (error.status != NATIVE_GUEST_REF_NULL))
	{
		return 1;
	}

	NativeGuestRef_UnregisterRegion(3);
	if (NativeGuestRef_ResolveRequired(staleReference, sizeof(uint32_t), _Alignof(uint32_t), &resolved, &error, "stale probe") ||
	    (error.status != NATIVE_GUEST_REF_UNREGISTERED_REGION))
	{
		return 1;
	}

	printf("[CTR GuestRef] self-test passed: reference=0x%08x overflow=%s stale=%s\n", staleReference.bits,
	       NativeGuestRef_StatusName(NATIVE_GUEST_REF_OUT_OF_RANGE), NativeGuestRef_StatusName(error.status));
	return 0;
}
