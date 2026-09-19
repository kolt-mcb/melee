#include <dolphin.h>

#include <dolphin/ax.h>
#include <dolphin/axfx.h>

// functions
static void DLsetdelay(struct AXFX_REVSTD_DELAYLINE* dl, s32 lag);
static void DLcreate(struct AXFX_REVSTD_DELAYLINE* dl, s32 max_length);
static void DLdelete(struct AXFX_REVSTD_DELAYLINE* dl);
static int ReverbSTDCreate(struct AXFX_REVSTD_WORK* rv, float coloration,
                           float time, float mix, float damping,
                           float predelay);
static int ReverbSTDModify(struct AXFX_REVSTD_WORK* rv, float coloration,
                           float time, float mix, float damping,
                           float predelay);
static void HandleReverb(s32* sptr, struct AXFX_REVSTD_WORK* rv);
static void ReverbSTDCallback(s32* left, s32* right, s32* surround,
                              struct AXFX_REVSTD_WORK* rv);
static void ReverbSTDFree(struct AXFX_REVSTD_WORK* rv);

static void DLsetdelay(struct AXFX_REVSTD_DELAYLINE* dl, s32 lag)
{
    dl->outPoint = dl->inPoint - (lag * 4);
    while (dl->outPoint < 0) {
        dl->outPoint += dl->length;
    }
}

static void DLcreate(struct AXFX_REVSTD_DELAYLINE* dl, s32 max_length)
{
    dl->length = (max_length * 4);
    dl->inputs = __AXFXAlloc(max_length * 4);
    memset(dl->inputs, 0, max_length * 4);
    dl->lastOutput = 0.0f;
    DLsetdelay(dl, max_length >> 1);
    dl->inPoint = 0;
    dl->outPoint = 0;
}

static void DLdelete(struct AXFX_REVSTD_DELAYLINE* dl)
{
    __AXFXFree(dl->inputs);
}

static int ReverbSTDCreate(struct AXFX_REVSTD_WORK* rv, float coloration,
                           float time, float mix, float damping,
                           float predelay)
{
    u8 i;
    u8 k;
    static s32 lens[4] = {
        0x000006FD,
        0x000007CF,
        0x000001B1,
        0x00000095,
    };

    if ((coloration < 0.0f) || (coloration > 1.0f) || (time < 0.01f) ||
        (time > 10.0f) || (mix < 0.0f) || (mix > 1.0f) || (damping < 0.0f) ||
        (damping > 1.0f) || (predelay < 0.0f) || (predelay > 0.1f))
    {
        return 0;
    }

    memset(rv, 0, sizeof(struct AXFX_REVSTD_WORK));
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 2; i++) {
            DLcreate(&rv->C[i + (k * 2)], lens[i] + 2);
            DLsetdelay(&rv->C[i + (k * 2)], lens[i]);
            rv->combCoef[i + (k * 2)] =
                powf(10.0f, (lens[i] * -3) / (32000.0f * time));
        }
        for (i = 0; i < 2; i++) {
            DLcreate(&rv->AP[i + (k * 2)], lens[i + 2] + 2);
            DLsetdelay(&rv->AP[i + (k * 2)], lens[i + 2]);
        }
        rv->lpLastout[k] = 0.0f;
    }
    rv->allPassCoeff = coloration;
    rv->level = mix;
    rv->damping = damping;
    if (rv->damping < 0.05f) {
        rv->damping = 0.05f;
    }
    rv->damping = (1.0f - (0.05f + (0.8f * rv->damping)));
    if (0.0f != predelay) {
        rv->preDelayTime = (32000.0f * predelay);
        for (i = 0; i < 3; i++) {
            rv->preDelayLine[i] = __AXFXAlloc(rv->preDelayTime * 4);
            memset(rv->preDelayLine[i], 0, rv->preDelayTime * 4);
            rv->preDelayPtr[i] = rv->preDelayLine[i];
        }
    } else {
        rv->preDelayTime = 0;
        for (i = 0; i < 3; i++) {
            rv->preDelayPtr[i] = 0;
            rv->preDelayLine[i] = 0;
        }
    }
    return 1;
}

