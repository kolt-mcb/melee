#include "port/pc_scene.h"

#if defined(BUILD_TARGET_PC)

#include "port/log.h"
#include "port/pc_ptr.h"

#include <sc/types.h>
#include <gr/grdatfiles.h>
#include <lb/lbheap.h>

#include <baselib/cobj.h>
#include <baselib/lobj.h>
#include <baselib/wobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Arrays in a scene are NULL-terminated (vi0401.c:169 walks models that way).
 * These caps only exist so a garbage offset cannot turn a bad read into an
 * unbounded scan or a huge allocation. */
#define PC_SCENE_MAX_MODELS 256
#define PC_SCENE_MAX_ANIMS 256
#define PC_SCENE_MAX_CAMERAS 64
#define PC_SCENE_MAX_LIGHTS 64

static u32 be32(const void* p)
{
    const u8* b = (const u8*) p;
    return ((u32) b[0] << 24) | ((u32) b[1] << 16) | ((u32) b[2] << 8) |
           (u32) b[3];
}

static u16 be16(const void* p)
{
    const u8* b = (const u8*) p;
    return (u16) (((u32) b[0] << 8) | (u32) b[1]);
}

static f32 bef32(const void* p)
{
    union {
        u32 u;
        f32 f;
    } v;
    v.u = be32(p);
    return v.f;
}

/* Same rule grdatfiles.c uses: a scene offset is relative to the data
 * section, and an absolute GameCube address is something we cannot follow. */
static void* off_to_ptr(u32 off, u8* dataBase)
{
    if (off == 0 || off >= 0x80000000U) {
        return NULL;
    }
    return dataBase + off;
}

static void* scene_alloc(unsigned long size)
{
    void* p = lbHeap_80015BD0(0, size);
    if (p != NULL) {
        memset(p, 0, (size_t) size);
    }
    return p;
}

/* Count a NULL-terminated array of 32-bit offsets, capped. */
static int count_offsets(const u8* arr, int cap)
{
    int n = 0;
    if (arr == NULL) {
        return 0;
    }
    while (n < cap && be32(arr + (size_t) n * 4) != 0) {
        n++;
    }
    return n;
}

/* Class names are looked up with strcmp against the HSD class registry, so a
 * garbage pointer is a crash rather than a miss. A scene whose class_name does
 * not survive the trip is better off with NULL, which makes HSD fall back to
 * the default class -- exactly what it does for an unnamed object. */
static char* conv_class_name(u32 off, u8* dataBase)
{
    char* p = off_to_ptr(off, dataBase);
    if (p == NULL || !pc_str_sane(p, 64)) {
        return NULL;
    }
    return p;
}

/* HSD_WObjDesc: { char* class_name; Vec3 pos; HSD_RObjDesc* robjdesc; }
 * 0x14 on GameCube. robjdesc drives runtime-computed placement, which no
 * scene here uses; it is left NULL rather than dragged in unconverted. */
static HSD_WObjDesc* conv_wobj(u32 off, u8* dataBase)
{
    const u8* r = off_to_ptr(off, dataBase);
    HSD_WObjDesc* out;

    if (r == NULL) {
        return NULL;
    }
    out = scene_alloc(sizeof(HSD_WObjDesc));
    if (out == NULL) {
        return NULL;
    }
    out->class_name = conv_class_name(be32(r + 0x00), dataBase);
    out->pos.x = bef32(r + 0x04);
    out->pos.y = bef32(r + 0x08);
    out->pos.z = bef32(r + 0x0C);
    out->robjdesc = NULL;
    return out;
}

static Vec3* conv_vec3(u32 off, u8* dataBase)
{
    const u8* r = off_to_ptr(off, dataBase);
    Vec3* out;

    if (r == NULL) {
        return NULL;
    }
    out = scene_alloc(sizeof(Vec3));
    if (out == NULL) {
        return NULL;
    }
    out->x = bef32(r + 0x00);
    out->y = bef32(r + 0x04);
    out->z = bef32(r + 0x08);
    return out;
}

/* HSD_CObjDesc, 0x40 on GameCube. The union's members share a 0x30-byte
 * common head and differ only in the trailing floats, so read the head once
 * and pick the tail by projection_type. */
