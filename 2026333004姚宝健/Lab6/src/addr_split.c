/* addr_split.c —— 把一个虚拟地址拆成页号与页内偏移
 *
 * 编译：gcc -O0 -o addr_split addr_split.c
 * 运行：./addr_split
 *
 * 程序打印三个对象（全局变量、堆、栈）的地址，按本机页大小拆成
 * “页号 VPN + 页内偏移 offset”，并把该地址所在的 /proc/self/maps
 * 区间一起打印出来，便于对照。
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int global_var = 1;                 /* 全局变量，位于数据段 */

/* 在 /proc/self/maps 中查找 addr 落在哪个区间 */
static void show_region(unsigned long addr)
{
    FILE *fp = fopen("/proc/self/maps", "r");
    char line[512];

    if (fp == NULL) {
        printf("         所在区间：读不到 /proc/self/maps\n");
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned long start = 0, end = 0;
        if (sscanf(line, "%lx-%lx", &start, &end) == 2 &&
            addr >= start && addr < end) {
            printf("         所在区间：%s", line);      /* line 自带换行 */
            break;
        }
    }
    fclose(fp);
}

static void show(const char *tag, void *p, unsigned long ps)
{
    unsigned long addr = (unsigned long)p;
    unsigned long vpn = addr / ps;
    unsigned long offset = addr % ps;
    unsigned long page_base = addr - offset;

    printf("%s\n", tag);
    printf("         地址             0x%016lx\n", addr);
    printf("         页号 VPN         %lu\n", vpn);
    printf("         页内偏移 offset  %lu\n", offset);
    printf("         本页首地址       0x%016lx\n", page_base);
    printf("         地址 +1 的页号    %lu（%s）\n", (addr + 1) / ps,
           (addr + 1) / ps == vpn ? "仍在同一页" : "已跨到下一页");
    printf("         下一页首地址     0x%016lx，页号 %lu\n",
           page_base + ps, (page_base + ps) / ps);
    show_region(addr);
    printf("\n");
}

int main(void)
{
    unsigned long ps = (unsigned long)sysconf(_SC_PAGESIZE);
    unsigned long bits = 0, tmp;
    unsigned long width;
    int stack_var = 2;
    int *heap_var = malloc(sizeof(int));

    if (heap_var == NULL) {
        perror("malloc");
        return 1;
    }
    *heap_var = 3;

    /* 页大小是 2 的几次方 */
    tmp = ps;
    while (tmp > 1) {
        tmp >>= 1;
        bits++;
    }
    width = (unsigned long)sizeof(void *) * 8;

    printf("pid = %d\n", (int)getpid());
    printf("pagesize = %lu 字节 = 2^%lu，因此页内偏移占 %lu 位\n", ps, bits, bits);
    printf("指针宽度 = %lu 位\n\n", width);

    show("【全局变量】", &global_var, ps);
    show("【堆上的对象】", heap_var, ps);
    show("【栈上的变量】", &stack_var, ps);

    free(heap_var);
    return 0;
}