static int ReverbSTDModify(struct AXFX_REVSTD_WORK* rv, float coloration,
                           float time, float mix, float damping,
                           float predelay)
{
    u8 i;

    if ((coloration < 0.0f) || (coloration > 1.0f) || (time < 0.01f) ||
        (time > 10.0f) || (mix < 0.0f) || (mix > 1.0f) || (damping < 0.0f) ||
        (damping > 1.0f) || (predelay < 0.0f) || (predelay > 100.0f))
    {
        return 0;
    }
    rv->allPassCoeff = coloration;
    rv->level = mix;
    rv->damping = damping;
    if (rv->damping < 0.05f) {
        rv->damping = 0.05f;
    }
    rv->damping = (1.0f - (0.05f + (0.8f * rv->damping)));
    for (i = 0; i < 6; i++) {
        DLdelete(&rv->AP[i]);
    }
    for (i = 0; i < 6; i++) {
        DLdelete(&rv->C[i]);
    }
    if (rv->preDelayTime) {
        for (i = 0; i < 3; i++) {
            __AXFXFree(rv->preDelayLine[i]);
        }
    }
    return ReverbSTDCreate(rv, coloration, time, mix, damping, predelay);
}

const static float value0_3 = 0.3f;
const static float value0_6 = 0.6f;
const static double i2fMagic = 4503601774854144.0;

#ifdef PC_AXFX_C
/* C transcription of the asm below (PC port). Three buses follow each other
 * in memory; channel k uses comb C[2k..2k+1], allpass AP[2k..2k+1],
 * lpLastout[k] and preDelay*[k]. Delay-line points are byte offsets. */