static HSD_CObjDesc* conv_cobj(u32 off, u8* dataBase)
{
    const u8* r = off_to_ptr(off, dataBase);
    HSD_CObjDesc* out;

    if (r == NULL) {
        return NULL;
    }
    out = scene_alloc(sizeof(HSD_CObjDesc));
    if (out == NULL) {
        return NULL;
    }

    out->common.class_name = conv_class_name(be32(r + 0x00), dataBase);
    out->common.flags = be16(r + 0x04);
    out->common.projection_type = be16(r + 0x06);
    out->common.viewport.xmin = (s16) be16(r + 0x08);
    out->common.viewport.xmax = (s16) be16(r + 0x0A);
    out->common.viewport.ymin = (s16) be16(r + 0x0C);
    out->common.viewport.ymax = (s16) be16(r + 0x0E);
    out->common.scissor.left = be16(r + 0x10);
    out->common.scissor.right = be16(r + 0x12);
    out->common.scissor.top = be16(r + 0x14);
    out->common.scissor.bottom = be16(r + 0x16);
    out->common.eyepos = conv_wobj(be32(r + 0x18), dataBase);
    out->common.interest = conv_wobj(be32(r + 0x1C), dataBase);
    out->common.roll = bef32(r + 0x20);
    out->common.up_vector = conv_vec3(be32(r + 0x24), dataBase);
    out->common.nnear = bef32(r + 0x28);
    out->common.ffar = bef32(r + 0x2C);

    switch (out->common.projection_type) {
    case PROJ_PERSPECTIVE:
        out->perspective.fov = bef32(r + 0x30);
        out->perspective.aspect = bef32(r + 0x34);
        break;
    case PROJ_FRUSTUM:
    case PROJ_ORTHO:
        out->frustum.top = bef32(r + 0x30);
        out->frustum.bottom = bef32(r + 0x34);
        out->frustum.left = bef32(r + 0x38);
        out->frustum.right = bef32(r + 0x3C);
        break;
    default:
        PORT_LOG_WARN("pc_conv_SceneDesc: camera has unknown projection "
                      "type %u\n",
                      (unsigned) out->common.projection_type);
        break;
    }
    if (getenv("MELEE_SCENE_TRACE") != NULL) {
        fprintf(stderr,
                "[SCENE] cobj class=%s flags=0x%x proj=%u vp=(%d,%d,%d,%d) "
                "near=%g far=%g eye=%p int=%p\n",
                out->common.class_name ? out->common.class_name : "(none)",
                (unsigned) out->common.flags,
                (unsigned) out->common.projection_type,
                out->common.viewport.xmin, out->common.viewport.xmax,
                out->common.viewport.ymin, out->common.viewport.ymax,
                (double) out->common.nnear, (double) out->common.ffar,
                (void*) out->common.eyepos, (void*) out->common.interest);
    }
    return out;
}

/* The three attenuation blocks HSD_LObjLoadDesc reaches through
 * HSD_LightDesc::u. All of them are 4-byte scalars, so a swap is enough --
 * which one it is depends on flags/attnflags, exactly as lobj.c decides. */
static void* conv_light_attn(u32 off, u8* dataBase, unsigned long size)
{
    const u8* r = off_to_ptr(off, dataBase);
    u32* out;
    unsigned i;

    if (r == NULL) {
        return NULL;
    }
    out = scene_alloc(size);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < size / 4u; i++) {
        out[i] = be32(r + i * 4u);
    }
    return out;
}

