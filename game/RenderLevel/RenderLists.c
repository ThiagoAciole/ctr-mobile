#include <common.h>

enum RenderListsSlot1P2P
{
	RENDER_LIST_SLOT_4X4 = 0,
	RENDER_LIST_SLOT_DYNAMIC_SUBDIV = 1,
	RENDER_LIST_SLOT_4X2 = 2,
	RENDER_LIST_SLOT_4X1 = 3,
	RENDER_LIST_SLOT_WATER = 4,
	RENDER_LIST_SLOT_FULL_DYNAMIC = 5,
};

enum RenderListsScratchOffset
{
	RENDER_LISTS_CORNER_JUMP_TABLE_OFFSET = 0x84,
	RENDER_LISTS_STACK_OFFSET = 0xd0,
};

struct RenderListsScratchRecord
{
	BspChildId childID;
	s16 unused;
	struct BoundingBox box;
};

CTR_STATIC_ASSERT(sizeof(struct RenderListsScratchRecord) == 0x10);

static int RenderLists_IsVisible(const int *visLeafList, BspChildId childID)
{
	u32 rawChildID = (u16)childID;

	if (visLeafList == 0)
	{
		return 1;
	}

	return ((s32)visLeafList[(rawChildID >> 5) & 0x1ff] << (rawChildID & 0x1f)) < 0;
}

static int RenderLists_BoxOverlapsPushBuffer(const struct BoundingBox *box, const struct BoundingBox *pbBox)
{
	return (box->min.x <= pbBox->max.x) && (pbBox->min.x <= box->max.x) && (box->min.y <= pbBox->max.y) && (pbBox->min.y <= box->max.y) &&
	       (box->min.z <= pbBox->max.z) && (pbBox->min.z <= box->max.z);
}

static void RenderLists_SelectBoxCorner(const struct BoundingBox *box, int cornerIndex, s16 *point)
{
	// NOTE(aalhendi): This is the exact corner order produced by retail helpers 0x80070310..0x8007037c: max/max/max through min/min/min by bit index.
	point[0] = (cornerIndex & 1) ? box->min.x : box->max.x;
	point[1] = (cornerIndex & 2) ? box->min.y : box->max.y;
	point[2] = (cornerIndex & 4) ? box->min.z : box->max.z;
}

static void RenderLists_Load1P2PGteState(struct PushBuffer *pb)
{
	if (pb == 0)
	{
		return;
	}

	// NOTE(aalhendi): Retail 0x8006fecc loads RT/TR/OFX/OFY/H once before traversal; frustum helpers only touch the light-matrix/RBK controls.
	gte_SetRotMatrix(&pb->matrix_ViewProj);
	gte_SetTransMatrix(&pb->matrix_ViewProj);
	gte_SetGeomOffset(pb->rect.w >> 1, pb->rect.h >> 1);
	gte_SetGeomScreen(pb->distanceToScreen_PREV);
}

static int RenderLists_FrustumRejectsCorner(const struct PushBufferFrustumPlane *plane, const s16 *point)
{
	// NOTE(aalhendi): Retail 0x80070290 tests the selected BSP corner with llv0bk and rejects when IR1 is positive.
	MTC2(CTR_PackS16Pair(point[0], point[1]), 0);
	MTC2((u32)(s32)point[2], 1);
	CTC2(CTR_PackS16Pair(plane->normal.x, plane->normal.y), 8);
	CTC2(CTR_PackS16Pair(plane->normal.z, plane->halfDistance), 9);
	CTC2((u32)(s32)(-2 * plane->halfDistance), 13);
	doCOP2(0x04a2012);

	return MFC2_S(9) > 0;
}

static int RenderLists_BoxPassesFrustum(struct PushBuffer *pb, const struct BoundingBox *box)
{
	s16 point[3];
	const struct PushBufferFrustumPlane *plane;

	if (pb == 0)
	{
		return 1;
	}

	for (int planeIndex = 0; planeIndex < 4; planeIndex++)
	{
		// NOTE(aalhendi): Retail tests planes in 1,3,0,2 order; each test is independent, so native preserves the plane/corner pairing in a simple loop.
		plane = &pb->frustumPlanes[planeIndex];
		RenderLists_SelectBoxCorner(box, pb->RenderListJmpIndex[planeIndex] & 7, point);

		if (RenderLists_FrustumRejectsCorner(plane, point))
		{
			return 0;
		}
	}

	return 1;
}