static void HandleReverb(s32* sptr, struct AXFX_REVSTD_WORK* rv)
{
    const float ap = rv->allPassCoeff;
    const float damp = rv->damping;
    const float wet = rv->level * 0.6f;
    const float dry = 0.6f - wet;
    int k, n;

    for (k = 0; k < 3; k++) {
        struct AXFX_REVSTD_DELAYLINE* c0 = &rv->C[k * 2];
        struct AXFX_REVSTD_DELAYLINE* c1 = &rv->C[k * 2 + 1];
        struct AXFX_REVSTD_DELAYLINE* a0 = &rv->AP[k * 2];
        struct AXFX_REVSTD_DELAYLINE* a1 = &rv->AP[k * 2 + 1];
        const float cc0 = rv->combCoef[k * 2];
        const float cc1 = rv->combCoef[k * 2 + 1];
        float c0last = c0->lastOutput, c1last = c1->lastOutput;
        float a0last = a0->lastOutput, a1last = a1->lastOutput;
        float lp = rv->lpLastout[k];
        float* pdline = rv->preDelayLine[k];
        float* pdptr = rv->preDelayPtr[k];
        float* pdend = pdline + (rv->preDelayTime - 1);
        s32 c0in = c0->inPoint, c0out = c0->outPoint;
        s32 c1in = c1->inPoint, c1out = c1->outPoint;
        s32 a0in = a0->inPoint, a0out = a0->outPoint;
        s32 a1in = a1->inPoint, a1out = a1->outPoint;

        for (n = 0; n < 160; n++) {
            float x = (float) sptr[n];
            float in = x;
            float f8, f9, f14, o;
            if (rv->preDelayTime != 0) {
                in = *pdptr;
                *pdptr++ = x;
                if (pdptr == pdend) {
                    pdptr = pdline;
                }
            }
            /* two comb filters in parallel */
            f8 = cc0 * c0last + in;
            f9 = cc1 * c1last + in;
            c0->inputs[c0in >> 2] = f8;
            c1->inputs[c1in >> 2] = f9;
            c0in += 4;
            c1in += 4;
            f14 = c0->inputs[c0out >> 2];
            c1last = c1->inputs[c1out >> 2];
            c0out += 4;
            c1out += 4;
            c0last = f14;
            f14 += c1last;
            if (c0in == c0->length) c0in = 0;
            if (c0out == c0->length) c0out = 0;
            if (c1in == c1->length) c1in = 0;
            if (c1out == c1->length) c1out = 0;
            /* allpass 1 */
            f9 = ap * a0last + f14;
            a0->inputs[a0in >> 2] = f9;
            f14 = a0last - ap * f9;
            a0in += 4;
            a0last = a0->inputs[a0out >> 2];
            a0out += 4;
            if (a0in == a0->length) a0in = 0;
            if (a0out == a0->length) a0out = 0;
            /* damping low-pass */
            f14 = f14 * 0.3f;
            f14 = damp * lp + f14;
            lp = f14;
            /* allpass 2 */
            f9 = ap * a1last + f14;
            a1->inputs[a1in >> 2] = f9;
            f14 = a1last - ap * f9;
            a1last = a1->inputs[a1out >> 2];
            a1in += 4;
            a1out += 4;
            if (a1in == a1->length) a1in = 0;
            if (a1out == a1->length) a1out = 0;
            o = wet * f14 + dry * x;
            if (o >= 2147483648.0f) {
                sptr[n] = 0x7FFFFFFF;
            } else if (o <= -2147483648.0f) {
                sptr[n] = (s32) 0x80000000;
            } else {
                sptr[n] = (s32) o; /* fctiwz: truncate */
            }
        }
        c0->inPoint = c0in; c0->outPoint = c0out; c0->lastOutput = c0last;
        c1->inPoint = c1in; c1->outPoint = c1out; c1->lastOutput = c1last;
        a0->inPoint = a0in; a0->outPoint = a0out; a0->lastOutput = a0last;
        a1->inPoint = a1in; a1->outPoint = a1out; a1->lastOutput = a1last;
        rv->lpLastout[k] = lp;
        rv->preDelayPtr[k] = pdptr;
        sptr += 160;
    }
}
#else
asm static void HandleReverb(register s32* sptr,
                             register struct AXFX_REVSTD_WORK* rv)
{
    // clang-format off
    nofralloc
	stwu r1, -144(r1)
	stmw r17, 8(r1)
	stfd f14, 88(r1)
	stfd f15, 96(r1)
	stfd f16, 104(r1)
	stfd f17, 112(r1)
	stfd f18, 120(r1)
	stfd f19, 128(r1)
	stfd f20, 136(r1)
	lis r31, value0_3@ha
	lfs f6, value0_3@l(r31)
	lis r31, value0_6@ha
	lfs f9, value0_6@l(r31)
	lis r31, i2fMagic@ha
	lfd f5, i2fMagic@l(r31)
	lfs f2, AXFX_REVSTD_WORK.allPassCoeff(rv)
	lfs f11, AXFX_REVSTD_WORK.damping(rv)
	lfs f8, AXFX_REVSTD_WORK.level(rv)
	fmuls f3, f8, f9
	fsubs f4, f9, f3
	lis r30, 0x4330 // 176.0f (0x43300000)
	stw r30, 80(r1)
	li r5, 0
L_00000638:
	slwi r31, r5, 3
	add r31, r31, rv
	lfs f19, AXFX_REVSTD_WORK.combCoef[0](r31)
	lfs f20, AXFX_REVSTD_WORK.combCoef[1](r31)
	slwi r31, r5, 2
	add r31, r31, rv
	lfs f7, AXFX_REVSTD_WORK.lpLastout[0](r31)
	lwz r27, AXFX_REVSTD_WORK.preDelayLine[0](r31)
	lwz r28, AXFX_REVSTD_WORK.preDelayPtr[0](r31)
	lwz r31, AXFX_REVSTD_WORK.preDelayTime(rv)
	subi r22, r31, 1
	slwi r22, r22, 2
	add r22, r22, r27
	cmpwi cr7, r31, 0
	mulli r31, r5, 0x28 // sizeof(struct AXFX_REVSTD_DELAYLINE * 2)
	addi r29, rv, AXFX_REVSTD_WORK.C
	add r29, r29, r31
	addi r30, rv, AXFX_REVSTD_WORK.AP
	add r30, r30, r31
	lwz r21, AXFX_REVSTD_DELAYLINE.inPoint    + 0x00(r29) // C array + 0
	lwz r20, AXFX_REVSTD_DELAYLINE.outPoint   + 0x00(r29) // C array + 0
	lwz r19, AXFX_REVSTD_DELAYLINE.inPoint    + 0x14(r29) // C array + 1
	lwz r18, AXFX_REVSTD_DELAYLINE.outPoint   + 0x14(r29) // C array + 1
	lfs f15, AXFX_REVSTD_DELAYLINE.lastOutput + 0x00(r29) // C array + 0
	lfs f16, AXFX_REVSTD_DELAYLINE.lastOutput + 0x14(r29) // C array + 1
	lwz r26, AXFX_REVSTD_DELAYLINE.length     + 0x00(r29) // C array + 0
	lwz r25, AXFX_REVSTD_DELAYLINE.length     + 0x14(r29) // C array + 1
	lwz r7,  AXFX_REVSTD_DELAYLINE.inputs     + 0x00(r29) // C array + 0
	lwz r8,  AXFX_REVSTD_DELAYLINE.inputs     + 0x14(r29) // C array + 1
	lwz r12, AXFX_REVSTD_DELAYLINE.inPoint    + 0x00(r30) // AP array + 0
	lwz r11, AXFX_REVSTD_DELAYLINE.outPoint   + 0x00(r30) // AP array + 0
	lwz r10, AXFX_REVSTD_DELAYLINE.inPoint    + 0x14(r30) // AP array + 1
	lwz r9,  AXFX_REVSTD_DELAYLINE.outPoint   + 0x14(r30) // AP array + 1
	lfs f17, AXFX_REVSTD_DELAYLINE.lastOutput + 0x00(r30) // AP array + 0
	lfs f18, AXFX_REVSTD_DELAYLINE.lastOutput + 0x14(r30) // AP array + 1
	lwz r24, AXFX_REVSTD_DELAYLINE.length     + 0x00(r30) // AP array + 0
	lwz r23, AXFX_REVSTD_DELAYLINE.length     + 0x14(r30) // AP array + 1
	lwz r17, AXFX_REVSTD_DELAYLINE.inputs     + 0x00(r30) // AP array + 0
	lwz r6,  AXFX_REVSTD_DELAYLINE.inputs     + 0x14(r30) // AP array + 1
	lwz r30, 0(sptr)
	xoris r30, r30, 0x8000
	stw r30, 84(r1)
	lfd f12, 80(r1)
	fsubs f12, f12, f5
	li r31, 159
	mtctr r31
L_000006F0:
	fmr f13, f12
	beq cr7, L_00000710
	lfs f13, 0(r28)
	addi r28, r28, 4
	cmpw r28, r22
	stfs f12, -4(r28)
	bne+ L_00000710
	mr r28, r27
L_00000710:
	fmadds f8, f19, f15, f13
	lwzu r29, 4(sptr)
	fmadds f9, f20, f16, f13
	stfsx f8, r7, r21
	addi r21, r21, 4
	stfsx f9, r8, r19
	lfsx f14, r7, r20
	addi r20, r20, 4
	lfsx f16, r8, r18
	cmpw r21, r26
	cmpw cr1, r20, r26
	addi r19, r19, 4
	addi r18, r18, 4
	fmr f15, f14
	cmpw cr5, r19, r25
	fadds f14, f14, f16
	cmpw cr6, r18, r25
	bne+ L_0000075C
	li r21, 0
L_0000075C:
	xoris r29, r29, 0x8000
	fmadds f9, f2, f17, f14
	bne+ cr1, L_0000076C
	li r20, 0
L_0000076C:
	stw r29, 84(r1)
	bne+ cr5, L_00000778
	li r19, 0
L_00000778:
	stfsx f9, r17, r12
	fnmsubs f14, f2, f9, f17
	addi r12, r12, 4
	bne+ cr6, L_0000078C
	li r18, 0
L_0000078C:
	lfsx f17, r17, r11
	cmpw cr5, r12, r24
	addi r11, r11, 4
	cmpw cr6, r11, r24
	bne+ cr5, L_000007A4
	li r12, 0
L_000007A4:
	bne+ cr6, L_000007AC
	li r11, 0
L_000007AC:
	fmuls f14, f14, f6
	lfd f10, 80(r1)
	fmadds f14, f11, f7, f14
	fmadds f9, f2, f18, f14
	fmr f7, f14
	stfsx f9, r6, r10
	fnmsubs f14, f2, f9, f18
	fmuls f8, f4, f12
	lfsx f18, r6, r9
	addi r10, r10, 4
	addi r9, r9, 4
	fmadds f14, f3, f14, f8
	cmpw cr5, r10, r23
	cmpw cr6, r9, r23
	fctiwz f14, f14
	bne+ cr5, L_000007F0
	li r10, 0
L_000007F0:
	bne+ cr6, L_000007F8
	li r9, 0
L_000007F8:
	li r31, -4
	fsubs f12, f10, f5
	stfiwx f14, sptr, r31
	bdnz L_000006F0
	fmr f13, f12
	beq cr7, L_00000828
	lfs f13, 0(r28)
	addi r28, r28, 4
	cmpw r28, r22
	stfs f12, -4(r28)
	bne+ L_00000828
	mr r28, r27
L_00000828:
	fmadds f8, f19, f15, f13
	fmadds f9, f20, f16, f13
	stfsx f8, r7, r21
	addi r21, r21, 4
	stfsx f9, r8, r19
	lfsx f14, r7, r20
	addi r20, r20, 4
	lfsx f16, r8, r18
	cmpw r21, r26
	cmpw cr1, r20, r26
	addi r19, r19, 4
	addi r18, r18, 4
	fmr f15, f14
	cmpw cr5, r19, r25
	fadds f14, f14, f16
	cmpw cr6, r18, r25
	bne+ L_00000870
	li r21, 0
L_00000870:
	fmadds f9, f2, f17, f14
	bne+ cr1, L_0000087C
	li r20, 0
L_0000087C:
	bne+ cr5, L_00000884
	li r19, 0
L_00000884:
	stfsx f9, r17, r12
	fnmsubs f14, f2, f9, f17
	addi r12, r12, 4
	bne+ cr6, L_00000898
	li r18, 0
L_00000898:
	lfsx f17, r17, r11
	cmpw cr5, r12, r24
	addi r11, r11, 4
	cmpw cr6, r11, r24
	bne+ cr5, L_000008B0
	li r12, 0
L_000008B0:
	bne+ cr6, L_000008B8
	li r11, 0
L_000008B8:
	fmuls f14, f14, f6
	fmadds f14, f11, f7, f14
	mulli r31, r5, 0x28 // sizeof(struct AXFX_REVSTD_DELAYLINE * 2)
	fmadds f9, f2, f18, f14
	fmr f7, f14
	addi r29, rv, AXFX_REVSTD_WORK.C
	add r29, r29, r31
	stfsx f9, r6, r10
	fnmsubs f14, f2, f9, f18
	fmuls f8, f4, f12
	lfsx f18, r6, r9
	addi r10, r10, 4
	addi r9, r9, 4
	fmadds f14, f3, f14, f8
	cmpw cr5, r10, r23
	cmpw cr6, r9, r23
	fctiwz f14, f14
	bne+ cr5, L_00000904
	li r10, 0
L_00000904:
	bne+ cr6, L_0000090C
	li r9, 0
L_0000090C:
	addi r30, rv, AXFX_REVSTD_WORK.AP
	add r30, r30, r31
	stfiwx f14, r0, sptr
	stw r21, AXFX_REVSTD_DELAYLINE.inPoint  + 0x00(r29) // C array + 0
	stw r20, AXFX_REVSTD_DELAYLINE.outPoint + 0x00(r29) // C array + 0
	stw r19, AXFX_REVSTD_DELAYLINE.inPoint  + 0x14(r29) // C array + 1
	stw r18, AXFX_REVSTD_DELAYLINE.outPoint + 0x14(r29) // C array + 1
	addi sptr, sptr, 4
	stfs f15, AXFX_REVSTD_DELAYLINE.lastOutput + 0x00(r29) // C array + 0
	stfs f16, AXFX_REVSTD_DELAYLINE.lastOutput + 0x14(r29) // C array + 1
	slwi r31, r5, 2
	add r31, r31, rv
	addi r5, r5, 1
	stw r12, AXFX_REVSTD_DELAYLINE.inPoint  + 0x00(r30) // AP array + 0
	stw r11, AXFX_REVSTD_DELAYLINE.outPoint + 0x00(r30) // AP array + 0
	stw r10, AXFX_REVSTD_DELAYLINE.inPoint  + 0x14(r30) // AP array + 1
	stw r9, AXFX_REVSTD_DELAYLINE.outPoint  + 0x14(r30) // AP array + 1
	cmpwi r5, 3
	stfs f17, AXFX_REVSTD_DELAYLINE.lastOutput + 0x00(r30) // AP array + 0
	stfs f18, AXFX_REVSTD_DELAYLINE.lastOutput + 0x14(r30) // AP array + 1
	stfs f7, AXFX_REVSTD_WORK.lpLastout(r31)
	stw r28, AXFX_REVSTD_WORK.preDelayPtr(r31)
	bne L_00000638
	lfd f14, 88(r1)
	lfd f15, 96(r1)
	lfd f16, 104(r1)
	lfd f17, 112(r1)
	lfd f18, 120(r1)
	lfd f19, 128(r1)
	lfd f20, 136(r1)
	lmw r17, 8(r1)
	addi r1, r1, 144
	blr
    // clang-format on
}

