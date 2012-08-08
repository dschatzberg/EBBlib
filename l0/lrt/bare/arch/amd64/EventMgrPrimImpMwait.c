/*
 * Copyright (C) 2011 by Project SESA, Boston University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <config.h>
#include <stdint.h>
#include <inttypes.h>

#include <l0/lrt/types.h>
#include <l0/cobj/cobj.h>
#include <lrt/io.h>
#include <l0/lrt/trans.h>
#include <lrt/assert.h>
#include <l0/cobj/CObjEBB.h>
#include <l0/EBBMgrPrim.h>
#include <l0/cobj/CObjEBBUtils.h>
#include <l0/cobj/CObjEBBRoot.h>
#include <l0/cobj/CObjEBBRootMulti.h>
#include <l0/cobj/CObjEBBRootMultiImp.h>
#include <l0/EventMgrPrim.h>
#include <l0/EventMgrPrimImp.h>
#include <l0/MemMgr.h>
#include <l0/MemMgrPrim.h>
#include <l0/lrt/event.h>
#include <lrt/string.h>
#include <sync/misc.h>

STATIC_ASSERT(LRT_EVENT_NUM_EVENTS % 8 == 0,
              "num allocatable events isn't divisible by 8");
static uint8_t alloc_table[LRT_EVENT_NUM_EVENTS / 8];
static struct lrt_event_descriptor lrt_event_table[LRT_EVENT_NUM_EVENTS];

struct event_bvs {
  uint64_t vec[4];
  char packing[CACHE_LINE_ALIGNMENT - (sizeof(uint64_t) * 4)];
} _ALIGN_CACHE_;

static inline void
event_set_bit_bv(struct event_bvs *bv, int bit)
{
  int word = bit / 64;
  uint64_t mask = (uint64_t)1 << (bit % 64);
  __sync_or_and_fetch(&bv->vec[word], mask);
}

static inline int
event_get_unset_bit_bv(struct event_bvs *bv)
{
  for (int word = 3; word >= 0; word--) {
    uint64_t val = ACCESS_ONCE(bv->vec[word]);
    if (val) {
      int bit = 63 - __builtin_clzl(val);
      __sync_and_and_fetch(&bv->vec[word], ~(1 << bit));
      return word * 64 + bit;
    }
  }
  return -1;
}

CObject(EventMgrPrimImpMwait){
  CObjInterface(EventMgrPrim) *ft;

  EventMgrPrimImpMwaitRef *reps;
  CObjEBBRootMultiRef theRoot;
  EventLoc eventLoc;
  struct event_bvs bv;
};

static EBBRC
EventMgrPrimImpMwait_allocEventNo(EventMgrPrimRef _self, EventNo *eventNoPtr)
{
  int i;
  //we start from the beginning and just find the first
  // unallocated event
  for (i = 0;
       i < (LRT_EVENT_NUM_EVENTS - LRT_EVENT_NUM_HIGH_PRIORITY_EVENTS);
       i++) {
    uint8_t res = __sync_fetch_and_or(&alloc_table[i / 8], 1 << (i % 8));
    if (!(res & (1 << (i % 8)))) {
      break;
    }
  }
  if (i >= (LRT_EVENT_NUM_EVENTS - LRT_EVENT_NUM_HIGH_PRIORITY_EVENTS)) {
    return EBBRC_OUTOFRESOURCES;
  }
  *eventNoPtr = i;
  return EBBRC_OK;
}

static EBBRC
EventMgrPrimImpMwait_allocHighPriorityEventNo(EventMgrPrimRef _self,
                                         EventNo *eventNoPtr)
{
  int i;
  //we start from the beginning and just find the first
  // unallocated event
  for (i = (LRT_EVENT_NUM_EVENTS - LRT_EVENT_NUM_HIGH_PRIORITY_EVENTS);
       i < LRT_EVENT_NUM_EVENTS; i++) {
    uint8_t res = __sync_fetch_and_or(&alloc_table[i / 8], 1 << (i % 8));
    if (!(res & (1 << (i % 8)))) {
      break;
    }
  }
  if (i >= LRT_EVENT_NUM_EVENTS) {
    return EBBRC_OUTOFRESOURCES;
  }
  *eventNoPtr = i;
  return EBBRC_OK;
}

static EBBRC
EventMgrPrimImpMwait_freeEventNo(EventMgrPrimRef _self, EventNo eventNo)
{
  __sync_fetch_and_and(&alloc_table[eventNo / 8], ~(1 << (eventNo % 8)));
  return EBBRC_OK;
}

static EBBRC
EventMgrPrimImpMwait_bindEvent(EventMgrPrimRef _self, EventNo eventNo,
          EBBId handler, EBBFuncNum fn)
{
  lrt_event_table[eventNo].id = handler;
  lrt_event_table[eventNo].fnum = fn;
  return EBBRC_OK;
}

static EBBRC
EventMgrPrimImpMwait_routeIRQ(EventMgrPrimRef _self, IRQ *isrc, EventNo eventNo,
                      enum EventLocDesc desc, EventLoc el)
{
  LRT_Assert(0);
  return EBBRC_OK;
}

static inline EventMgrPrimImpMwaitRef
findTarget(EventMgrPrimImpMwaitRef self, EventLoc loc)
{
  RepListNode *node;
  EBBRep * rep = NULL;

  while (1) {
    rep = (EBBRep *)self->reps[loc];
    if (rep == NULL) {
      for (node = self->theRoot->ft->nextRep(self->theRoot, 0, &rep);
           node != NULL;
           node = self->theRoot->ft->nextRep(self->theRoot, node, &rep)) {
        LRT_Assert(rep != NULL);
        if (((EventMgrPrimImpMwaitRef)rep)->eventLoc == loc) break;
      }
      self->reps[loc] = (EventMgrPrimImpMwaitRef)rep;
    }
    // FIXME: handle case that rep doesn't yet exist
    if (rep != NULL) {
      return (EventMgrPrimImpMwaitRef)rep;;
    }
    lrt_printf("x");
  }
  LRT_Assert(0);		/* can't get here */
}

