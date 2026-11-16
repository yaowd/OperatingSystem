/* addr_split.c —— 把虚拟地址拆成虚拟页号与页内偏移
 *
 * 编译：gcc -O0 -o addr_split addr_split.c
 * 运行：./addr_split
 *
 * 打印全局变量、堆对象和栈变量的虚拟地址、VPN、页内偏移与页首地址，
 * 并显示该地址所在的 /proc/self/maps 映射区间。
 * 地址与 VPN 采用十六进制；偏移同时显示十六进制和十进制（字节）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int global_var = 1;                 /* 全局变量，位于数据段 */

/* 在 /proc/self/maps 中查找包含 addr 的虚拟内存映射区间 */
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
            printf("         所在区间：%s", line);  /* line 自带换行 */
            break;
        }
    }
    fclose(fp);
}

static void show(const char *tag, void *p, unsigned long ps, int offset_hex_digits)
{
    unsigned long addr = (unsigned long)p;
    unsigned long vpn = addr / ps;
    unsigned long offset = addr % ps;
    unsigned long page_base = addr - offset;

    printf("%s\n", tag);
    printf("         虚拟地址         0x%016lx\n", addr);
    printf("         虚拟页号 VPN     0x%lx\n", vpn);
    printf("         页内偏移 offset  0x%0*lx（%lu 字节）\n",
           offset_hex_digits, offset, offset);
    printf("         本页首地址       0x%016lx\n", page_base);
    printf("         下一页首地址     0x%016lx\n", page_base + ps);
    printf("         下一页 VPN       0x%lx\n", vpn + 1);
    show_region(addr);
    printf("\n");
}

int main(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned long ps;
    unsigned long bits = 0, tmp;
    unsigned long width;
    int offset_hex_digits;
    int stack_var = 2;
    int *heap_var;

    if (page_size <= 0) {
        fprintf(stderr, "获取页大小失败\n");
        return 1;
    }
    ps = (unsigned long)page_size;
    if ((ps & (ps - 1)) != 0) {
        fprintf(stderr, "本程序要求页大小是 2 的整数次幂\n");
        return 1;
    }

    heap_var = malloc(sizeof(int));
    if (heap_var == NULL) {
        perror("malloc");
        return 1;
    }
    *heap_var = 3;

    /* 页大小为 2^bits；每 4 位二进制对应 1 位十六进制 */
    tmp = ps;
    while (tmp > 1) {
        tmp >>= 1;
        bits++;
    }
    offset_hex_digits = (int)((bits + 3) / 4);
    width = (unsigned long)sizeof(void *) * 8;

    printf("pid = %d\n", (int)getpid());
    printf("pagesize = %lu 字节 = 0x%lx = 2^%lu，页内偏移占 %lu 位\n",
           ps, ps, bits, bits);
    printf("指针宽度 = %lu 位\n\n", width);

    show("【全局变量】", &global_var, ps, offset_hex_digits);
    show("【堆上的对象】", heap_var, ps, offset_hex_digits);
    show("【栈上的变量】", &stack_var, ps, offset_hex_digits);

    free(heap_var);
    return 0;
}