static int RenderLists_ProjectDistance(struct PushBuffer *pb, const struct BoundingBox *box)
{
	s16 point[3];
	int distance;

	if (pb == 0)
	{
		return 0;
	}

	RenderLists_SelectBoxCorner(box, pb->RenderListJmpIndex[5] & 7, point);

	CTR_GteLoadS16TripletV0(point);
	gte_rtps();
	gte_stsz(&distance);

	return distance;
}

static int RenderLists_Select1P2PSlot(const struct BSP *bsp, struct PushBuffer *pb, int lodDistanceThreshold)
{
	if ((bsp->flag & BSP_LEAF_FLAG_WATER) != 0)
	{
		return RENDER_LIST_SLOT_WATER;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_DYNAMIC_SUBDIV) != 0)
	{
		return RENDER_LIST_SLOT_DYNAMIC_SUBDIV;
	}

	if (RenderLists_ProjectDistance(pb, &bsp->box) > lodDistanceThreshold)
	{
		return RENDER_LIST_SLOT_FULL_DYNAMIC;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X4) != 0)
	{
		return RENDER_LIST_SLOT_4X4;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X1) != 0)
	{
		return RENDER_LIST_SLOT_4X1;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X2) != 0)
	{
		return RENDER_LIST_SLOT_4X2;
	}

	return RENDER_LIST_SLOT_DYNAMIC_SUBDIV;
}

static struct VisMemBspListNode **RenderLists_GetHead(struct DrawLevelOvr1PRenderList *renderList, int slotIndex)
{
	if ((renderList == NULL) || (slotIndex < 0) || (slotIndex > RENDER_LIST_SLOT_FULL_DYNAMIC))
	{
		return NULL;
	}

	if (slotIndex == RENDER_LIST_SLOT_FULL_DYNAMIC)
	{
		return &renderList->bspListStart_FullDynamic;
	}

	return &renderList->list[slotIndex].bspListStart;
}

static int RenderLists_Select3P4PSlot(const struct BSP *bsp)
{
	if ((bsp->flag & BSP_LEAF_FLAG_WATER) != 0)
	{
		return 2;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_DYNAMIC_SUBDIV) != 0)
	{
		return 3;
	}

	if ((bsp->flag & BSP_RENDER_LEAF_FLAG_4X4) != 0)
	{
		return 0;
	}

	return 1;
}

static void RenderLists_LinkBsp(struct BSP *bspRoot, struct BSP *bsp, struct VisMemBspListNode **head, struct VisMemBspListNode *bspList)
{
	struct VisMemBspListNode *node;
	int bspIndex = bsp - bspRoot;

	node = &bspList[bspIndex];

	node->next = *head;
	*head = node;
}

static void RenderLists_PushChild(struct BSP *bspRoot, const int *visLeafList, struct PushBuffer *pb, BspChildId childID,
                                  struct RenderListsScratchRecord **stack, struct RenderListsScratchRecord *stackEnd)
{
	struct BSP *child;
	struct RenderListsScratchRecord *record;

	if (childID < 0)
	{
		return;
	}

	if (!RenderLists_IsVisible(visLeafList, childID))
	{
		return;
	}

	child = &bspRoot[((u16)childID) & BSP_CHILD_ID_INDEX_MASK];
	if (!RenderLists_BoxOverlapsPushBuffer(&child->box, &pb->bbox))
	{
		return;
	}

	if (*stack >= stackEnd)
	{
		return;
	}

	record = *stack;
	record->childID = childID;
	record->unused = 0;
	record->box = child->box;
	*stack = record + 1;
}

static int RenderLists_Walk1P2P(struct BSP *bspRoot, const int *visLeafList, struct PushBuffer *pb,
                                struct DrawLevelOvr1PRenderList *renderList, struct VisMemBspListNode *bspList, u8 numPlyr)
{
	struct RenderListsScratchRecord *stackBase = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, RENDER_LISTS_STACK_OFFSET);
	struct RenderListsScratchRecord *stackEnd = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, CTR_SCRATCHPAD_SIZE);
	struct RenderListsScratchRecord *stack = stackBase;
	struct BSP *branch = bspRoot;
	int lodDistanceThreshold = (numPlyr == 1) ? CTR_SCRATCHPAD_PTR(struct MainRenderLevelGeometryScratch, 0)->bspLodDistanceThreshold : 0x1540;
	int count = 0;

	if (bspRoot == 0 || pb == 0)
	{
		return 0;
	}

	if ((bspRoot->flag & BSP_NODE_FLAG_LEAF) != 0)
	{
		int slotIndex = RenderLists_Select1P2PSlot(bspRoot, pb, lodDistanceThreshold);

		RenderLists_LinkBsp(bspRoot, bspRoot, RenderLists_GetHead(renderList, slotIndex), bspList);
		return 1;
	}

	for (;;)
	{
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[0], &stack, stackEnd);
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[1], &stack, stackEnd);

		while (stack != stackBase)
		{
			struct RenderListsScratchRecord record = *--stack;
			struct BSP *bsp = &bspRoot[((u16)record.childID) & BSP_CHILD_ID_INDEX_MASK];

			if (((u16)record.childID & BSP_CHILD_ID_LEAF_FLAG) == 0)
			{
				branch = bsp;
				goto nextBranch;
			}

			if (!RenderLists_BoxPassesFrustum(pb, &record.box))
			{
				continue;
			}

			int slotIndex = RenderLists_Select1P2PSlot(bsp, pb, lodDistanceThreshold);
			RenderLists_LinkBsp(bspRoot, bsp, RenderLists_GetHead(renderList, slotIndex), bspList);
			count++;
		}

		return count;

	nextBranch:
		continue;
	}
}

