/* tlb_probe.c —— 观察跨页访问随工作集增大的耗时趋势
 *
 * 编译：gcc -O2 -o tlb_probe tlb_probe.c
 * 运行：./tlb_probe
 *
 * 实验一：目标数据区访问次数相近，访问的页数不同。
 *         1 个页内重复 100 万次，与 16384 个页上按完整轮次访问，比较每次耗时。
 * 实验二：工作集从 64 页加到 16384 页，看每次访问的耗时在哪里明显上升。
 *
 * 两次都读取 ru_minflt，记录新增轻微缺页，不统计 TLB 未命中或重大缺页。
 * 耗时还受数据 cache、页表缓存、循环与调度等影响，不能直接测出 TLB 容量。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>

#define MAX_PAGES   16384           /* 16384 × 4 KiB = 64 MiB */
#define BIG_ACCESS  1000000         /* 实验一的访存次数 */
#define SWEEP_ACCESS 200000         /* 实验二每一档的访存次数 */

static double now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static long minor_faults(void)
{
    struct rusage ru;

    getrusage(RUSAGE_SELF, &ru);
    return (long)ru.ru_minflt;
}

static uint64_t rng_state = 88172645463325252ULL;

static uint64_t next_rand(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* 生成 0..n-1 的随机排列，让相邻两次访问落在不同的页上 */
static void make_perm(size_t *perm, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        perm[i] = i;
    }
    for (i = n; i > 1; i--) {
        size_t j = (size_t)(next_rand() % i);
        size_t t = perm[i - 1];
        perm[i - 1] = perm[j];
        perm[j] = t;
    }
}

/* 在 pages 个页上按随机顺序轮转，按完整轮次取整；返回每次目标读取的纳秒数 */
static double probe(volatile unsigned char *base, size_t ps,
                    const size_t *perm, size_t pages, size_t total,
                    size_t *done, long *fault_delta)
{
    size_t rounds = total / pages;
    size_t i, r;
    volatile unsigned long sink = 0;
    long f0;
    double t0;
    double t1;

    if (rounds == 0) {
        rounds = 1;
    }
    /* 先预热本档的访问路径，减少计时中首次使用带来的轻微缺页。 */
    for (i = 0; i < pages; i++) {
        sink += base[perm[i] * ps];
    }
    f0 = minor_faults();
    t0 = now_ns();
    for (r = 0; r < rounds; r++) {
        for (i = 0; i < pages; i++) {
            sink += base[perm[i] * ps];
        }
    }
    t1 = now_ns();
    *fault_delta = minor_faults() - f0;
    *done = rounds * pages;
    if (sink == 0x5a5a5a5aUL) {         /* 防止循环被优化掉，不会真的成立 */
        printf(" ");
    }
    return (t1 - t0) / (double)(rounds * pages);
}

int main(void)
{
    size_t ps = (size_t)sysconf(_SC_PAGESIZE);
    size_t total_bytes = (size_t)MAX_PAGES * ps;
    volatile unsigned char *base;
    size_t *perm;
    size_t done;
    long faults;
    double one_page, many_pages;

    base = malloc(total_bytes);
    perm = malloc(MAX_PAGES * sizeof(size_t));
    if (base == NULL || perm == NULL) {
        printf("申请 %zu 字节内存失败，先关闭一些程序再试\n", total_bytes);
        free((void *)base);
        free(perm);
        return 1;
    }
    /* 先把数据页都碰一遍，减少按需分配带来的缺页；不保证计时期间计数为 0。 */
    memset((void *)base, 1, total_bytes);
    /* 计时函数的第一次使用也可能触发轻微缺页，先调用一次。 */
    (void)now_ns();
    (void)minor_faults();

    printf("pagesize = %zu 字节\n", ps);
    printf("最大工作集 = %zu 页 = %zu KiB\n\n", (size_t)MAX_PAGES, total_bytes / 1024);

    printf("== 实验一：访存次数相近，访问的页数不同 ==\n");
    make_perm(perm, 1);
    one_page = probe(base, ps, perm, 1, BIG_ACCESS, &done, &faults);
    printf("在 1 个页内重复        %8zu 次   每次 %8.2f ns   新增轻微缺页 %ld\n",
           done, one_page, faults);
    make_perm(perm, MAX_PAGES);
    many_pages = probe(base, ps, perm, MAX_PAGES, BIG_ACCESS, &done, &faults);
    printf("在 %zu 个页上轮转  %8zu 次   每次 %8.2f ns   新增轻微缺页 %ld\n",
           (size_t)MAX_PAGES, done, many_pages, faults);
    printf("倍数：%.1f\n\n", many_pages / one_page);

    printf("== 实验二：工作集逐步增大 ==\n");
    printf("%-12s %-14s %-12s %-14s %-12s %s\n",
           "工作集(页)", "工作集(KiB)", "访问次数", "每次耗时(ns)", "相对64页", "新增轻微缺页");
    {
        size_t sizes[5] = { 64, 256, 1024, 4096, 16384 };
        double first = 0.0;
        int k;

        for (k = 0; k < 5; k++) {
            size_t pages = sizes[k];
            double ns;

            make_perm(perm, pages);
            ns = probe(base, ps, perm, pages, SWEEP_ACCESS, &done, &faults);
            if (k == 0) {
                first = ns;
            }
            printf("%-12zu %-14zu %-12zu %-14.2f %-12.1f %ld\n",
                   pages, pages * ps / 1024, done, ns, ns / first, faults);
        }
    }
    printf("\n说明：耗时受数据 cache、TLB、页表缓存、循环与虚拟机环境等共同影响，\n");
    printf("      不能从曲线直接测出 TLB 容量，没有明显拐点也应如实记录。\n");
    printf("      新增轻微缺页来自 ru_minflt，不是 TLB 未命中次数，也不包含重大缺页。\n");

    free((void *)base);
    free(perm);
    return 0;
}
