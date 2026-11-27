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
void taskS(void *p) { unsigned char l[4]; (void)p; SINK(0, l, 4); SINK(0, bufTest, 4); }

void init(void)
{
    s_cb = cbX;
    xTaskCreate(taskA, "A", 128, 0, 1, 0);
    s_ctxB.dst = bufAnn;
    xTaskCreate(taskB, "B", 128, &s_ctxB, 1, 0);
    xTaskCreate(taskH, "H", 128, 0, 1, 0);
    xTaskCreate(taskS, "S", 128, 0, 1, 0);
    xTaskCreate(timerTask, "T", 128, 0, 1, 0);
}