static int RenderLists_Walk3P4P(struct BSP *bspRoot, const int *visLeafList, struct PushBuffer *pb,
                                struct DrawLevelOvr1PRenderList *renderList, struct VisMemBspListNode *bspList)
{
	struct RenderListsScratchRecord *stackBase = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, RENDER_LISTS_STACK_OFFSET);
	struct RenderListsScratchRecord *stackEnd = CTR_SCRATCHPAD_PTR(struct RenderListsScratchRecord, CTR_SCRATCHPAD_SIZE);
	struct RenderListsScratchRecord *stack = stackBase;
	struct BSP *branch = bspRoot;
	int count = 0;

	if (bspRoot == 0 || pb == 0)
	{
		return 0;
	}

	if ((bspRoot->flag & BSP_NODE_FLAG_LEAF) != 0)
	{
		int slotIndex = RenderLists_Select3P4PSlot(bspRoot);
		struct VisMemBspListNode **head = RenderLists_GetHead(renderList, slotIndex);

		RenderLists_LinkBsp(bspRoot, bspRoot, head, bspList);
		return 1;
	}

	for (;;)
	{
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[0], &stack, stackEnd);
		RenderLists_PushChild(bspRoot, visLeafList, pb, branch->data.branch.childID[1], &stack, stackEnd);

		while (stack != stackBase)
		{
			struct RenderListsScratchRecord record = *--stack;
			struct BSP *bsp = &bspRoot[((u16)record.childID) & BSP_CHILD_ID_INDEX_MASK];

			if (((u16)record.childID & BSP_CHILD_ID_LEAF_FLAG) == 0)
			{
				branch = bsp;
				goto nextBranch;
			}

			if (!RenderLists_BoxPassesFrustum(pb, &record.box))
			{
				continue;
			}

			int slotIndex = RenderLists_Select3P4PSlot(bsp);
			struct VisMemBspListNode **head = RenderLists_GetHead(renderList, slotIndex);

			RenderLists_LinkBsp(bspRoot, bsp, head, bspList);
			count++;
		}

		return count;

	nextBranch:
		continue;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800702d4-0x80070388.
// NOTE(aalhendi): Retail corner helpers 0x80070308..0x80070384 are represented by RenderLists_SelectBoxCorner.
void RenderLists_PreInit()
{
	static const u32 renderListJumpTable[] = {
	    0x80070310, 0x80070320, 0x80070330, 0x80070340, 0x8007034c, 0x8007035c, 0x8007036c, 0x8007037c,
	};
	u32 *scratchJumpTable = CTR_SCRATCHPAD_PTR(u32, RENDER_LISTS_CORNER_JUMP_TABLE_OFFSET);

	for (int i = 0; i < 8; i++)
	{
		scratchJumpTable[i] = renderListJumpTable[i];
	}
}

int RenderLists_Init1P2P(struct BSP *bspRoot, int *visLeafList, struct PushBuffer *pb, void *levRenderList, void *bspList, u8 numPlyr)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8006fe70-0x800702d4.
	RenderLists_Load1P2PGteState(pb);
	return RenderLists_Walk1P2P(bspRoot, visLeafList, pb, levRenderList, bspList, numPlyr);
}

int RenderLists_Init3P4P(struct BSP *bspRoot, int *visLeafList, struct PushBuffer *pb, void *levRenderList, void *bspList)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80070388-0x80070720.
	RenderLists_Load1P2PGteState(pb);
	return RenderLists_Walk3P4P(bspRoot, visLeafList, pb, levRenderList, bspList);
}

