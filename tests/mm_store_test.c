/* SPDX-License-Identifier: GPL-3.0-only
 * In-memory NOR only: exhaustive write-boundary failures, torn writes and retry. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../firmware/src/mm_store.h"
static uint8_t flash[MMS_END-MMS_BASE], baseline[sizeof(flash)];
static unsigned mutations, reads, fail_at, read_fail_at, cut_mode, powered_off;
static int bounds(uint32_t off,uint32_t n)
{
    return off>=MMS_BASE && off<MMS_RESERVE && n<=MMS_RESERVE-off;
}
static int rd(void *ctx,uint32_t off,void *dst,uint32_t n)
{
    (void)ctx;assert(bounds(off,n));++reads;
    if(powered_off || (read_fail_at && reads==read_fail_at))return -1;
    memcpy(dst,flash+(off-MMS_BASE),n);return 0;
}
static int mutate(uint32_t off,const void *src,uint32_t n,int erase)
{
    assert(bounds(off,n));if(powered_off)return -1;
    ++mutations;int fail=fail_at && mutations==fail_at;
    unsigned amount=fail ? cut_mode==0?0:cut_mode==1?n/2:n : n;
    for(unsigned i=0;i<amount;++i) {
        uint8_t *b=&flash[off-MMS_BASE+i];
        if(erase)*b=255;
        else {uint8_t v=((const uint8_t *)src)[i];assert((*b&v)==v);*b&=v;}
    }
    if(fail){powered_off=1;return -1;}return 0;
}
static int er(void *ctx,uint32_t off)
{
    (void)ctx;assert(!(off&(MMS_SECTOR-1)));return mutate(off,0,MMS_SECTOR,1);
}
static int pg(void *ctx,uint32_t off,const void *src,uint32_t n)
{
    (void)ctx;assert(n && n<=MMS_PAGE && (off%MMS_PAGE)+n<=MMS_PAGE);
    return mutate(off,src,n,0);
}
static mms_io io={0,rd,pg,er};
static uint8_t old0[700],old1[513],new0[700],new1[513],last0[700],readback[MMS_PAYLOAD_MAX];
static void operational(void)
{
    mutations=reads=fail_at=read_fail_at=cut_mode=powered_off=0;
}
static void reopen(mms_store *s)
{
    operational();assert(mms_init(s,io)==0);assert(mms_load(s)==0);
}
static int transaction(mms_store *s)
{
    int rc=mms_stage(s,0,new0,sizeof new0);if(rc)return rc;
    rc=mms_stage(s,1,new1,sizeof new1);if(rc)return rc;
    return mms_commit(s);
}
static void expect(mms_store *s,unsigned object,const uint8_t *value,unsigned n)
{
    assert(mms_read_object(s,object,readback,sizeof readback)==(int)n);
    assert(!memcmp(readback,value,n));
}
static void setup(void)
{
    operational();memset(flash,255,sizeof flash);
    /* Reserve sentinel proves it is never erased, read or programmed. */
    memset(flash+(MMS_RESERVE-MMS_BASE),0xA5,MMS_END-MMS_RESERVE);
    memset(old0,0x11,sizeof old0);memset(old1,0x22,sizeof old1);
    memset(new0,0x33,sizeof new0);memset(new1,0x44,sizeof new1);memset(last0,0x55,sizeof last0);
    mms_store s;assert(mms_init(&s,io)==0);assert(mms_load(&s)==MMS_CORRUPT);assert(!mutations);
    assert(mms_format(&s,0)==MMS_APPROVAL_REQUIRED);assert(mms_format(&s,3)==MMS_APPROVAL_REQUIRED);assert(!mutations);
    assert(mms_stage(&s,0,old0,sizeof old0)==MMS_NOT_LOADED);
    assert(mms_format(&s,MMS_OWNER_WAIVER)==0);assert(mms_read_object(&s,0,readback,sizeof readback)==MMS_NOT_FOUND);
    assert(mms_stage(&s,0,old0,sizeof old0)==0);assert(mms_stage(&s,1,old1,sizeof old1)==0);
    assert(mms_commit(&s)==0);memcpy(baseline,flash,sizeof flash);
}
static void powercuts(void)
{
    mms_store s;reopen(&s);assert(transaction(&s)==0);unsigned total=mutations;
    assert(total>10);
    for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=total;++cut) {
        memcpy(flash,baseline,sizeof flash);reopen(&s);fail_at=cut;cut_mode=mode;
        assert(transaction(&s)!=0);reopen(&s);
        assert(mms_read_object(&s,0,readback,sizeof readback)==sizeof old0);
        int published=!memcmp(readback,new0,sizeof new0);
        if(!published)assert(!memcmp(readback,old0,sizeof old0));
        expect(&s,1,published?new1:old1,sizeof old1); /* never mixed generations */
        /* Retry with a DIFFERENT body. The actually selected catalogue remains
         * readable through every staged object write, including previous orphans. */
        assert(mms_stage(&s,0,last0,sizeof last0)==0);
        expect(&s,0,published?new0:old0,sizeof old0);
        assert(mms_stage(&s,1,new1,sizeof new1)==0);assert(mms_commit(&s)==0);
        reopen(&s);expect(&s,0,last0,sizeof last0);expect(&s,1,new1,sizeof new1);
        for(unsigned i=MMS_RESERVE-MMS_BASE;i<sizeof flash;++i)assert(flash[i]==0xA5);
    }
    printf("  %u transaction boundaries x 3 torn-write modes + retries\n",total);
}
static void orphan_and_failed_commit(void)
{
    mms_store s;memcpy(flash,baseline,sizeof flash);reopen(&s);
    unsigned committed_side=s.committed.ref[0].side;
    assert(mms_stage(&s,0,new0,sizeof new0)==0);
    assert(s.pending.ref[0].side!=committed_side);expect(&s,0,old0,sizeof old0);
    reopen(&s);expect(&s,0,old0,sizeof old0); /* newest physical header is an orphan */
    assert(mms_stage(&s,0,last0,sizeof last0)==0);assert(s.pending.ref[0].side!=committed_side);
    assert(mms_abort(&s)==0);assert(mms_commit(&s)==0);reopen(&s);expect(&s,0,old0,sizeof old0);
    assert(mms_stage(&s,0,new0,sizeof new0)==0);
    /* Header physically completed but driver returns failure: stage is locked
     * until reload, which must adopt the newly committed exact generation. */
    mutations=0;fail_at=3;cut_mode=2;
    assert(mms_commit(&s)==MMS_IO);assert(!s.loaded);
    assert(mms_stage(&s,0,last0,sizeof last0)==MMS_NOT_LOADED);
    assert(mms_commit(&s)==MMS_NOT_LOADED);
    reopen(&s);expect(&s,0,new0,sizeof new0);
    assert(mms_stage(&s,0,last0,sizeof last0)==0);expect(&s,0,new0,sizeof new0);
    assert(mms_commit(&s)==0);reopen(&s);expect(&s,0,last0,sizeof last0);
    /* Failed restaging drops the former pending ref, leaving committed sound. */
    assert(mms_stage(&s,0,new0,sizeof new0)==0);
    mutations=0;fail_at=2;cut_mode=1;assert(mms_stage(&s,0,old0,sizeof old0)==MMS_IO);
    operational();assert(mms_commit(&s)==0);reopen(&s);expect(&s,0,last0,sizeof last0);
}
static void rewrite_generation(unsigned obj,unsigned side,uint32_t generation)
{
    mms_header *h=(mms_header *)(void *)(flash+(mms_address(obj,side)-MMS_BASE));
    h->generation=generation;h->header_crc=mms_crc(h,28);
}
static void wrap_and_corruption(void)
{
    mms_store s;memcpy(flash,baseline,sizeof flash);reopen(&s);
    /* Force both committed object and catalogue counters to UINT32_MAX, keeping
     * wire CRCs valid. Next save must select generation zero across the wrap. */
    unsigned side=s.side;
    rewrite_generation(0,s.committed.ref[0].side,UINT32_MAX);
    uint8_t *sector=flash+(mms_address(MMS_CATALOG,side)-MMS_BASE);
    mms_catalog *c=(mms_catalog *)(void *)(sector+MMS_PAGE);c->ref[0].generation=UINT32_MAX;
    mms_header *h=(mms_header *)(void *)sector;h->generation=UINT32_MAX;h->crc=mms_crc(c,sizeof(*c));h->header_crc=mms_crc(h,28);
    /* Remove older catalogue so forced test counter is unambiguous. */
    memset(flash+(mms_address(MMS_CATALOG,side^1u)-MMS_BASE),255,MMS_SECTOR);
    reopen(&s);assert(s.generation==UINT32_MAX);
    assert(mms_stage(&s,0,new0,sizeof new0)==0);assert(mms_commit(&s)==0);
    reopen(&s);assert(s.generation==0 && s.committed.ref[0].generation==0);expect(&s,0,new0,sizeof new0);
    /* Bad newest object causes whole-catalogue fallback, never a mixture. */
    unsigned newest=s.committed.ref[0].side;
    flash[mms_address(0,newest)-MMS_BASE+MMS_PAGE]^=1;
    reopen(&s);assert(s.generation==UINT32_MAX);expect(&s,0,old0,sizeof old0);
    /* With older copy invalid too, failure is read-only and staging forbidden. */
    flash[mms_address(0,s.committed.ref[0].side)-MMS_BASE]^=1;
    operational();assert(mms_load(&s)==MMS_CORRUPT);assert(!mutations);
    assert(mms_stage(&s,0,new0,sizeof new0)==MMS_NOT_LOADED);
    /* Corrupt catalogue header and catalogue payload each fall back to the older
     * empty snapshot from baseline; unreferenced object bytes remain ignored. */
    for(unsigned offset=0;offset<=MMS_PAGE;offset+=MMS_PAGE) {
        memcpy(flash,baseline,sizeof flash);reopen(&s);
        flash[mms_address(MMS_CATALOG,s.side)-MMS_BASE+offset]^=1;
        reopen(&s);assert(mms_read_object(&s,0,readback,sizeof readback)==MMS_NOT_FOUND);
    }
}
static void reads_and_bounds(void)
{
    mms_store s;memcpy(flash,baseline,sizeof flash);reopen(&s);unsigned load_reads=reads;
    for(unsigned at=1;at<=load_reads;++at) {
        operational();assert(mms_init(&s,io)==0);read_fail_at=at;
        assert(mms_load(&s)==MMS_IO);assert(!mutations && !s.loaded);
        assert(mms_stage(&s,0,new0,sizeof new0)==MMS_NOT_LOADED);
    }
    reopen(&s);reads=0;read_fail_at=2;
    assert(mms_read_object(&s,0,readback,sizeof readback)==MMS_IO);assert(!s.loaded);
    reopen(&s);unsigned before=mutations;
    assert(mms_address(17,0)==UINT32_MAX && mms_address(0,2)==UINT32_MAX);
    assert(mms_stage(&s,16,new0,sizeof new0)==MMS_INVALID);
    assert(mms_stage(&s,0,new0,MMS_PAYLOAD_MAX+1)==MMS_INVALID);
    assert(mms_stage(&s,0,0,1)==MMS_INVALID);
    assert(mms_read_object(&s,0,readback,699)==MMS_INVALID);
    assert(mms_read_object(&s,16,readback,sizeof readback)==MMS_INVALID);
    assert(mutations==before);
    uint8_t max[MMS_PAYLOAD_MAX];for(unsigned i=0;i<sizeof max;++i)max[i]=(uint8_t)i;
    assert(mms_stage(&s,15,max,sizeof max)==0);assert(mms_stage(&s,14,0,0)==0);assert(mms_commit(&s)==0);
    reopen(&s);expect(&s,15,max,sizeof max);assert(mms_read_object(&s,14,0,0)==0);
    assert(mms_crc("123456789",9)==0xCBF43926u);
}
static void format_and_commit_reads(void)
{
    mms_store s;
    for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=5;++cut) {
        memcpy(flash,baseline,sizeof flash);reopen(&s);fail_at=cut;cut_mode=mode;
        assert(mms_format(&s,1)==MMS_IO);assert(!s.loaded);
        operational();assert(mms_init(&s,io)==0);
        int rc=mms_load(&s);assert(rc==MMS_OK || rc==MMS_CORRUPT);assert(!mutations);
        /* Format retries are still explicit and backup-authorized. */
        assert(mms_format(&s,1)==0);assert(mms_read_object(&s,0,readback,sizeof readback)==MMS_NOT_FOUND);
    }
    memcpy(flash,baseline,sizeof flash);reopen(&s);
    assert(mms_stage(&s,0,new0,sizeof new0)==0);reads=0;
    assert(mms_commit(&s)==0);unsigned commit_reads=reads;
    for(unsigned at=1;at<=commit_reads;++at) {
        memcpy(flash,baseline,sizeof flash);reopen(&s);
        assert(mms_stage(&s,0,new0,sizeof new0)==0);reads=0;read_fail_at=at;
        assert(mms_commit(&s)==MMS_IO);assert(!s.loaded);
        assert(mms_stage(&s,0,last0,sizeof last0)==MMS_NOT_LOADED);
        reopen(&s);
        /* Commit prevalidation failures leave old data. Failed readback may
         * follow publication; whichever catalogue wins remains protected. */
        assert(mms_read_object(&s,0,readback,sizeof readback)==sizeof old0);
        assert(!memcmp(readback,old0,sizeof old0) || !memcmp(readback,new0,sizeof new0));
        assert(mms_stage(&s,0,last0,sizeof last0)==0);assert(mms_commit(&s)==0);
        reopen(&s);expect(&s,0,last0,sizeof last0);
    }
    /* A CRC-valid object header for the wrong generation is not the referenced
     * object, even though standalone newest-object loading would accept it. */
    memcpy(flash,baseline,sizeof flash);reopen(&s);
    rewrite_generation(0,s.committed.ref[0].side,s.committed.ref[0].generation+9);
    reopen(&s);assert(mms_read_object(&s,0,readback,sizeof readback)==MMS_NOT_FOUND);
    assert(mms_address(0,0)==0xB4000 && mms_address(7,1)==0xC3000);
    assert(mms_address(8,0)==0xC4000 && mms_address(11,1)==0xCB000);
    assert(mms_address(12,0)==0xCC000 && mms_address(15,1)==0xD3000);
    assert(mms_address(16,0)==0xD4000 && mms_address(16,1)==0xD5000);
}
int main(void)
{
    setup();powercuts();orphan_and_failed_commit();wrap_and_corruption();reads_and_bounds();format_and_commit_reads();
    printf("mm_store: power cuts/retry, atomic catalogues, wrap, corruption, I/O and bounds passed; state=%zu bytes\n",sizeof(mms_store));
    return 0;
}
