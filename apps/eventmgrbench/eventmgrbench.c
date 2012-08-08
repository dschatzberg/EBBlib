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

#include <inttypes.h>
#include <stdbool.h>

#include <arch/cpu.h>
#include <intercept/Interceptor.h>
#include <intercept/SwapInterceptor.h>
#include <l0/EventMgrPrimImp.h>
#include <l0/cobj/CObjEBBRootShared.h>
#include <l0/cobj/CObjEBBUtils.h>
#include <l1/App.h>
#include <lrt/exit.h>
#include <lrt/io.h>
#include <lrt/string.h>
#include <sync/barrier.h>

CObject(EventMgrBench) {
  CObjInterface(EventMgrBench) *ft;
  uint64_t tstamp;
  uint64_t count;
};

CObjInterface(EventMgrBench) {
  CObjImplements(App);
  EBBRC (*runNextTest) (EventMgrBenchRef self);
  EBBRC (*localEvent) (EventMgrBenchRef self);
};

/* /\******************************************** */
/*  **************** NULL OBJ START ************ */
/*  ********************************************\/ */

/* COBJ_EBBType(NullObj) { */
/*   EBBRC (*func) (NullObjRef self); */
/* }; */

/* CObject(NullObjImp) */
/* { */
/*   COBJ_EBBFuncTbl(NullObj); */
/* }; */

/* static EBBRC */
/* NullObjImp_func(NullObjRef self) */
/* { */
/*   return EBBRC_OK; */
/* } */

/* CObjInterface(NullObj) NullObjImp_ftable = { */
/*   .func = NullObjImp_func */
/* }; */

/* NullObjId nullid; */

/* EBBRC */
/* NullObjImpCreate() */
/* { */
/*   NullObjImpRef repRef; */
/*   CObjEBBRootSharedRef rootRef; */
/*   EBBRC rc; */

/*   rc = EBBPrimMalloc(sizeof(NullObjImp), &repRef, EBB_MEM_GLOBAL); */
/*   LRT_RCAssert(rc); */
/*   repRef->ft = &NullObjImp_ftable; */

/*   rc = CObjEBBRootSharedCreate(&rootRef, (EBBRepRef)repRef); */
/*   LRT_RCAssert(rc); */

/*   rc = EBBAllocPrimId((EBBId *)&nullid); */
/*   LRT_RCAssert(rc); */

/*   rc = CObjEBBBind((EBBId)nullid, rootRef); */
/*   LRT_RCAssert(rc); */

/*   return EBBRC_OK; */
/* } */

/* /\******************************************** */
/*  **************** NULL OBJ END ************** */
/*  ********************************************\/ */
enum test_type {
  LOCAL
};

static enum test_type test;
static EventNo runTestEvent;
static EventNo endSwapEvent;
static EventNo localEvent;
static struct barrier_s barrier1;
static struct barrier_s barrier2;
static int num_cores = 0;
static int order;
static SwapInterceptorId swapId;
static InterceptorControllerId controllerId;

#include <l0/lrt/event.h>
static void setupNextTest(void);
extern EBBRC EventMgrPrimImpMwaitCreate(EBBMissFunc *, EBBArg *);
extern void EventMgrPrimImpTransfer_get(uint8_t *,
                                        struct lrt_event_descriptor *);
extern void EventMgrPrimImpMwaitTransfer_set(uint8_t *,
                                             struct lrt_event_descriptor *);

static void
transfer_func()
{
  //FIXME: free the original eventmgr
  //FIXME: destroy its root

  //Create the next eventmanager
  EBBMissFunc mf;
  EBBArg arg;
  EBBRC rc = EventMgrPrimImpMwaitCreate(&mf, &arg);
  LRT_RCAssert(rc);

  //bind it in
  rc = EBBBindPrimId((EBBId)theEventMgrPrimId, mf, arg);

  //Ok, now transfer state
  //FIXME: THIS IS A MASSIVE KLUDGE
  uint8_t alloc_table[LRT_EVENT_NUM_EVENTS / 8];
  bzero(alloc_table, sizeof(uint8_t) * LRT_EVENT_NUM_EVENTS / 8);
  struct lrt_event_descriptor lrt_event_table[LRT_EVENT_NUM_EVENTS];
  bzero(lrt_event_table,
        sizeof(struct lrt_event_descriptor) * LRT_EVENT_NUM_EVENTS);
  EventMgrPrimImpTransfer_get(alloc_table, lrt_event_table);
  EventMgrPrimImpMwaitTransfer_set(alloc_table, lrt_event_table);
  //transfer is complete

  //after the swap is done, we continue there
  rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent,
                    endSwapEvent, EVENT_LOC_SINGLE, MyEventLoc());
  LRT_RCAssert(rc);

}

