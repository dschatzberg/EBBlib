/*
 * Copyright (C) 2012 by Project SESA, Boston University
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

#include <arch/args.h>
#include <intercept/Interceptor.h>
#include <intercept/SwapInterceptor.h>
#include <l0/EBBMgrPrim.h>
#include <l0/EventMgrPrim.h>
#include <l0/MemMgrPrim.h>
#include <l0/cobj/CObjEBBRootMultiImp.h>
#include <l0/cobj/CObjEBBUtils.h>

enum stage_type {
  FORWARD,
  BLOCKED
};

struct swap_shared {
  TransferFunc tf;
  bool release;
  EBBId myId;
  EventNo ev;
  EventLoc start;
};

CObject(SwapInterceptorImp) {
  CObjInterface(SwapInterceptorImp) *ft;
  struct swap_shared *shared;
  enum stage_type stage;
};

CObjInterface(SwapInterceptorImp) {
  CObjImplements(SwapInterceptor);
  EBBRC (*rrEvent)(SwapInterceptorImpRef self);
};

static EBBRC
SwapInterceptorImp_PreCall(InterceptorRef _self, struct args *args,
                        EBBFuncNum fnum, union func_ret *fr)
{
  SwapInterceptorImpRef self = (SwapInterceptorImpRef)_self;
  switch (self->stage) {
  case FORWARD:
    break;
  case BLOCKED:
    while (ACCESS_ONCE(self->shared->release) == false) {
      cpu_relax();
    }
    //Ok transfer has occurred
    *(void **)args = NULL; //set the this pointer to NULL so it gets
                           //reacquired
    self->stage = FORWARD;
    break;
  }
  return EBBRC_OK;
}

static EBBRC
SwapInterceptorImp_PostCall(InterceptorRef _self, EBBRC rc)
{
  return rc;
}

static EBBRC
SwapInterceptorImp_begin(SwapInterceptorRef _self)
{
  SwapInterceptorImpRef self = (SwapInterceptorImpRef)_self;
  //Start an event that round robins across all event locations
  // At each location, we have established quiescence so we can block
  // all future calls. Once the final event location goes, all calls
  // are blocked, and so we can do a transfer
  EBBRC rc = COBJ_EBBCALL(theEventMgrPrimId, allocHighPriorityEventNo,
                          &self->shared->ev);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(theEventMgrPrimId, bindEvent, self->shared->ev,
                    (EBBId)self->shared->myId,
                    COBJ_FUNCNUM_FROM_TYPE(CObjInterface(SwapInterceptorImp),
                                           rrEvent));
  LRT_RCAssert(rc);

  self->shared->start = MyEventLoc();

  rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent,
                    self->shared->ev, EVENT_LOC_SINGLE,
                    NextEventLoc(MyEventLoc()));
  LRT_RCAssert(rc);

  return EBBRC_OK;
}

static EBBRC
SwapInterceptorImp_rrEvent(SwapInterceptorImpRef self)
{
  if (self->shared->start != MyEventLoc()) {
    //We are at a quiescent point on this location, so we block
    EBBRC rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent,
                      self->shared->ev, EVENT_LOC_SINGLE,
                      NextEventLoc(MyEventLoc()));
    LRT_RCAssert(rc);
    self->stage = BLOCKED;
  } else {
    self->shared->tf();
    self->shared->release = true;
    self->stage = FORWARD;
  }
  return EBBRC_OK;
}

static CObjInterface(SwapInterceptorImp) SwapInterceptorImp_ftable = {
  .SwapInterceptor_if = {
    .Interceptor_if = {
      .PreCall = SwapInterceptorImp_PreCall,
      .PostCall = SwapInterceptorImp_PostCall
    },
    .begin = SwapInterceptorImp_begin
  },
  .rrEvent = SwapInterceptorImp_rrEvent
};

static EBBRepRef
SwapInterceptorImp_createRep(CObjEBBRootMultiRef root)
{
  SwapInterceptorImpRef intRef;
  EBBRC rc = EBBPrimMalloc(sizeof(SwapInterceptorImp), &intRef, EBB_MEM_GLOBAL);
  LRT_RCAssert(rc);

  intRef->ft = &SwapInterceptorImp_ftable;
  intRef->shared = (struct swap_shared *)root->ft->getKey(root);
  intRef->stage = FORWARD;
  return (EBBRepRef)intRef;
}

EBBRC
SwapInterceptorCreate(SwapInterceptorId *id, TransferFunc tf)
{
  struct swap_shared *shared;
  EBBRC rc = EBBPrimMalloc(sizeof(struct swap_shared), &shared, EBB_MEM_GLOBAL);
  LRT_RCAssert(rc);
  shared->tf = tf;
  shared->release = false;

  CObjEBBRootMultiImpRef rootRef;
  rc = CObjEBBRootMultiImpCreate(&rootRef, SwapInterceptorImp_createRep);
  LRT_RCAssert(rc);

  rc = EBBAllocPrimId((EBBId *)id);
  LRT_RCAssert(rc);

  shared->myId = (EBBId)*id;

  rootRef->ft->setKey((CObjEBBRootMultiRef)rootRef, (uintptr_t)shared);

  rc = CObjEBBBind(*(EBBId *)id, rootRef);
  LRT_RCAssert(rc);
  return EBBRC_OK;
}
