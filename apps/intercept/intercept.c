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

#include <intercept/Interceptor.h>
#include <intercept/TestInterceptor.h>
#include <intercept/SwapInterceptor.h>
#include <arch/args.h>
#include <arch/cpu.h>
#include <l0/EventMgrPrim.h>
#include <l0/cobj/CObjEBBRootShared.h>
#include <l0/cobj/CObjEBBUtils.h>
#include <l1/App.h>
#include <lrt/io.h>
#include <lrt/exit.h>

COBJ_EBBType(Target) {
  EBBRC (*func) (TargetRef);
};

static EBBRC
Target0_func(TargetRef ref)
{
  return EBBRC_OK;
}

static CObjInterface(Target) Target0_ftable = {
  .func = Target0_func
};

static EBBRC
Target1_func(TargetRef ref)
{
  return EBBRC_OK;
}

static CObjInterface(Target) Target1_ftable = {
  .func = Target1_func
};

CObject(Intercept) {
  CObjInterface(Intercept) *ft;
};

CObjInterface(Intercept) {
  CObjImplements(App);
  EBBRC (*work) (InterceptRef self);
  EBBRC (*continueSwap) (InterceptRef self);
};

static TargetId target;
static EventNo evnum;

static TargetRef targRef0;
static CObjEBBRootSharedRef targRootRef0;

static EventNo continueEv;

static SwapInterceptorId id2;
static InterceptorControllerId controllerId2;

static void transfer_func() {
  //Free the original target:
  EBBPrimFree(sizeof(Target), targRef0);
  CObjEBBRootSharedDestroy(targRootRef0);

  //allocate a new one
  TargetRef targRef1;
  EBBRC rc = EBBPrimMalloc(sizeof(Target), &targRef1, EBB_MEM_GLOBAL);
  LRT_RCAssert(rc);
  targRef1->ft = &Target1_ftable;

  CObjEBBRootSharedRef targRootRef1;
  rc = CObjEBBRootSharedCreate(&targRootRef1, (EBBRepRef)targRef1);
  LRT_RCAssert(rc);

  rc = CObjEBBBind((EBBId)target, targRootRef1);
  LRT_RCAssert(rc);

  //no transfer of state

  //have ourselves call the target object
  rc = COBJ_EBBCALL(theEventMgrPrimId, allocEventNo, &continueEv);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(theEventMgrPrimId, bindEvent, continueEv,
                    (EBBId)theAppId,
                    COBJ_FUNCNUM_FROM_TYPE(CObjInterface(Intercept),
                                           continueSwap));
  LRT_RCAssert(rc);
  rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent, continueEv,
                    EVENT_LOC_SINGLE, MyEventLoc());
  LRT_RCAssert(rc);
}

EBBRC
Intercept_start(AppRef _self)
{
  InterceptRef self = (InterceptRef)_self;
  EBBRC rc = EBBPrimMalloc(sizeof(Target), &targRef0, EBB_MEM_GLOBAL);
  LRT_RCAssert(rc);
  targRef0->ft = &Target0_ftable;

  rc = EBBAllocPrimId((EBBId *)&target);
  LRT_RCAssert(rc);

  rc = CObjEBBRootSharedCreate(&targRootRef0, (EBBRepRef)targRef0);
  LRT_RCAssert(rc);

  rc = CObjEBBBind((EBBId)target, targRootRef0);
  LRT_RCAssert(rc);
  //Target initialized

  InterceptorId id0;
  rc = TestInterceptorCreate(&id0, "Interceptor 0");
  LRT_RCAssert(rc);

  InterceptorControllerId controllerId0;
  rc = EBBAllocPrimId((EBBId *)&controllerId0);
  LRT_RCAssert(rc);

  InterceptorControllerImp_Create(controllerId0);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(controllerId0, start, (EBBId)target, id0);
  LRT_RCAssert(rc);

  COBJ_EBBCALL(target, func);

  rc = COBJ_EBBCALL(controllerId0, stop);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(theEventMgrPrimId, allocEventNo, &evnum);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(theEventMgrPrimId, bindEvent, evnum,
                    (EBBId)theAppId, COBJ_FUNCNUM(self, work));
  LRT_RCAssert(rc);

  for(EventNo num = NextEventLoc(MyEventLoc());
      num != MyEventLoc();
      num = NextEventLoc(num)) {
    rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent, evnum,
                      EVENT_LOC_SINGLE, num);
    LRT_RCAssert(rc);
  }

  //Ok now all cores are invoking the target repeatedly
  rc = COBJ_EBBCALL(controllerId0, start, (EBBId)target, id0);
  LRT_RCAssert(rc);

  uint64_t time = read_timestamp();
  while ((read_timestamp() - time) < 1000000)
    ;

  rc = COBJ_EBBCALL(controllerId0, stop);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(controllerId0, start, (EBBId)target, id0);
  LRT_RCAssert(rc);

  time = read_timestamp();
  while ((read_timestamp() - time) < 1000000)
    ;

  InterceptorId id1;
  rc = TestInterceptorCreate(&id1, "Interceptor 1");
  LRT_RCAssert(rc);
  InterceptorControllerId controllerId1;
  rc = EBBAllocPrimId((EBBId *)&controllerId1);
  LRT_RCAssert(rc);

  InterceptorControllerImp_Create(controllerId1);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(controllerId1, start, (EBBId)target, id1);
  LRT_RCAssert(rc);

  time = read_timestamp();
  while ((read_timestamp() - time) < 1000000)
    ;

  rc = COBJ_EBBCALL(controllerId0, stop);
  LRT_RCAssert(rc);

  time = read_timestamp();
  while ((read_timestamp() - time) < 1000000)
    ;

  /* rc = COBJ_EBBCALL(controllerId0, destroy); */
  /* LRT_RCAssert(rc); */

  rc = COBJ_EBBCALL(controllerId1, stop);
  LRT_RCAssert(rc);

  /* rc = COBJ_EBBCALL(controllerId1, destroy); */
  /* LRT_RCAssert(rc); */

  time = read_timestamp();
  while ((read_timestamp() - time) < 1000000)
    ;

  rc = SwapInterceptorCreate(&id2, transfer_func);
  LRT_RCAssert(rc);

  rc = EBBAllocPrimId((EBBId *)&controllerId2);
  LRT_RCAssert(rc);

  InterceptorControllerImp_Create(controllerId2);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(controllerId2, start, (EBBId)target, (InterceptorId)id2);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(id2, begin);

  return EBBRC_OK;
}

static EBBRC
Intercept_work(InterceptRef self)
{
  COBJ_EBBCALL(target, func);
  EBBRC rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent, evnum,
                    EVENT_LOC_SINGLE, MyEventLoc());
  LRT_RCAssert(rc);
  return EBBRC_OK;
}

static EBBRC
Interceptor_continueSwap(InterceptRef self)
{
  //free Event
  EBBRC rc = COBJ_EBBCALL(theEventMgrPrimId, freeEventNo, continueEv);
  LRT_RCAssert(rc);

  rc = COBJ_EBBCALL(controllerId2, stop);
  LRT_RCAssert(rc);

  //destroy id2...
  uint64_t time = read_timestamp();
  while ((read_timestamp() - time) < 1000000)
    ;

  lrt_exit(0);
  return EBBRC_OK;
}
CObjInterface(Intercept) Intercept_ftable = {
  {.start = Intercept_start},
  .work = Intercept_work,
  .continueSwap = Interceptor_continueSwap
};

APP_START_ONE(Intercept);