/* HSD_LightDesc, 0x18 on GameCube, and a `next` chain. */
static HSD_LightDesc* conv_light(u32 off, u8* dataBase, int depth)
{
    const u8* r = off_to_ptr(off, dataBase);
    HSD_LightDesc* out;
    u32 u_off;

    if (r == NULL || depth > 16) {
        return NULL;
    }
    out = scene_alloc(sizeof(HSD_LightDesc));
    if (out == NULL) {
        return NULL;
    }
    out->class_name = conv_class_name(be32(r + 0x00), dataBase);
    out->flags = be16(r + 0x08);
    out->attnflags = be16(r + 0x0A);
    out->color.r = ((const u8*) r)[0x0C];
    out->color.g = ((const u8*) r)[0x0D];
    out->color.b = ((const u8*) r)[0x0E];
    out->color.a = ((const u8*) r)[0x0F];
    out->position = conv_wobj(be32(r + 0x10), dataBase);
    out->interest = conv_wobj(be32(r + 0x14), dataBase);

    u_off = be32(r + 0x18);
    switch (out->flags & LOBJ_TYPE_MASK) {
    case LOBJ_POINT:
        out->u.p = (out->attnflags & LOBJ_LIGHT_ATTN)
                       ? conv_light_attn(u_off, dataBase,
                                         sizeof(struct HSD_LightAttn))
                       : conv_light_attn(u_off, dataBase,
                                         sizeof(struct HSD_LightPointDesc));
        break;
    case LOBJ_SPOT:
        out->u.p = (out->attnflags != 0)
                       ? conv_light_attn(u_off, dataBase,
                                         sizeof(struct HSD_LightAttn))
                       : conv_light_attn(u_off, dataBase,
                                         sizeof(struct HSD_LightSpotDesc));
        break;
    default:
        /* Infinite and ambient lights do not read the union. */
        out->u.p = NULL;
        break;
    }

    out->next = conv_light(be32(r + 0x04), dataBase, depth + 1);
    if (getenv("MELEE_LOBJLOG") != NULL) {
        Vec3 p = { 0, 0, 0 }, it = { 0, 0, 0 };
        if (out->position != NULL) p = out->position->pos;
        if (out->interest != NULL) it = out->interest->pos;
        fprintf(stderr,
                "[LIGHTCONV] type=%u attn=0x%x colour=(%u,%u,%u,%u) "
                "pos=(%.1f,%.1f,%.1f) interest=(%.1f,%.1f,%.1f)\n",
                (unsigned) (out->flags & LOBJ_TYPE_MASK),
                (unsigned) out->attnflags, out->color.r, out->color.g,
                out->color.b, out->color.a, (double) p.x, (double) p.y,
                (double) p.z, (double) it.x, (double) it.y, (double) it.z);
    }
    return out;
}

/* A NULL-terminated array of offsets to anim-joint trees, converted with the
 * existing grdatfiles.c walkers. */
static void** conv_anim_array(u32 off, u8* dataBase, int kind)
{
    const u8* arr = off_to_ptr(off, dataBase);
    void** out;
    int n, i;

    n = count_offsets(arr, PC_SCENE_MAX_ANIMS);
    if (n == 0) {
        return NULL;
    }
    out = scene_alloc(sizeof(void*) * (unsigned long) (n + 1));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        const u8* p = off_to_ptr(be32(arr + (size_t) i * 4), dataBase);
        if (p == NULL) {
            continue;
        }
        switch (kind) {
        case 0:
            out[i] = grDatFiles_ConvertAnimJointTreeGCNtoX64(p, dataBase, 0);
            break;
        case 1:
            out[i] =
                grDatFiles_ConvertMatAnimJointTreeGCNtoX64(p, dataBase, 0);
            break;
        default:
            out[i] =
                grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(p, dataBase, 0);
            break;
        }
    }
    out[n] = NULL;
    return out;
}

/* Models are shared: the same digit model is asked for by several HUD
 * elements, and converting it twice would leak a second joint tree. */
#define PC_MODEL_CACHE 64
static struct {
    u32 off;
    DynamicModelDesc* conv;
} pc_model_cache[PC_MODEL_CACHE];
static int pc_model_cache_n;

static DynamicModelDesc* conv_model(u32 off, u8* dataBase)
{
    const u8* r = off_to_ptr(off, dataBase);
    DynamicModelDesc* out;
    const u8* joint;
    int i;

    if (r == NULL) {
        return NULL;
    }
    for (i = 0; i < pc_model_cache_n; i++) {
        if (pc_model_cache[i].off == off) {
            return pc_model_cache[i].conv;
        }
    }
    out = scene_alloc(sizeof(DynamicModelDesc));
    if (out == NULL) {
        return NULL;
    }
    joint = off_to_ptr(be32(r + 0x00), dataBase);
    if (joint != NULL) {
        out->joint = grDatFiles_ConvertJointTreeGCNtoX64(joint, dataBase, 0,
                                                         NULL);
    }
    out->anims = (HSD_AnimJoint**) conv_anim_array(be32(r + 0x04), dataBase, 0);
    out->matanims =
        (HSD_MatAnimJoint**) conv_anim_array(be32(r + 0x08), dataBase, 1);
    out->shapeanims =
        (HSD_ShapeAnimJoint**) conv_anim_array(be32(r + 0x0C), dataBase, 2);
    if (pc_model_cache_n < PC_MODEL_CACHE) {
        pc_model_cache[pc_model_cache_n].off = off;
        pc_model_cache[pc_model_cache_n].conv = out;
        pc_model_cache_n++;
    }
    return out;
}

