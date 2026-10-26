/* SPDX-License-Identifier: GPL-3.0-only
 * Catalogue-committed A/B object store. Header-last protocol and nibble CRC-32
 * follow storage.c (Leo Kuroshita / Hugelton Instruments, GPL-3.0-only).
 * Wire integers are little endian, as on the firmware target. No allocation,
 * global flash state, automatic formatting, or writes during init/load.
 *
 * Objects 0..7: eight patterns, A/B 64 KiB total.
 * Objects 8..11: four paired-kit objects (eight kits), A/B 32 KiB total.
 * Objects 12..15: four 32-sound objects (128 sounds), A/B 32 KiB total.
 * Each object copy has 3840 payload bytes. Catalogue A/B: 8 KiB.
 * Reserved last 24 KiB is never accessed. Callbacks return zero on success;
 * erase handles one aligned 4096-byte sector, program never crosses a page.
 *
 * SINGLE WRITER: caller serializes operations. Stage writes the side opposite
 * the COMMITTED catalogue, never the physically newest orphan. Commit publishes
 * all staged objects together. A failed catalogue write can be ambiguous: load
 * must succeed before another stage/commit. Read failures never authorize writes.
 */
#ifndef MNM_STORE_H
#define MNM_STORE_H
#include <stdint.h>
#ifndef MNM_NO_LIBC_HEADERS
#include <string.h>
#endif
#define MMS_BASE 0xB4000u
#define MMS_END 0xDC000u
#define MMS_RESERVE 0xD6000u
#define MMS_SECTOR 4096u
#define MMS_PAGE 256u
#define MMS_PAYLOAD_MAX 3840u
#define MMS_OBJECTS 16u
#define MMS_CATALOG 16u
#define MMS_VERSION 1u
#define MMS_MAGIC 0x31534D4Du
/* Absent object is distinct from a present object with a zero-length payload. */
enum { MMS_OK=0, MMS_INVALID=-1, MMS_IO=-2, MMS_CORRUPT=-3,
       MMS_NOT_FOUND=-4, MMS_NOT_LOADED=-5, MMS_APPROVAL_REQUIRED=-6 };
enum { MMS_VERIFIED_BACKUP=1, MMS_OWNER_WAIVER=2 };
typedef struct {
    void *context;
    int (*read)(void *, uint32_t, void *, uint32_t);
    int (*program)(void *, uint32_t, const void *, uint32_t);
    int (*erase)(void *, uint32_t);
} mms_io;
typedef struct {
    uint32_t magic;
    uint16_t type, side;
    uint32_t generation, length, crc, reserved[2], header_crc;
} mms_header;
typedef struct {
    uint32_t generation, crc;
    uint16_t length;
    uint8_t side, present;
} mms_ref;
typedef struct {
    uint32_t version, objects;
    mms_ref ref[MMS_OBJECTS];
} mms_catalog;
typedef struct {
    mms_io io;
    mms_catalog committed, pending;
    uint32_t generation;
    uint8_t side, loaded;
} mms_store;
_Static_assert(sizeof(mms_header)==32, "object header layout");
_Static_assert(sizeof(mms_ref)==12 && sizeof(mms_catalog)==200, "catalogue layout");

