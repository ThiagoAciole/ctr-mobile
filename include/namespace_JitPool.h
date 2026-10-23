#ifndef CTR_NATIVE_NAMESPACE_JITPOOL_H
#define CTR_NATIVE_NAMESPACE_JITPOOL_H

enum JitPoolConstants
{
	JITPOOL_ITEM_ALIGNMENT = sizeof(void *),
	JITPOOL_ITEM_ALIGNMENT_MASK = JITPOOL_ITEM_ALIGNMENT - 1,
	JITPOOL_ITEM_ALIGNMENT_CLEAR_MASK = -JITPOOL_ITEM_ALIGNMENT,
};

#define JITPOOL_ALIGN_ITEM_STRIDE(size) (((size) + JITPOOL_ITEM_ALIGNMENT_MASK) & JITPOOL_ITEM_ALIGNMENT_CLEAR_MASK)

struct JitPool
{
	// 0x0
	struct LinkedList free;

	// 0xc
	struct LinkedList taken;

	// 0x18
	s32 maxItems;

	// 0x1c
	u32 itemSize;

	// 0x20
	s32 poolSize;

	// 0x24
	void *ptrPoolData;
};

CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, free) == 0x0);
#if UINTPTR_MAX == UINT32_MAX
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, taken) == 0xc);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, maxItems) == 0x18);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, itemSize) == 0x1c);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, poolSize) == 0x20);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, ptrPoolData) == 0x24);
CTR_STATIC_ASSERT(sizeof(struct JitPool) == 0x28);
#else
CTR_STATIC_ASSERT(sizeof(void *) == 0x8);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, taken) == 0x18);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, maxItems) == 0x30);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, itemSize) == 0x34);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, poolSize) == 0x38);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, ptrPoolData) == 0x40);
CTR_STATIC_ASSERT(sizeof(struct JitPool) == 0x48);
#endif

#endif