static EBBRC
EventMgrPrimImpMwait_triggerEvent(EventMgrPrimRef _self, EventNo eventNo,
                          enum EventLocDesc desc, EventLoc el)
{
  EventMgrPrimImpMwaitRef self = (EventMgrPrimImpMwaitRef)_self;
  LRT_Assert(el < lrt_num_event_loc());
  LRT_Assert(desc == EVENT_LOC_SINGLE);

  EventMgrPrimImpMwaitRef rep = self;

  if (el != lrt_my_event_loc()) {
    rep = findTarget(self, el);
  }

  event_set_bit_bv(&rep->bv, eventNo);

  return EBBRC_OK;
}

static EBBRC
EventMgrPrimImpMwait_dispatchEvent(EventMgrPrimRef _self, EventNo eventNo)
{
  struct lrt_event_descriptor *desc = &lrt_event_table[eventNo];
  lrt_trans_id id = desc->id;
  lrt_trans_func_num fnum = desc->fnum;

  //this infrastructure should be pulled out of this file
  lrt_trans_rep_ref ref = lrt_trans_id_dref(id);
  ref->ft[fnum](ref);
  return EBBRC_OK;
}

static EBBRC
EventMgrPrimImpMwait_enableInterrupts(EventMgrPrimRef _self)
{
  EventMgrPrimImpMwaitRef self = (EventMgrPrimImpMwaitRef)_self;
  int bit = event_get_unset_bit_bv(&self->bv);
  if (bit != -1) {
    EventMgrPrimImpMwait_dispatchEvent(_self, bit);
    return EBBRC_OK;
  }
  asm volatile ("monitor"
                :
                : "a" (&self->bv), "c" (0));
  bit = event_get_unset_bit_bv(&self->bv);
  if (bit != -1) {
    EventMgrPrimImpMwait_dispatchEvent(_self, bit);
    return EBBRC_OK;
  }
  asm volatile ("mwait"
                :
                : "a" (0),"c" (1));
  bit = event_get_unset_bit_bv(&self->bv);
  if (bit != -1) {
    EventMgrPrimImpMwait_dispatchEvent(_self, bit);
    return EBBRC_OK;
  } else {
    asm volatile ("sti\n\t"
                  "nop\n\t"
                  "cli"
                  ::
                  : "rax", "rcx", "rdx", "rsi",
                    "rdi", "r8", "r9", "r10", "r11");
  }
  return EBBRC_OK;
}