/* A NULL-terminated array of LightList offsets, each entry
 * { HSD_LightDesc* desc; HSD_LightAnim** anims; }. Both the array and the
 * LightList itself have to be rebuilt rather than rebased: the entries are
 * 4-byte GameCube offsets where x86_64 wants 8-byte pointers, and LightList
 * grows from 8 bytes to 16.
 *
 * Scenes reach this through their SceneDesc, but stages need it too --
 * UnkStageDat_x8_t::x18 is the stage's own light list, and until it was
 * converted Ground_801C466C_inline could only return NULL, so every stage
 * fell back to the generic default light list in Ground_803E06C8. */
/* A slot in archive data holding one 32-bit descriptor offset. Scenes reach
 * their camera and lights through a SceneDesc, but some screens keep a bare
 * table of descriptor offsets at a public symbol instead -- the character
 * select's "MnSelectChrDataTable" is four of them. Such a table cannot be
 * read in place: each slot is 4 bytes on GameCube and the struct declaring
 * them is 8-byte-per-pointer here, so field two onward lands in the wrong
 * place and field one splices two offsets into one number. */
/* The same two descriptors, but addressed directly rather than through a slot
 * holding an offset. HSD_ArchiveGetPublicAddress hands back a pointer into the
 * archive's data section, so recover the offset and convert from there. */
HSD_CObjDesc* pc_conv_CObjDescRaw(const void* raw, u8* dataBase)
{
    if (raw == NULL || dataBase == NULL || (const u8*) raw < dataBase) {
        return NULL;
    }
    return conv_cobj((u32) ((const u8*) raw - dataBase), dataBase);
}

HSD_CObjDesc* pc_conv_CObjDescAt(const void* slot, u8* dataBase)
{
    if (slot == NULL) {
        return NULL;
    }
    return conv_cobj(be32(slot), dataBase);
}

HSD_LightDesc* pc_conv_LightDescAt(const void* slot, u8* dataBase)
{
    if (slot == NULL) {
        return NULL;
    }
    return conv_light(be32(slot), dataBase, 0);
}

LightList** pc_conv_LightListArray(const void* arrBase, u8* dataBase)
{
    const u8* arr = arrBase;
    LightList** out;
    int n, i;

    if (arr == NULL) {
        return NULL;
    }
    n = count_offsets(arr, PC_SCENE_MAX_LIGHTS);
    if (n <= 0) {
        return NULL;
    }
    out = scene_alloc(sizeof(LightList*) * (unsigned long) (n + 1));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        const u8* e = off_to_ptr(be32(arr + (size_t) i * 4), dataBase);
        LightList* ll;
        out[i] = NULL;
        if (e == NULL) {
            continue;
        }
        ll = scene_alloc(sizeof(LightList));
        if (ll == NULL) {
            continue;
        }
        ll->desc = conv_light(be32(e + 0x00), dataBase, 0);
        ll->anims = NULL;
        out[i] = ll;
    }
    out[n] = NULL;
    return out;
}

DynamicModelDesc** pc_conv_ModelDescArray(const void* arrBase, u8* dataBase)
{
    static struct {
        const void* raw;
        DynamicModelDesc** conv;
    } cache[16];
    static int cache_n;
    const u8* arr = arrBase;
    DynamicModelDesc** out;
    int n, i;

    if (!pc_ptr_sane(arrBase) || !pc_ptr_sane(dataBase)) {
        return NULL;
    }
    for (i = 0; i < cache_n; i++) {
        if (cache[i].raw == arrBase) {
            return cache[i].conv;
        }
    }
    n = count_offsets(arr, PC_SCENE_MAX_MODELS);
    if (n == 0) {
        return NULL;
    }
    out = scene_alloc(sizeof(DynamicModelDesc*) * (unsigned long) (n + 1));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        out[i] = conv_model(be32(arr + (size_t) i * 4), dataBase);
    }
    out[n] = NULL;
    if (cache_n < 16) {
        cache[cache_n].raw = arrBase;
        cache[cache_n].conv = out;
        cache_n++;
    }
    return out;
}

DynamicModelDesc* pc_conv_ModelDescAt(const void* slot, u8* dataBase)
{
    if (!pc_ptr_sane(slot) || !pc_ptr_sane(dataBase)) {
        return NULL;
    }
    return conv_model(be32(slot), dataBase);
}

