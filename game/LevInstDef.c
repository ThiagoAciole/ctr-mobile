#include <common.h>


void LevInstDef_UnPack(struct mesh_info *ptr_mesh_info)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003116c-0x80031268.
	/*
	 * Retail rewrites four-byte InstDef references into four-byte Instance
	 * pointers in place. Native retains the immutable InstDef references and
	 * resolves ptrInstance through InstDef_GetInstance at each consumer.
	 */
	(void)ptr_mesh_info;
}


void LevInstDef_RePack(struct mesh_info *ptr_mesh_info, b32 boolAdvHub)
{
	// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031268-0x800313c8.
	struct Level *level1;
	struct Thread *th;

	(void)ptr_mesh_info;

	level1 = sdata->gGT->level1;

	if ((level1 != NULL) && (level1->numInstances != 0))
	{
		struct CtrAssetRef32 *references =
		    Level_GetInstDefRefs(level1, (size_t)level1->numInstances + 1u, "LevInstDef_RePack instance references");

		for (u32 index = 0; (references != NULL) && (index <= level1->numInstances) && (references[index].bits != 0); index++)
		{
			struct InstDef *instDef = NULL;
			struct Instance *inst;

			if (!CtrAssetRef_ResolveRequired(references[index], sizeof(*instDef), _Alignof(struct InstDef), (void **)&instDef,
							"LevInstDef_RePack InstDef"))
			{
				break;
			}

			inst = InstDef_GetInstance(instDef);
			if (inst == NULL)
			{
				continue;
			}

			// if on adv hub
			if (boolAdvHub != 0)
			{
				th = inst->thread;
				if (th != 0)
				{
					th->flags |= THREAD_FLAG_DEAD;
				}

				// erase instance in pool
				LIST_AddFront(&sdata->gGT->JitPools.instance.free, (struct Item *)inst);
			}
		}
	}

	PROC_CheckAllForDead();
}
