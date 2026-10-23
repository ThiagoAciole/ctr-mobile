#include <common.h>

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800ad92c-0x800ad9ac.
void RB_Explosion_ThTick(struct Thread *t)
{
	struct Instance *inst = t->inst;

	int frame = inst->animFrame;
	int total = INSTANCE_GetNumAnimFrames(inst, 0);

	if ((frame + 1) < total)
	{
		inst->animFrame++;
	}
	else
	{
		// dead thread
		t->flags |= THREAD_FLAG_DEAD;
	}

	ThTick_FastRET(t);
}

// Retail stores these nine records at 0x800b2d58 with a 0x24-byte, 32-bit
// ParticleEmitter stride. A raw-byte cast is only valid for that ABI: native
// 64-bit builds place the pointer-bearing union at 0x8 and use a 0x30 stride.
// Expressing the table semantically preserves the retail bytes on 32-bit and
// gives every host ABI its correct offsets and stride.
static const struct ParticleEmitter s_potionShatterEmitter[] = {
    [0] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START,
            .initOffset = PARTICLE_EMITTER_INIT_FUNC_OFFSET,
            .InitTypes.FuncInit =
                {
                    .particle_funcPtr = NULL,
                    .particle_colorFlags = 0x00a1,
                    .particle_lifespan = 20,
                    .particle_Type = 0,
                },
        },
    [1] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START,
            .initOffset = PARTICLE_AXIS_POS_X,
            .InitTypes.AxisInit = {.baseValue = {.startVal = 1}},
        },
    [2] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START,
            .initOffset = PARTICLE_AXIS_POS_Z,
            .InitTypes.AxisInit = {.baseValue = {.startVal = 1}},
        },
    [3] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START |
                     PARTICLE_EMITTER_FLAG_BASE_VELOCITY |
                     PARTICLE_EMITTER_FLAG_BASE_ACCEL |
                     PARTICLE_EMITTER_FLAG_RANDOM_VELOCITY,
            .initOffset = PARTICLE_AXIS_POS_Y,
            .InitTypes.AxisInit =
                {
                    .baseValue = {.startVal = 1, .velocity = 3800, .accel = -280},
                    .rngSeed = {.velocity = 400},
                },
        },
    [4] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START,
            .initOffset = PARTICLE_AXIS_SCALE_X_OR_LINE_SCALE,
            .InitTypes.AxisInit = {.baseValue = {.startVal = 0x1000}},
        },
    [5] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START,
            .initOffset = PARTICLE_AXIS_COLOR_R,
            .InitTypes.AxisInit = {.baseValue = {.startVal = 1}},
        },
    [6] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START,
            .initOffset = PARTICLE_AXIS_COLOR_G,
            .InitTypes.AxisInit = {.baseValue = {.startVal = 0xc800}},
        },
    [7] =
        {
            .flags = PARTICLE_EMITTER_FLAG_BASE_START,
            .initOffset = PARTICLE_AXIS_COLOR_B,
            .InitTypes.AxisInit = {.baseValue = {.startVal = 1}},
        },
    [8] = {0},
};

static int RB_Explosion_PotionAxisMatches(int index, u16 flags, s16 axis,
                                          int start, s16 velocity, s16 accel,
                                          int randomStart, s16 randomVelocity, s16 randomAccel)
{
	const struct ParticleEmitter *emitter = &s_potionShatterEmitter[index];
	const struct ParticleAxis *base = &emitter->InitTypes.AxisInit.baseValue;
	const struct ParticleAxis *random = &emitter->InitTypes.AxisInit.rngSeed;

	return emitter->flags == flags &&
	       emitter->initOffset == axis &&
	       base->startVal == start &&
	       base->velocity == velocity &&
	       base->accel == accel &&
	       random->startVal == randomStart &&
	       random->velocity == randomVelocity &&
	       random->accel == randomAccel;
}

