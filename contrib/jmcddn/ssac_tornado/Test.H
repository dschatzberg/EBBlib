#ifndef __TEST_H__
#define __TEST_H__

inline unsigned long long 
now(void) 
{
  unsigned int a,d;

  __asm__ __volatile__("rdtsc" : "=a" (a), "=d" (d));
  return ((unsigned long long)a) | (((unsigned long long)d) << 32);
}


// simple dumb spin based barrier to make life easier
class Barrier {
  int size;
  volatile int enterCount;
  volatile int leaveCount;
  void reset() { enterCount = leaveCount = size; }
  void leave() {
     __sync_sub_and_fetch(&leaveCount, 1);
     if (leaveCount == 0 ) reset();  // last one to leave resets barrier
  }
public:
  void init(int val) { size=val; reset(); }
  Barrier() { init(0); }
  Barrier(int val)  { init(val); }
  int enter() {
    __sync_sub_and_fetch(&enterCount, 1);
    while (enterCount != 0); // spin
    leave();
  }
};


class Test {
  friend void *testPThreadFunc(void *);
private:
  EBBRC worker(int id);
  EBBRC doWork();
protected:
  Barrier bar;
  int numWorkers;
  int iterations;
  struct WArgs {
    int id;
    unsigned long long start;
    unsigned long long end;
    Test *theTest;
  } *wargs;

  virtual EBBRC init(void)=0;
  virtual EBBRC work(int id)=0;
  virtual EBBRC end(void);
public:
  Test(int n);
  EBBRC doTest(void) {
    
    if (init()<0) return -1;
    doWork();
    end();
  }
  virtual ~Test();
};


#endif