CObjInterface(EventMgrPrim) EventMgrPrimImpMwait_ftable = {
  .allocEventNo = EventMgrPrimImpMwait_allocEventNo,
  .freeEventNo = EventMgrPrimImpMwait_freeEventNo,
  .allocHighPriorityEventNo = EventMgrPrimImpMwait_allocHighPriorityEventNo,
  .bindEvent = EventMgrPrimImpMwait_bindEvent,
  .routeIRQ = EventMgrPrimImpMwait_routeIRQ,
  .triggerEvent = EventMgrPrimImpMwait_triggerEvent,
  .enableInterrupts = EventMgrPrimImpMwait_enableInterrupts,
  .dispatchEvent = EventMgrPrimImpMwait_dispatchEvent
};

static void
EventMgrPrimSetFT(EventMgrPrimImpMwaitRef o)
{
  o->ft = &EventMgrPrimImpMwait_ftable;
}

static EBBRep *
EventMgrPrimImpMwait_createRep(CObjEBBRootMultiRef root)
{
  EventMgrPrimImpMwaitRef repRef;
  EBBRC rc;
  rc = EBBPrimMalloc(sizeof(EventMgrPrimImpMwait), &repRef, EBB_MEM_DEFAULT);
  LRT_RCAssert(rc);

  EventMgrPrimSetFT(repRef);
  repRef->theRoot = root;
  repRef->eventLoc = lrt_my_event_loc();

  bzero(&repRef->bv, sizeof(repRef->bv));

  rc = EBBPrimMalloc(sizeof(repRef->reps)*lrt_num_event_loc(), &repRef->reps,
                     EBB_MEM_DEFAULT);
  bzero(repRef->reps, sizeof(repRef->reps) * lrt_num_event_loc());

  return (EBBRep *)repRef;
}

EBBRC
EventMgrPrimImpMwaitCreate(EBBMissFunc *mf, EBBArg *arg)
{
  static CObjEBBRootMultiImpRef rootRef;
  EBBRC rc = CObjEBBRootMultiImpCreate(&rootRef, EventMgrPrimImpMwait_createRep);
  LRT_RCAssert(rc);
  *mf = CObjEBBMissFunc;
  *arg = (EBBArg)rootRef;
  return EBBRC_OK;
}

EBBRC
EventMgrPrimImpMwaitInit(void)
{
  EBBRC rc;

  if (__sync_bool_compare_and_swap(&theEventMgrPrimId, (EventMgrPrimId)0,
                                   (EventMgrPrimId)-1)) {
    LRT_Assert(has_monitor());
    LRT_Assert(monitor_ibe());
    EBBMissFunc mf;
    EBBArg arg;
    rc = EventMgrPrimImpMwaitCreate(&mf, &arg);
    LRT_RCAssert(rc);
    EBBId id;
    rc = EBBAllocPrimId(&id);
    LRT_RCAssert(rc);
    rc = EBBBindPrimId(id, mf, arg);
    LRT_RCAssert(rc);
    theEventMgrPrimId = (EventMgrPrimId)id;
  } else {
    while ((*(volatile uintptr_t *)&theEventMgrPrimId)==-1);
  }
  return EBBRC_OK;
};

void
EventMgrPrimImpMwaitTransfer_set(uint8_t *alloc_table_in,
                                 struct lrt_event_descriptor *lrt_event_table_in)
{
  for(int i = 0; i < LRT_EVENT_NUM_EVENTS; i++) {
    if (i % 8 == 0) {
      alloc_table[i/8] = alloc_table_in[i/8];
    }
    lrt_event_table[i] = lrt_event_table_in[i];
  }
}