/* Same conversion, but for a public symbol that names the DynamicModelDesc
 * itself rather than a slot holding its offset -- HSD_ArchiveGetPublicAddress
 * hands back `archive->data + offset`, so the struct is already located and
 * only its four fields need following. */
DynamicModelDesc* pc_conv_ModelDescRaw(const void* raw, u8* dataBase)
{
    if (!pc_ptr_sane(raw) || !pc_ptr_sane(dataBase) ||
        (const u8*) raw < dataBase)
    {
        return NULL;
    }
    return conv_model((u32) ((const u8*) raw - dataBase), dataBase);
}

/* Converting the same scene twice would leak a whole second tree, and callers
 * legitimately ask more than once (the HUD is rebuilt per match). */
#define PC_SCENE_CACHE 16
static struct {
    const void* raw;
    SceneDesc* conv;
} pc_scene_cache[PC_SCENE_CACHE];
static int pc_scene_cache_n;

SceneDesc* pc_conv_SceneDesc(const void* raw, u8* dataBase)
{
    const u8* r = raw;
    SceneDesc* out;
    int i, n;

    if (!pc_ptr_sane(raw) || !pc_ptr_sane(dataBase)) {
        return NULL;
    }
    for (i = 0; i < pc_scene_cache_n; i++) {
        if (pc_scene_cache[i].raw == raw) {
            return pc_scene_cache[i].conv;
        }
    }

    out = scene_alloc(sizeof(SceneDesc));
    if (out == NULL) {
        return NULL;
    }

    /* +0x00 models: NULL-terminated array of DynamicModelDesc offsets. */
    {
        const u8* arr = off_to_ptr(be32(r + 0x00), dataBase);
        n = count_offsets(arr, PC_SCENE_MAX_MODELS);
        if (n > 0) {
            out->models = scene_alloc(sizeof(DynamicModelDesc*) *
                                      (unsigned long) (n + 1));
            if (out->models != NULL) {
                for (i = 0; i < n; i++) {
                    out->models[i] =
                        conv_model(be32(arr + (size_t) i * 4), dataBase);
                }
                out->models[n] = NULL;
            }
        }
    }

    /* +0x04 cameras: an array of { desc, anims } pairs -- not pointers -- so
     * it terminates on a null desc rather than a null element. */
    {
        const u8* arr = off_to_ptr(be32(r + 0x04), dataBase);
        n = 0;
        if (arr != NULL) {
            while (n < PC_SCENE_MAX_CAMERAS &&
                   be32(arr + (size_t) n * 8) != 0)
            {
                n++;
            }
        }
        if (n > 0) {
            out->cameras =
                scene_alloc(sizeof(*out->cameras) * (unsigned long) (n + 1));
            if (out->cameras != NULL) {
                for (i = 0; i < n; i++) {
                    out->cameras[i].desc =
                        conv_cobj(be32(arr + (size_t) i * 8), dataBase);
                    out->cameras[i].anims = NULL;
                }
                out->cameras[n].desc = NULL;
                out->cameras[n].anims = NULL;
            }
        }
    }

    /* +0x08 lights. */
    out->lights = pc_conv_LightListArray(off_to_ptr(be32(r + 0x08), dataBase),
                                         dataBase);

    /* +0x0C fogs is left NULL: an unconverted HSD_FogDesc would be worse
     * than an absent one. Readers must cope -- gm_1832.c's All-Star intro
     * does read fogs[0], and went through a null here. */
    out->fogs = NULL;

    if (pc_scene_cache_n < PC_SCENE_CACHE) {
        pc_scene_cache[pc_scene_cache_n].raw = raw;
        pc_scene_cache[pc_scene_cache_n].conv = out;
        pc_scene_cache_n++;
    }

    if (getenv("MELEE_SCENE_TRACE") != NULL) {
        int nm = 0, nc = 0, nl = 0;
        if (out->models != NULL) {
            while (out->models[nm] != NULL) nm++;
        }
        if (out->cameras != NULL) {
            while (out->cameras[nc].desc != NULL) nc++;
        }
        if (out->lights != NULL) {
            while (out->lights[nl] != NULL) nl++;
        }
        fprintf(stderr,
                "[SCENE] %p -> %p: %d models, %d cameras, %d lights\n", raw,
                (void*) out, nm, nc, nl);
    }
    return out;
}

#endif /* BUILD_TARGET_PC */
