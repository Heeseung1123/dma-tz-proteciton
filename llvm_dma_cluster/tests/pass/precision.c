/* DmaCluster pass 단위 시험 (opt). 실제 FreeRTOS 없이 API 모양만 선언한다. */
typedef void (*TaskFunction_t)(void *);
typedef void (*TimerCb)(void *);
int xTaskCreate(TaskFunction_t f, const char *n, unsigned short d, void *p, unsigned pr, void **h);
int SINK(void *src, void *dst, unsigned len);              /* config: args [1] */
void *pvPortMalloc(unsigned);

static unsigned char bufA[32], bufB[32], bufShared[16], bufP1[8], bufP2[8], bufTest[8];
static unsigned char bufAnn[24] __attribute__((annotate("dma:taskB")));   /* 구조체 포인터로만 전달 → annotate 로 보완 */
struct ctx { unsigned char *dst; };
static struct ctx s_ctxB = { 0 };
static unsigned char *gp;
static TimerCb s_cb;

static void cbX(void *p) { (void)p; }                      /* 진짜 간접 호출 대상 (void(ptr)) */
void timerTask(void *p) { for (;;) s_cb(p); }              /* 간접 호출: taskA/taskB 로 연결되면 안 됨 */
static void helper(void *d) { SINK(0, d, 4); }             /* Argument → 호출자로 역추적 */
void setp(int x) { gp = x ? bufP1 : bufP2; }               /* 전역 포인터 변수 */
void attack(void) { SINK(0, bufB, 4); }                    /* exclude_callers */

void taskA(void *p) { (void)p; helper(bufA); helper(bufShared); SINK(0, gp, 4); attack(); }
void taskB(void *p) { struct ctx *c = p; helper(bufB); helper(bufShared + 8); SINK(0, c->dst, 4); }
void taskH(void *p) { (void)p; SINK(0, pvPortMalloc(8), 8); }
/* SVC sink (config: "svc": 110, args = 레지스터 [1]). 원본 App_DmaStartSvc 형태와 리터럴 형태, 다른 번호(111)는 무시 */
static unsigned char bufV[8], bufV2[8], bufNo[8];
#define SVC_N(num, d) do { register unsigned r0 __asm("r0") = 0; register unsigned r1 __asm("r1") = (unsigned)(d); \
    register unsigned r2 __asm("r2") = 4; __asm volatile ("svc %[n]" : "+r"(r0) : "r"(r1), "r"(r2), [n] "i"(num) : "memory"); } while (0)
void taskV(void *p)
{
    (void)p;
    SVC_N(110, bufV);
    SVC_N(111, bufNo);
    register unsigned r1 __asm("r1") = (unsigned)bufV2;
    __asm volatile ("svc #110" :: "r"(r1) : "r0", "memory");
}
void taskS(void *p) { unsigned char l[4]; (void)p; SINK(0, l, 4); SINK(0, bufTest, 4); }

/* 구조체 일부만 DMA (head = 앞 필드 32 B, 태그 밖) + 공유 버퍼를 taskQ cluster 끝에 (shared_host) */
struct memT { unsigned char g[32]; unsigned char dst[32]; };
static struct memT s_memT;
static unsigned char bufQ[16], bufH[16];
void taskT(void *p) { unsigned i = (unsigned)p & 7u; SINK(0, s_memT.dst, 4); SINK(0, &s_memT.dst[i], 4); SINK(0, bufH, 4); }
void taskQ(void *p) { (void)p; SINK(0, bufQ, 4); SINK(0, bufH, 4); }

void init(void)
{
    s_cb = cbX;
    xTaskCreate(taskA, "A", 128, 0, 1, 0);
    s_ctxB.dst = bufAnn;
    xTaskCreate(taskB, "B", 128, &s_ctxB, 1, 0);
    xTaskCreate(taskH, "H", 128, 0, 1, 0);
    xTaskCreate(taskS, "S", 128, 0, 1, 0);
    xTaskCreate(timerTask, "T", 128, 0, 1, 0);
    xTaskCreate(taskV, "V", 128, 0, 1, 0);
    xTaskCreate(taskT, "T2", 128, 0, 1, 0);
    xTaskCreate(taskQ, "Q", 128, 0, 1, 0);
}
