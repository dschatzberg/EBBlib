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
#include <l0/types.h>
#include <l0/cobj/cobj.h>
#include <l0/cobj/CObjEBB.h>
#include <l0/cobj/CObjEBBRoot.h>
#include <l0/cobj/CObjEBBRootShared.h>

//What you want to do here is install theRep into the EBBLTrans
//then set obj to point to theRep (obj is really of type Object **)
//Then simply return EBBRC_OK and the default func will
//handle the rest. Return any failure code to have the call fail
uintptr_t
CObjEBBRootShared_handleMiss(void *_self, void *obj, EBBLTrans *lt, 
			     FuncNum fnum)
{
  CObjEBBRootSharedRef self = _self;
  EBBCacheObj(lt, self->theRep);
  *(void **)obj = self->theRep;
  return EBBRC_OK;
}

//Jonathan's code
/* uintptr_t */
/* CObjEBBRootShared_handleMiss(void *_self, void *obj, EBBLTrans *lt,  */
/* 			     FuncNum fnum) */
/* { */
/*   CObjEBBRootSharedRef self = _self; */
/*   return (uintptr_t)(self->theRep); */
/* } */
 
void
CObjEBBRootShared_init(void *_self, void *rep)
{
  CObjEBBRootSharedRef self = _self;
  self->theRep = rep;
}

CObjInterface(CObjEBBRootShared) CObjEBBRootShared_ftable = {
  { CObjEBBRootShared_handleMiss },
  CObjEBBRootShared_init
};