int RB_Explosion_RunPotionEmitterSelfTest(void)
{
	const struct ParticleEmitter *functionEmitter = &s_potionShatterEmitter[0];
	const u16 randomVelocityFlags = PARTICLE_EMITTER_FLAG_BASE_START |
	                                PARTICLE_EMITTER_FLAG_BASE_VELOCITY |
	                                PARTICLE_EMITTER_FLAG_BASE_ACCEL |
	                                PARTICLE_EMITTER_FLAG_RANDOM_VELOCITY;
	int semanticMatch =
	    functionEmitter->flags == PARTICLE_EMITTER_FLAG_BASE_START &&
	    functionEmitter->initOffset == PARTICLE_EMITTER_INIT_FUNC_OFFSET &&
	    functionEmitter->InitTypes.FuncInit.particle_funcPtr == NULL &&
	    functionEmitter->InitTypes.FuncInit.particle_colorFlags == 0x00a1 &&
	    functionEmitter->InitTypes.FuncInit.particle_lifespan == 20 &&
	    functionEmitter->InitTypes.FuncInit.particle_Type == 0 &&
	    RB_Explosion_PotionAxisMatches(1, PARTICLE_EMITTER_FLAG_BASE_START, PARTICLE_AXIS_POS_X, 1, 0, 0, 0, 0, 0) &&
	    RB_Explosion_PotionAxisMatches(2, PARTICLE_EMITTER_FLAG_BASE_START, PARTICLE_AXIS_POS_Z, 1, 0, 0, 0, 0, 0) &&
	    RB_Explosion_PotionAxisMatches(3, randomVelocityFlags, PARTICLE_AXIS_POS_Y, 1, 3800, -280, 0, 400, 0) &&
	    RB_Explosion_PotionAxisMatches(4, PARTICLE_EMITTER_FLAG_BASE_START, PARTICLE_AXIS_SCALE_X_OR_LINE_SCALE, 0x1000, 0, 0, 0, 0, 0) &&
	    RB_Explosion_PotionAxisMatches(5, PARTICLE_EMITTER_FLAG_BASE_START, PARTICLE_AXIS_COLOR_R, 1, 0, 0, 0, 0, 0) &&
	    RB_Explosion_PotionAxisMatches(6, PARTICLE_EMITTER_FLAG_BASE_START, PARTICLE_AXIS_COLOR_G, 0xc800, 0, 0, 0, 0, 0) &&
	    RB_Explosion_PotionAxisMatches(7, PARTICLE_EMITTER_FLAG_BASE_START, PARTICLE_AXIS_COLOR_B, 1, 0, 0, 0, 0, 0) &&
	    s_potionShatterEmitter[8].flags == 0;

	if (!semanticMatch)
	{
		fprintf(stderr, "[CTR PotionEmitter] self-test failed: semantic table mismatch\n");
		return 1;
	}

#if UINTPTR_MAX == UINT32_MAX
	static const u32 retailWords[] = {
	    0x000c0001, 0x00000000, 0x001400a1, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00000001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00020001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00010017, 0x00000001, 0xfee80ed8, 0x00000000, 0x00000190, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00050001, 0x00001000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00070001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00080001, 0x0000c800, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00090001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	};

	if (sizeof(s_potionShatterEmitter) != sizeof(retailWords) ||
	    memcmp(s_potionShatterEmitter, retailWords, sizeof(retailWords)) != 0)
	{
		fprintf(stderr, "[CTR PotionEmitter] self-test failed: 32-bit retail byte layout mismatch\n");
		return 1;
	}
#endif

	printf("[CTR PotionEmitter] self-test passed: pointer-size=%zu entries=8 lifespan=20 random-velocity=400 typed-layout=yes\n",
	       sizeof(void *));
	return 0;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b1458-0x800b1630.
// NOTE(aalhendi): Native expresses retail emitter bytes from 0x800b2d58 as
// typed records so pointer-bearing layouts remain valid on 64-bit hosts.
void RB_Explosion_InitPotion(struct Instance *inst)
{
	struct Instance *shatterInst;
	struct Particle *p;
	int shatterColor;

	// green explosion
	shatterColor = STATIC_SHOCKWAVE_GREEN;

	// if red beaker, red explosion
	if (inst->model->id == STATIC_BEAKER_RED)
	{
		shatterColor = STATIC_SHOCKWAVE_RED;
	}

	// create thread for shatter
	shatterInst = INSTANCE_BirthWithThread(shatterColor, 0, SMALL, OTHER, RB_Explosion_ThTick, 0, 0);

	shatterInst->flags |= (PIXEL_LOD | CUSTOM_MATRIX);

	// set funcThDestroy to remove instance from instance pool
	shatterInst->thread->funcThDestroy = PROC_DestroyInstance;

	// copy position and rotation from one instance to the other
	CTR_MatrixCopyRot(&shatterInst->matrix, &inst->matrix);

	for (int i = 0; i < 3; i++)
	{
		shatterInst->scale.v[i] = 0x800;
		shatterInst->matrix.t[i] = inst->matrix.t[i];
	}

	// particles for potion shatter
	for (int i = 0; i < 5; i++)
	{
		// Create instance in particle pool
		p = Particle_Init(0, sdata->gGT->iconGroup[1], s_potionShatterEmitter);

		if (p == NULL)
		{
			continue;
		}

		p->axis[0].startVal += shatterInst->matrix.t[0] * 0x100;
		p->axis[1].startVal += shatterInst->matrix.t[1] * 0x100;
		p->axis[2].startVal += shatterInst->matrix.t[2] * 0x100;

		p->modelID = shatterColor;

		if (shatterColor == STATIC_SHOCKWAVE_GREEN)
		{
			p->axis[7].startVal = 1;
			p->axis[8].startVal = 0xc800;
		}

		else
		{
			p->axis[7].startVal = 0xc800;
			p->axis[8].startVal = 1;
		}

		p->axis[9].startVal = 1;

		p->funcPtr = Particle_FuncPtr_PotionShatter;
	}

	RB_Potion_OnShatter_TeethSearch(inst);
	return;
}

static char s_explosion1[] = "explosion1";

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800b1630-0x800b1714.
void RB_Explosion_InitGeneric(struct Instance *inst)
{
	struct Instance *explosion;
	u32 color;

	// create thread for explosion
	explosion = INSTANCE_BirthWithThread(STATIC_CRATE_EXPLOSION, s_explosion1, SMALL, OTHER, RB_Explosion_ThTick, 0, 0);

	// copy position and rotation from one instance to the other
	CTR_MatrixCopyRot(&explosion->matrix, &inst->matrix);

	explosion->matrix.t[0] = inst->matrix.t[0];
	explosion->matrix.t[1] = inst->matrix.t[1];
	explosion->matrix.t[2] = inst->matrix.t[2];

	// green
	color = 0x1eac000;

	// instance -> model -> modelID == TNT
	if ((inst->model->id) == STATIC_CRATE_TNT)
	{
		// red
		color = 0xad10000;
	}

	// set color
	explosion->colorRGBA = color;

	// set scale
	explosion->alphaScale = 0x1000;

	// set funcThDestroy to remove instance from instance pool
	explosion->thread->funcThDestroy = PROC_DestroyInstance;
	return;
}
