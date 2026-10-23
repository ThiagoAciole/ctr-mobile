#ifndef CTR_ASSET_REF_H
#define CTR_ASSET_REF_H

#include <macros.h>

/*
 * A four-byte reference stored inside a relocated retail asset.
 *
 * The existing i686 loader stores a native 32-bit pointer in bits. The LP64
 * loader stores the tagged guest-reference encoding selected by ADR-0001.
 * Consumers must resolve the value through this API instead of casting it.
 */
struct CtrAssetRef32
{
	u32 bits;
};

#if defined(CTR_NATIVE)
int CtrAssetRef_ResolveOptional(struct CtrAssetRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut, const char *context);
int CtrAssetRef_ResolveRequired(struct CtrAssetRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut, const char *context);
int CtrAssetRef_ResolveArrayOptional(struct CtrAssetRef32 reference, size_t count, size_t elementSize, size_t alignment, void **hostPointerOut,
				    const char *context);
int CtrAssetRef_ResolveArrayRequired(struct CtrAssetRef32 reference, size_t count, size_t elementSize, size_t alignment, void **hostPointerOut,
				    const char *context);
#else
force_inline int CtrAssetRef_ResolveOptional(struct CtrAssetRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut, const char *context)
{
	(void)accessSize;
	(void)alignment;
	(void)context;

	if (hostPointerOut == NULL)
	{
		return 0;
	}

	*hostPointerOut = (void *)(uintptr_t)reference.bits;
	return 1;
}

force_inline int CtrAssetRef_ResolveRequired(struct CtrAssetRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut, const char *context)
{
	return (reference.bits != 0) && CtrAssetRef_ResolveOptional(reference, accessSize, alignment, hostPointerOut, context);
}

force_inline int CtrAssetRef_ResolveArrayOptional(struct CtrAssetRef32 reference, size_t count, size_t elementSize, size_t alignment,
						  void **hostPointerOut, const char *context)
{
	if ((count == 0) || (elementSize == 0) || (count > SIZE_MAX / elementSize))
	{
		if (hostPointerOut != NULL)
		{
			*hostPointerOut = NULL;
		}
		return 0;
	}

	return CtrAssetRef_ResolveOptional(reference, count * elementSize, alignment, hostPointerOut, context);
}

force_inline int CtrAssetRef_ResolveArrayRequired(struct CtrAssetRef32 reference, size_t count, size_t elementSize, size_t alignment,
						  void **hostPointerOut, const char *context)
{
	if ((count == 0) || (elementSize == 0) || (count > SIZE_MAX / elementSize))
	{
		if (hostPointerOut != NULL)
		{
			*hostPointerOut = NULL;
		}
		return 0;
	}

	return CtrAssetRef_ResolveRequired(reference, count * elementSize, alignment, hostPointerOut, context);
}
#endif

CTR_STATIC_ASSERT(sizeof(struct CtrAssetRef32) == sizeof(u32));

#endif