static inline uint32_t mms_crc_step(uint32_t c, const void *data, uint32_t n)
{
    static const uint32_t table[16]={0x00000000u,0x1DB71064u,0x3B6E20C8u,0x26D930ACu,
        0x76DC4190u,0x6B6B51F4u,0x4DB26158u,0x5005713Cu,0xEDB88320u,0xF00F9344u,
        0xD6D6A3E8u,0xCB61B38Cu,0x9B64C2B0u,0x86D3D2D4u,0xA00AE278u,0xBDBDF21Cu};
    const uint8_t *p=(const uint8_t *)data;
    while(n--) {c^=*p++;c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}
    return c;
}
static inline uint32_t mms_crc(const void *data,uint32_t n)
{
    return ~mms_crc_step(0xFFFFFFFFu,data,n);
}
/* Invalid addresses are rejected before callbacks; UINT32_MAX is never passed. */
static inline uint32_t mms_address(unsigned object,unsigned side)
{
    return object<=MMS_CATALOG && side<2 ? MMS_BASE+object*2*MMS_SECTOR+side*MMS_SECTOR : UINT32_MAX;
}
static inline int mms_init(mms_store *s,mms_io io)
{
    if(!s) return MMS_INVALID;
    memset(s,0,sizeof(*s));s->io=io;
    return io.read && io.program && io.erase ? MMS_OK : MMS_INVALID;
}
static inline int mms_head(mms_store *s,unsigned object,unsigned side,mms_header *h)
{
    uint32_t base=mms_address(object,side);
    if(base==UINT32_MAX || !s->io.read) return MMS_INVALID;
    if(s->io.read(s->io.context,base,h,sizeof(*h))) return MMS_IO;
    if(h->magic!=MMS_MAGIC || h->type!=object || h->side!=side ||
       h->length>MMS_PAYLOAD_MAX || h->reserved[0]!=MMS_VERSION ||
       h->header_crc!=mms_crc(h,28)) return MMS_CORRUPT;
    return MMS_OK;
}
/* Verify a payload in bounded RAM; dst may be NULL for validation only. */
static inline int mms_body(mms_store *s,unsigned object,unsigned side,
                           const mms_header *h,void *dst)
{
    uint8_t page[MMS_PAGE];uint32_t crc=0xFFFFFFFFu;
    for(uint32_t off=0;off<h->length;off+=MMS_PAGE) {
        uint32_t n=h->length-off;if(n>MMS_PAGE)n=MMS_PAGE;
        if(s->io.read(s->io.context,mms_address(object,side)+MMS_PAGE+off,page,n)) return MMS_IO;
        crc=mms_crc_step(crc,page,n);
        if(dst)memcpy((uint8_t *)dst+off,page,n);
    }
    return ~crc==h->crc ? MMS_OK : MMS_CORRUPT;
}
static inline int mms_ref_check(mms_store *s,unsigned object,const mms_ref *r,void *dst)
{
    if(object>=MMS_OBJECTS || r->present!=1 || r->side>1 || r->length>MMS_PAYLOAD_MAX) return MMS_CORRUPT;
    mms_header h;int rc=mms_head(s,object,r->side,&h);if(rc)return rc;
    if(h.generation!=r->generation || h.length!=r->length || h.crc!=r->crc) return MMS_CORRUPT;
    return mms_body(s,object,r->side,&h,dst);
}
static inline int mms_catalog_check(mms_store *s,const mms_catalog *c)
{
    if(c->version!=MMS_VERSION || c->objects!=MMS_OBJECTS) return MMS_CORRUPT;
    for(unsigned i=0;i<MMS_OBJECTS;++i) {
        const mms_ref *r=&c->ref[i];
        if(!r->present) {
            if(r->generation || r->crc || r->length || r->side)return MMS_CORRUPT;
        } else {
            int rc=mms_ref_check(s,i,r,0);if(rc)return rc;
        }
    }
    return MMS_OK;
}
/* Read both catalogue headers, then try newest complete snapshot first (A on a
 * tie). Orphan headers do not select objects. Any I/O uncertainty blocks writes;
 * corruption may fall back only to a fully verified older catalogue snapshot. */
static inline int mms_load(mms_store *s)
{
    if(!s || !s->io.read)return MMS_INVALID;
    s->loaded=0;
    mms_header h[2];int status[2];
    for(unsigned side=0;side<2;++side) {
        status[side]=mms_head(s,MMS_CATALOG,side,&h[side]);
        if(status[side]==MMS_IO)return MMS_IO;
    }
    unsigned first=0;
    if(!status[1] && (status[0] || (h[1].generation!=h[0].generation &&
        h[1].generation-h[0].generation<0x80000000u)))first=1;
    for(unsigned n=0;n<2;++n) {
        unsigned side=first^n;
        if(status[side] || h[side].length!=sizeof(mms_catalog))continue;
        mms_catalog candidate;
        int rc=mms_body(s,MMS_CATALOG,side,&h[side],&candidate);
        if(!rc)rc=mms_catalog_check(s,&candidate);
        if(rc==MMS_IO)return rc;
        if(rc)continue;
        s->committed=s->pending=candidate;s->generation=h[side].generation;
        s->side=(uint8_t)side;s->loaded=1;return MMS_OK;
    }
    return MMS_CORRUPT;
}
/* Return nonnegative byte length; destination may be modified on read failure.
 * Reads always use the committed snapshot, never staged payloads. */
static inline int mms_read_object(mms_store *s,unsigned object,void *dst,uint32_t capacity)
{
    if(!s || object>=MMS_OBJECTS)return MMS_INVALID;
    if(!s->loaded)return MMS_NOT_LOADED;
    const mms_ref *r=&s->committed.ref[object];
    if(!r->present)return MMS_NOT_FOUND;
    if(capacity<r->length || (!dst && r->length))return MMS_INVALID;
    int rc=mms_ref_check(s,object,r,dst);
    if(rc==MMS_IO)s->loaded=0;
    return rc ? rc : (int)r->length;
}
/* Low-level write. The caller-selected side, not physical sequence numbers,
 * determines what is safe to erase. RAM page copies support XIP source data. */
