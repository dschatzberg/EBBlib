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
#ifndef __LRT_EVENT_H__
#error "Should only be included through l0/lrt/event.h"
#endif

struct lrt_event_descriptor {
  lrt_trans_id id;
  lrt_trans_func_num fnum;
};

int lrt_event_get_event_nonblock(void);
void lrt_event_halt(void);

#define LRT_EVENT_NUM_HIGH_PRIORITY_EVENTS (16)
#define LRT_EVENT_NUM_EVENTS (256)
STATIC_ASSERT((1 << (sizeof(lrt_event_num) * 8)) >= LRT_EVENT_NUM_EVENTS,
              "lrt_event_num cannot hold the range of events!");