int RenderLists_RunHostLayoutSelfTest(void)
{
	struct DrawLevelOvr1PRenderList renderList;
	struct VisMemBspListNode nodes[RENDER_LIST_SLOT_FULL_DYNAMIC + 1];

	memset(&renderList, 0, sizeof(renderList));
	memset(nodes, 0, sizeof(nodes));

	for (int slotIndex = 0; slotIndex < DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT; slotIndex++)
	{
		struct VisMemBspListNode **head = RenderLists_GetHead(&renderList, slotIndex);

		if (head != &renderList.list[slotIndex].bspListStart)
		{
			fprintf(stderr, "[CTR RenderLists] self-test failed: slot=%d host head mismatch\n", slotIndex);
			return 1;
		}
		*head = &nodes[slotIndex];
	}

	struct VisMemBspListNode **fullDynamicHead = RenderLists_GetHead(&renderList, RENDER_LIST_SLOT_FULL_DYNAMIC);
	if (fullDynamicHead != &renderList.bspListStart_FullDynamic)
	{
		fprintf(stderr, "[CTR RenderLists] self-test failed: full-dynamic host head mismatch\n");
		return 1;
	}
	*fullDynamicHead = &nodes[RENDER_LIST_SLOT_FULL_DYNAMIC];

	for (int slotIndex = 0; slotIndex < DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT; slotIndex++)
	{
		if (renderList.list[slotIndex].bspListStart != &nodes[slotIndex])
		{
			fprintf(stderr, "[CTR RenderLists] self-test failed: slot=%d write mismatch\n", slotIndex);
			return 1;
		}
	}
	if ((renderList.bspListStart_FullDynamic != &nodes[RENDER_LIST_SLOT_FULL_DYNAMIC]) ||
	    (RenderLists_GetHead(&renderList, -1) != NULL) ||
	    (RenderLists_GetHead(&renderList, RENDER_LIST_SLOT_FULL_DYNAMIC + 1) != NULL))
	{
		fprintf(stderr, "[CTR RenderLists] self-test failed: full-dynamic or bounds mismatch\n");
		return 1;
	}

	for (int bucketIndex = 0; bucketIndex < OVR226_BUCKET_COUNT; bucketIndex++)
	{
		if (sDrawLevelOvr1PBuckets[bucketIndex].role != bucketIndex)
		{
			fprintf(stderr, "[CTR RenderLists] self-test failed: canonical bucket=%d role=%u\n",
			        bucketIndex, sDrawLevelOvr1PBuckets[bucketIndex].role);
			return 1;
		}
	}

	u8 clipCursor;
	struct QuadBlock *renderedList[1];
	u32 visibilityWords[2];
	u32 orderingTable[DRAW_LEVEL_OVR1P_MAX_OT_INDEX + 1];
	struct PushBuffer pushBuffer;
	struct DrawLevelOvr1PClipRecord clipRecord;
	struct TextureLayout *waterEnvMap = (struct TextureLayout *)(void *)visibilityWords;

	memset(&pushBuffer, 0, sizeof(pushBuffer));
	memset(&clipRecord, 0, sizeof(clipRecord));
	sDrawLevelOvr1P_ClipRecordCursor = &clipCursor;
	sDrawLevelOvr1P_RenderedListCursor = renderedList;
	sDrawLevelOvr1P_VisibilityWordCursor = visibilityWords;
	sDrawLevelOvr1P_WaterEnvMap = waterEnvMap;
	pushBuffer.ptrOT = orderingTable;
	clipRecord.otEntry = (u32)(uintptr_t)&orderingTable[23];
	if ((DrawLevelOvr1P_GetClipRecordCursor() != &clipCursor) ||
	    (DrawLevelOvr1P_GetRenderedListCursor() != renderedList) ||
	    (sDrawLevelOvr1P_VisibilityWordCursor != visibilityWords) ||
	    (sDrawLevelOvr1P_WaterEnvMap != waterEnvMap) ||
	    (DrawLevelOvr1P_ResolveClipRecordOtEntry(&pushBuffer, &clipRecord) != &orderingTable[23]))
	{
		fprintf(stderr, "[CTR RenderLists] self-test failed: host pointer sidecar or clip OT rebase mismatch\n");
		return 1;
	}

	printf("[CTR RenderLists] self-test passed: pointer-size=%zu slot-size=%zu full-dynamic=0x%zx named-heads=6 dispatch=canonical-index host-cursors=4 clip-ot=rebased\n",
	       sizeof(void *), sizeof(struct DrawLevelOvr1PRenderListSlot),
	       offsetof(struct DrawLevelOvr1PRenderList, bspListStart_FullDynamic));
	return 0;
}
