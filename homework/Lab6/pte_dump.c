/* pte_dump.c —— 读 /proc/self/pagemap，观察一个虚拟页的状态
 *
 * 编译：gcc -O0 -o pte_dump pte_dump.c
 * 运行：sudo ./pte_dump
 *
 * pagemap 是内核导出格式，不是原始硬件 PTE。
 * 每项 8 字节，第 VPN 项对应第 VPN 页。
 *   第 63 位     present，页当前是否在物理内存中
 *   第 62 位     swapped，页是否已换出
 *   低 55 位     页在内存时表示 PFN；换出时表示交换位置
 * PFN 可见性依赖 CAP_SYS_ADMIN 等权限，容器中的 root 也可能读到 0。
 * 物理地址 = PFN × 页大小 + 页内偏移。
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

struct pte_info {
    uint64_t raw;
    uint64_t pfn;
    int present;
    int swapped;
    int ok;
};

static struct pte_info read_pte(void *p, unsigned long ps)
{
    struct pte_info info;
    unsigned long vpn = (unsigned long)p / ps;
    int fd;
    ssize_t n;

    memset(&info, 0, sizeof(info));
    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        perror("open /proc/self/pagemap");
        return info;
    }
    n = pread(fd, &info.raw, sizeof(info.raw), (off_t)(vpn * 8));
    if (n != (ssize_t)sizeof(info.raw)) {
        if (n < 0) {
            perror("pread /proc/self/pagemap");
        } else {
            printf("         读 pagemap 条目失败（返回值 %ld）\n", (long)n);
        }
        close(fd);
        return info;
    }
    close(fd);
    info.ok = 1;
    info.present = (int)((info.raw >> 63) & 1);
    info.swapped = (int)((info.raw >> 62) & 1);
    if (info.present) {
        info.pfn = info.raw & ((1ULL << 55) - 1);
    }
    return info;
}

static void show(const char *tag, void *p, unsigned long ps)
{
    unsigned long addr = (unsigned long)p;
    unsigned long offset = addr % ps;
    struct pte_info e = read_pte(p, ps);

    printf("%s\n", tag);
    printf("         地址              0x%016lx\n", addr);
    printf("         页内偏移 offset   %lu\n", offset);
    if (!e.ok) {
        printf("\n");
        return;
    }
    printf("         pagemap 条目64位  0x%016llx\n", (unsigned long long)e.raw);
    printf("         present（第63位） %d\n", e.present);
    if (e.swapped) {
        printf("         swapped（第62位） 1，低位是交换位置，不是 PFN\n");
        printf("         PFN               不可用\n");
    } else {
        printf("         PFN（低55位）     %llu\n", (unsigned long long)e.pfn);
    }
    if (!e.present) {
        printf("         结论              这一页当前不在内存，无法得到物理地址\n");
    } else if (e.pfn == 0) {
        printf("         结论              页已在内存，但 PFN 为 0，可能因权限受限而隐藏\n");
        printf("                           可在课程 Ubuntu 虚拟机用 sudo 核对；仍为 0 时记录限制，不计算物理地址\n");
    } else {
        printf("         物理地址          %llu × %lu + %lu = 0x%llx\n",
               (unsigned long long)e.pfn, ps, offset,
               (unsigned long long)(e.pfn * (uint64_t)ps + (uint64_t)offset));
    }
    printf("\n");
}

int main(void)
{
    unsigned long ps = (unsigned long)sysconf(_SC_PAGESIZE);
    char *area;
    int stack_var = 7;

    if (geteuid() != 0) {
        printf("注意：当前不是 root，PFN 可能被内核清零；课程 Ubuntu 虚拟机可用 sudo 核对。\n\n");
    }
    printf("pid = %d\n", (int)getpid());
    printf("pagesize = %lu 字节\n\n", ps);

    /* 申请一页，但先不碰它：此时页还没有装入 */
    area = mmap(NULL, ps, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    printf("=== 第一步：只申请，还没有写过 ===\n");
    show("【匿名映射的一页】", area, ps);

    printf("=== 第二步：写入 1 个字节，再读一次 ===\n");
    area[0] = 'A';
    show("【匿名映射的一页】", area, ps);

    printf("=== 第三步：对照一个已经在用的页 ===\n");
    show("【栈上的变量】", &stack_var, ps);

    munmap(area, ps);
    return 0;
}