#endif /* PC_AXFX_C */

static void ReverbSTDCallback(s32* left, s32* right, s32* surround,
                              struct AXFX_REVSTD_WORK* rv)
{
    HandleReverb(left, rv);
}

static void ReverbSTDFree(struct AXFX_REVSTD_WORK* rv)
{
    u8 i;

    for (i = 0; i < 6; i++) {
        DLdelete(&rv->AP[i]);
    }
    for (i = 0; i < 6; i++) {
        DLdelete(&rv->C[i]);
    }
    if (rv->preDelayTime) {
        for (i = 0; i < 3; i++) {
            __AXFXFree(rv->preDelayLine[i]);
        }
    }
}

int AXFXReverbStdInit(struct AXFX_REVERBSTD* rev)
{
    int ret;
    int old;

    old = OSDisableInterrupts();
    rev->tempDisableFX = 0;
    ret = ReverbSTDCreate(&rev->rv, rev->coloration, rev->time, rev->mix,
                          rev->damping, rev->preDelay);
    OSRestoreInterrupts(old);
    return ret;
}

int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* rev)
{
    int old;

    old = OSDisableInterrupts();
    ReverbSTDFree(&rev->rv);
    OSRestoreInterrupts(old);
    return 1;
}

int AXFXReverbStdSettings(struct AXFX_REVERBSTD* rev)
{
    int old;

    old = OSDisableInterrupts();
    rev->tempDisableFX = 1;
    ReverbSTDModify(&rev->rv, rev->coloration, rev->time, rev->mix,
                    rev->damping, rev->preDelay);
    rev->tempDisableFX = 0;
    OSRestoreInterrupts(old);
    return 1;
}

void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                           struct AXFX_REVERBSTD* reverb)
{
    if (reverb->tempDisableFX == 0) {
        ReverbSTDCallback(bufferUpdate->left, bufferUpdate->right,
                          bufferUpdate->surround, &reverb->rv);
    }
}