static inline int mms_write_copy(mms_store *s,unsigned object,unsigned side,
                                 uint32_t generation,const void *src,uint32_t length,
                                 mms_ref *written)
{
    uint32_t base=mms_address(object,side);
    if(base==UINT32_MAX || length>MMS_PAYLOAD_MAX || (!src && length) ||
       !s->io.erase || !s->io.program || !s->io.read)return MMS_INVALID;
    mms_header h={MMS_MAGIC,(uint16_t)object,(uint16_t)side,generation,length,
                  mms_crc(src,length),{MMS_VERSION,0xFFFFFFFFu},0};
    h.header_crc=mms_crc(&h,28);
    if(s->io.erase(s->io.context,base))return MMS_IO;
    for(uint32_t off=0;off<length;off+=MMS_PAGE) {
        uint8_t page[MMS_PAGE];uint32_t n=length-off;if(n>MMS_PAGE)n=MMS_PAGE;
        memcpy(page,(const uint8_t *)src+off,n);
        if(s->io.program(s->io.context,base+MMS_PAGE+off,page,n))return MMS_IO;
    }
    if(s->io.program(s->io.context,base,&h,sizeof(h)))return MMS_IO;
    mms_header check;int rc=mms_head(s,object,side,&check);
    if(rc)return rc;
    if(memcmp(&h,&check,sizeof(h)))return MMS_CORRUPT;
    rc=mms_body(s,object,side,&check,0);if(rc)return rc;
    if(written)*written=(mms_ref){generation,h.crc,(uint16_t)length,(uint8_t)side,1};
    return MMS_OK;
}
static inline int mms_stage(mms_store *s,unsigned object,const void *src,uint32_t length)
{
    if(!s || object>=MMS_OBJECTS || length>MMS_PAYLOAD_MAX || (!src && length))return MMS_INVALID;
    if(!s->loaded)return MMS_NOT_LOADED;
    const mms_ref *old=&s->committed.ref[object];
    unsigned side=old->present ? old->side^1u : 0;
    uint32_t generation=old->present ? old->generation+1u : 1u;
    /* Drop an earlier staging of this object before replacing it. A failed
     * restage cannot later publish the previous pending ref by accident. */
    s->pending.ref[object]=*old;
    return mms_write_copy(s,object,side,generation,src,length,&s->pending.ref[object]);
}
static inline int mms_abort(mms_store *s)
{
    if(!s || !s->loaded)return MMS_NOT_LOADED;
    s->pending=s->committed;return MMS_OK;
}
static inline int mms_commit(mms_store *s)
{
    if(!s || !s->loaded)return MMS_NOT_LOADED;
    int rc=mms_catalog_check(s,&s->pending);
    if(rc) {s->loaded=0;return rc;}
    unsigned side=s->side^1u;uint32_t generation=s->generation+1u;
    /* Once a catalogue write begins, its completion can be ambiguous even if
     * the callback reports failure. Only reload may resolve that uncertainty. */
    s->loaded=0;
    rc=mms_write_copy(s,MMS_CATALOG,side,generation,&s->pending,sizeof(s->pending),0);
    if(rc)return rc;
    s->committed=s->pending;s->side=(uint8_t)side;s->generation=generation;s->loaded=1;
    return MMS_OK;
}
/* Explicit destructive logical format: verified backup or explicit owner waiver.
 * Only catalogue sectors are erased; old object bytes become unreferenced and
 * are reclaimed on staging. Reserve and unrelated flash are never touched. */
static inline int mms_format(mms_store *s,int authorization)
{
    if(authorization!=MMS_VERIFIED_BACKUP && authorization!=MMS_OWNER_WAIVER)return MMS_APPROVAL_REQUIRED;
    if(!s || !s->io.read || !s->io.erase || !s->io.program)return MMS_INVALID;
    s->loaded=0;
    for(unsigned side=0;side<2;++side)
        if(s->io.erase(s->io.context,mms_address(MMS_CATALOG,side)))return MMS_IO;
    mms_catalog empty;memset(&empty,0,sizeof(empty));empty.version=MMS_VERSION;empty.objects=MMS_OBJECTS;
    int rc=mms_write_copy(s,MMS_CATALOG,0,1,&empty,sizeof(empty),0);if(rc)return rc;
    s->committed=s->pending=empty;s->generation=1;s->side=0;s->loaded=1;return MMS_OK;
}
#endif