static void
setupNextTest(void)
{
  enum stage_type {
    NOT_SETUP,
    LOCAL_NOT_SETUP,
    LOCAL_SETUP,
    COMPLETE,
    SWAP_COMPLETE
  };
  static enum stage_type stage = NOT_SETUP;
  static int num_runs = 0;
  EBBRC rc;
  int i;
  switch(stage) {
  case NOT_SETUP:
      lrt_printf("--------------------------\n");
      lrt_printf("-------HaltEventMgr-------\n");
      lrt_printf("--------------------------\n");
    //Setup the run test event
    rc = COBJ_EBBCALL(theEventMgrPrimId, allocEventNo, &runTestEvent);
    LRT_RCAssert(rc);

    rc = COBJ_EBBCALL(theEventMgrPrimId, bindEvent, runTestEvent,
                      (EBBId)theAppId,
                      COBJ_FUNCNUM_FROM_TYPE(CObjInterface(EventMgrBench),
                                             runNextTest));
    LRT_RCAssert(rc);

    //setup swapping interceptor
    rc = SwapInterceptorCreate(&swapId, transfer_func);
    LRT_RCAssert(rc);

    rc = EBBAllocPrimId((EBBId *)&controllerId);
    LRT_RCAssert(rc);

    InterceptorControllerImp_Create(controllerId);
    LRT_RCAssert(rc);

    rc = COBJ_EBBCALL(theEventMgrPrimId, allocEventNo, &endSwapEvent);
    LRT_RCAssert(rc);

    rc = COBJ_EBBCALL(theEventMgrPrimId, bindEvent, endSwapEvent,
                      (EBBId)theAppId,
                      COBJ_FUNCNUM_FROM_TYPE(CObjInterface(App),
                                             start));
    LRT_RCAssert(rc);
    //fall through
  case LOCAL_NOT_SETUP:
    //We need to setup the local test
    test = LOCAL;
    rc = COBJ_EBBCALL(theEventMgrPrimId, allocEventNo, &localEvent);
    LRT_RCAssert(rc);

    rc = COBJ_EBBCALL(theEventMgrPrimId, bindEvent, localEvent,
                      (EBBId)theAppId,
                      COBJ_FUNCNUM_FROM_TYPE(CObjInterface(EventMgrBench),
                                             localEvent));
    LRT_RCAssert(rc);
    stage = LOCAL_SETUP;
    //fall through
  case LOCAL_SETUP:
    num_cores++;
    if (num_cores <= NumEventLoc()) {
      lrt_printf("Running Local Test with %d cores\n", num_cores);
      init_barrier(&barrier1, num_cores);
      init_barrier(&barrier2, num_cores);
      order = 0;
      for (i = 0;
           i < num_cores;
           i++) {
        rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent,
                                runTestEvent, EVENT_LOC_SINGLE, i);
        LRT_RCAssert(rc);
      }
      break;
    } else {
      num_cores = 0; //so the next run will work
      // fall through
    }
  case COMPLETE:
    if (num_runs < 1) {
      //swap event managers
      rc = COBJ_EBBCALL(controllerId, start, (EBBId)theEventMgrPrimId,
                        (InterceptorId)swapId);
      LRT_RCAssert(rc);

      rc = COBJ_EBBCALL(swapId, begin);
      stage = SWAP_COMPLETE;
      num_runs++;
      break;
    } else {
      lrt_printf("Tests Complete!\n");
      lrt_exit(0);
    }
  case SWAP_COMPLETE:
    rc = COBJ_EBBCALL(controllerId, stop);
    LRT_RCAssert(rc);
    stage = LOCAL_SETUP;
    lrt_printf("--------------------------\n");
    lrt_printf("-------MwaitEventMgr------\n");
    lrt_printf("--------------------------\n");
    //cause setupNextTest to run again
    rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent,
                      endSwapEvent, EVENT_LOC_SINGLE, MyEventLoc());
    break;
  default:
    lrt_printf("Unknown stage!\n");
    LRT_Assert(0);
  }
}

static void outputResults(EventMgrBenchRef self)
{
  switch(test) {
  case LOCAL:
    lrt_printf("Core %d: %ld\n", MyEventLoc(), self->tstamp);
  }
}

static EBBRC
EventMgrBench_start(AppRef self)
{
  setupNextTest();
  return EBBRC_OK;
}

static EBBRC
EventMgrBench_runNextTest(EventMgrBenchRef self)
{
  int sense;
  EBBRC rc;
  switch(test) {
  case LOCAL:
    sense = 0;
    self->count = 0;
    barrier(&barrier1, &sense); //wait for all cores to get here
    self->tstamp = rdtsc();
    rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent,
                            localEvent, EVENT_LOC_SINGLE, MyEventLoc());
    LRT_RCAssert(rc);
  }
  return EBBRC_OK;
}

static EBBRC
EventMgrBench_localEvent(EventMgrBenchRef self)
{
  if (self->count < 100) {
    EBBRC rc = COBJ_EBBCALL(theEventMgrPrimId, triggerEvent,
                            localEvent, EVENT_LOC_SINGLE, MyEventLoc());
    LRT_RCAssert(rc);
    self->count++;
  } else {
    uint64_t time = rdtsc();
    self->tstamp = time - self->tstamp;
    int sense1 = 0;
    int sense2 = 0;
    barrier(&barrier1, &sense1);
    while(ACCESS_ONCE(order) != MyEventLoc())
      ;
    outputResults(self);
    order++;
    barrier(&barrier2, &sense2);
    if (MyEventLoc() == 0) {
      setupNextTest();
    }
  }
  return EBBRC_OK;
}

CObjInterface(EventMgrBench) EventMgrBench_ftable = {
  .App_if = {
    .start = EventMgrBench_start
  },
  .runNextTest = EventMgrBench_runNextTest,
  .localEvent = EventMgrBench_localEvent
};

/*
 * this application differs from others in that it
 * has its own app_start, since it wants to start up a
 * different EventMgr from the default
 */

EBBRC
app_start()
{
  EBBRC rc = EBBRC_OK;
  if (MyEventLoc() == 0) {
    EBBRC rc = EBBRC_OK;

    rc = EBBMemMgrPrimInit();
    LRT_RCAssert(rc);

    rc = EBBMgrPrimInit();
    LRT_RCAssert(rc);

    rc = EventMgrPrimImpInit();
    LRT_RCAssert(rc);
    create_app_obj_default();
    return COBJ_EBBCALL(theAppId, start);
  }
  return rc;
}
APP_BASE(EventMgrBench)
