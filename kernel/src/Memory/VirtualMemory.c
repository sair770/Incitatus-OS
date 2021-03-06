/**
| Copyright(C) 2012 Ali Ersenal
| License: WTFPL v2
| URL: http://sam.zoy.org/wtfpl/COPYING
|
|--------------------------------------------------------------------------
| VirtualMemory.c
|--------------------------------------------------------------------------
|
| DESCRIPTION:  Platform dependent X86 virtual memory manager, initialiser.
|
| AUTHOR:       Ali Ersenal, aliersenal@gmail.com
\------------------------------------------------------------------------*/


#include <Memory/VirtualMemory.h>
#include <Memory/PhysicalMemory.h>
#include <X86/CPU.h>
#include <X86/IDT.h>
#include <Debug.h>
#include <Memory.h>
#include <Process/Scheduler.h>
#include <Process/ProcessManager.h>
#pragma GCC diagnostic ignored "-Wstrict-aliasing" /* ignore FORCE_CAST compiler warning */

/*=======================================================
    DEFINE
=========================================================*/
#define PDE_INDEX(x) ((u32int) x >> 22)
#define PTE_INDEX(x) ((u32int) x >> 12  & 0x03FF)
#define ADDR_TO_FRAME_INDEX(addr) ((u32int) addr / FRAME_SIZE)
#define FRAME_INDEX_TO_ADDR(index) ((void*) (index * FRAME_SIZE))

#define KERNEL_HEAP_MAP_SIZE_MB  32

/*=======================================================
    STRUCT
=========================================================*/
typedef struct PageTableEntry     PageTableEntry;
typedef struct PageTable          PageTable;
typedef struct PageDirectoryEntry PageDirectoryEntry;

/* 4-Byte Page Table Entry */
struct PageTableEntry {

    /* Whether the page is in memory(is present) or not
        0: Not in memory
        1: In memory
    */
    u8int  inMemory     :  1;

    /* Read/Write flag
        0: Read only
        1: Read + Write
    */
    u8int  rwFlag       :  1;

    /* Page operation mode.
        0: Page is kernel mode.
        1: Page is user mode.
    */
    u8int  mode         :  1;

    /* Reserved */
    u8int               :  2;

    /* Access flag
        0: Has not been accessed.
        1: Has been accessed.
    */
    u8int  isAccessed   :  1;

    /* Is this page dirty?
        0: Has not been written to.
        1: Has been written to.
    */
    u8int  isDirty      :  1;

    /* Reserved */
    u16int              :  2;

    /* Available for use */
    u8int               :  3;

    /* Frame address */
    u32int frameIndex   : 20;

} __attribute__((packed));

struct PageTable {

    /* 4MB of virtual memory */
    PageTableEntry entries[1024];

};

/* 4-Byte Page Directory Entry */
struct PageDirectoryEntry {

    /* Whether the page table is in memory(is present) or not
        0: Not in memory
        1: In memory
    */
    u8int  inMemory         :  1;

    /* Read/Write flag
        0: Read only
        1: Read + Write
    */
    u8int  rwFlag           :  1;

    /* Page table operation mode.
        0: Page table is kernel mode.
        1: Page table is user mode.
    */
    u8int  mode             :  1;

    /* Write-through flag
        0: Write back cache is enabled.
        1: Write through cache is enabled.
    */
    u8int  isWriteThrough   :  1;

    /* Is cache disabled?
        0: Cache disabled
        1: Cache enabled
    */
    u8int  isCached         :  1;

    /* Access flag
        0: Has not been accessed.
        1: Has been accessed.
    */
    u8int  isAccessed       :  1;

    /* Reserved */
    u8int                   :  1;

    /* Page Size
        0: 4KB
        1: 4MB
    */
    u8int  pageSize         :  1;

    u8int  globalPage       :  1;

    /* Available for use */
    u8int                   :  3;

    /* Page table frame address */
    u32int frameIndex       : 20;

} __attribute__((packed));

struct PageDirectory {

    /* 4GB of virtual memory */
    PageDirectoryEntry entries[1024];

};

/*=======================================================
    PRIVATE DATA
=========================================================*/
PRIVATE Module vmmModule;
PRIVATE PageDirectory* kernelDir;

/*=======================================================
    FUNCTION
=========================================================*/

PRIVATE inline void VirtualMemory_invalidateTLB(void) {

    asm volatile(

        "movl  %cr3,%eax    \n"
        "movl  %eax,%cr3    \n"

    );

}

PRIVATE inline void VirtualMemory_invalidateTLBEntry(void* addr) {

    asm volatile("invlpg (%0)" ::"r" (addr) : "memory");

}

PRIVATE void VirtualMemory_setPaging(bool state) {

    u32int cr0 = CPU_getCR(0);
    CR0 reg = FORCE_CAST(cr0, CR0);
    reg.PG = state;
    CPU_setCR(0, FORCE_CAST(reg, u32int));

}

PRIVATE void VirtualMemory_pageFaultHandler(Regs* regs) {

    Process* process = Scheduler_getCurrentProcess();
    Debug_assert(process != NULL);

    u32int faultAddr;
    asm volatile("mov %%CR2, %0" : "=a" (faultAddr)); /* Retrieve the address that raised the page fault */

    Console_setColor(CONSOLE_ERROR);
    Console_printf("%s", "Process page fault!\n");
    Console_printf("%s%d%c", "pid: ", process->pid, '\n');
    Console_printf("%s%d%c%d%c", "cs-eip: ", regs->cs, ':' ,regs->eip, '\n');
    Console_printf("%s%d%c", "address: ", faultAddr, '\n');

    if(process->pid == KERNEL_PID) { /* Panic if it is the kernel process */

        Sys_panic("Page fault!");

    } else { /* User process, kill it */

        ProcessManager_killProcess(-1);

    }


}

PRIVATE void VirtualMemory_setPDE(PageDirectoryEntry* pde, PageTable* pageTable, bool mode) {

    Debug_assert(pde != NULL && pageTable != NULL);

    Memory_set(pde, 0, sizeof(PageDirectoryEntry));
    pde->frameIndex = ADDR_TO_FRAME_INDEX(pageTable);
    pde->inMemory = TRUE;
    pde->rwFlag = TRUE;
    pde->mode = mode;

}

PRIVATE void VirtualMemory_setPTE(PageTableEntry* pte, void* physicalAddr, bool mode) {

    Debug_assert(pte != NULL && (u32int) physicalAddr % FRAME_SIZE == 0);

    Memory_set(pte, 0, sizeof(PageTableEntry));
    pte->frameIndex = ADDR_TO_FRAME_INDEX(physicalAddr);
    pte->inMemory = TRUE;
    pte->rwFlag = TRUE;
    pte->mode = mode;

}

PRIVATE void VirtualMemory_init(void) {

    Debug_logInfo("%s%s", "Initialising ", vmmModule.moduleName);

    /* Create the initial page directory */
    PageDirectory* dir = PhysicalMemory_allocateFrame();
    Memory_set(dir, 0, sizeof(PageDirectory));
    VirtualMemory_switchPageDir(dir);
    kernelDir = dir;

    /* Identity map first 4MB (except first 4096kb in order to catch NULLs) */
    PageTable* first4MB = PhysicalMemory_allocateFrame();

    for(int i = 1; i < 1024; i++) {

        PageTableEntry* pte = &first4MB->entries[i];
        VirtualMemory_setPTE(pte, FRAME_INDEX_TO_ADDR(i), MODE_KERNEL);

    }

    VirtualMemory_setPDE(&dir->entries[0], first4MB, MODE_KERNEL);
    /* End of identity map */

    /* Map directory to last virtual 4MB - recursive mapping, lets us manipulate the page directory after paging is enabled */
    dir->entries[1023].frameIndex = ADDR_TO_FRAME_INDEX(dir);
    dir->entries[1023].inMemory = TRUE;
    dir->entries[1023].rwFlag = TRUE;
    dir->entries[1023].mode = MODE_KERNEL;

    /* Register page fault handler */
    IDT_registerHandler(&VirtualMemory_pageFaultHandler, 14);

    /* Turn on paging */
    VirtualMemory_setPaging(TRUE);

}

PUBLIC void* VirtualMemory_quickMap(void* virtualAddr, void* physicalAddr) {

    /* Map to current page dir */

    /* Addresses should be page aligned */
    Debug_assert((u32int) virtualAddr % FRAME_SIZE == 0 && (u32int) physicalAddr % FRAME_SIZE == 0);

    PageDirectory* dir = (PageDirectory*) 0xFFFFF000; /* Get current page directory */
    PageDirectoryEntry* pde = &dir->entries[PDE_INDEX(virtualAddr)];

    if(!pde->inMemory) { /* Need to allocate a page table */

        PageTable* pageTable = PhysicalMemory_allocateFrame();

        Debug_assert(pageTable != NULL); /* Out of physical memory */

        VirtualMemory_setPDE(pde, pageTable, MODE_KERNEL);
        pageTable = (PageTable*) (((u32int*) 0xFFC00000) + (0x400 * PDE_INDEX(virtualAddr)));
        Memory_set(pageTable, 0, sizeof(PageTable));

    }

    PageTable* pageTable = (PageTable*) (((u32int*) 0xFFC00000) + (0x400 * PDE_INDEX(virtualAddr)));
    PageTableEntry* pte = &pageTable->entries[PTE_INDEX(virtualAddr)];
    VirtualMemory_setPTE(pte, physicalAddr, MODE_KERNEL);
    VirtualMemory_invalidateTLBEntry(virtualAddr);

    return virtualAddr;

}

PUBLIC void VirtualMemory_quickUnmap(void* virtualAddr) {

    /* Unmap from current page dir */

    /* Address should be page aligned */
    Debug_assert((u32int) virtualAddr % FRAME_SIZE == 0);

    PageDirectory* dir = (PageDirectory*) 0xFFFFF000; /* Get current page directory */
    PageDirectoryEntry* pde = &dir->entries[PDE_INDEX(virtualAddr)];

    Debug_assert(pde->inMemory);

    PageTable* pageTable = (PageTable*) (((u32int*) 0xFFC00000) + (0x400 * PDE_INDEX(virtualAddr)));
    PageTableEntry* pte = &pageTable->entries[PTE_INDEX(virtualAddr)];

    Debug_assert(pte->inMemory);

    Memory_set(pte, 0, sizeof(PageTableEntry));
    VirtualMemory_invalidateTLBEntry(virtualAddr);

}

PUBLIC PageDirectory* VirtualMemory_getKernelDir(void) {

    return kernelDir;

}

PUBLIC void VirtualMemory_switchPageDir(PageDirectory* dir) {

    Debug_assert(dir != NULL);

    /* Set page directory base register */
    u32int cr3 = CPU_getCR(3);
    CR3 reg = FORCE_CAST(cr3, CR3);
    reg.PDBR = (u32int) &dir->entries / FRAME_SIZE;
    CPU_setCR(3, FORCE_CAST(reg, u32int));

    /* Flush all TLB entries */
    VirtualMemory_invalidateTLB();
}

PUBLIC void VirtualMemory_mapKernel(Process* process) {

    Debug_assert(process != NULL);

    PageDirectory* pageDir = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR, process->pageDir); /* Map it so that we can access it */
    PageDirectory* kDir = VirtualMemory_quickMap((void*) (TEMPORARY_MAP_VADDR + 0x1000), kernelDir); /* Map it so that we can access it */

    /* Map bottom 4MB */
    PageDirectoryEntry* pde = &pageDir->entries[0];
    Memory_set(pde, 0, sizeof(PageDirectoryEntry));
    pde->frameIndex = kDir->entries[0].frameIndex;
    pde->inMemory = TRUE;
    pde->rwFlag = TRUE;
    pde->mode = MODE_KERNEL;

    /* Map kernel heap, starting from  (KERNEL_HEAP_BASE_VADDR) to (KERNEL_HEAP_BASE_VADDR + KERNEL_HEAP_MAP_SIZE_MB) */
    for(u32int i = PDE_INDEX(KERNEL_HEAP_BASE_VADDR); i < PDE_INDEX(KERNEL_HEAP_BASE_VADDR) + KERNEL_HEAP_MAP_SIZE_MB / 4; i++) {

        pde = &pageDir->entries[i];
        Memory_set(pde, 0, sizeof(PageDirectoryEntry));

        if(kDir->entries[i].inMemory) {

            pde->frameIndex = kDir->entries[i].frameIndex;
            pde->inMemory = TRUE;
            pde->rwFlag = TRUE;
            pde->mode = MODE_KERNEL;

        }

    }

    /* Unmap temporary mappings */
    VirtualMemory_quickUnmap((void*) TEMPORARY_MAP_VADDR);
    VirtualMemory_quickUnmap((void*) (TEMPORARY_MAP_VADDR + 0x1000));

}

PUBLIC void VirtualMemory_createPageDirectory(Process* process) {

    Debug_assert(process->pageDir == NULL);

    PageDirectory* dir = (PageDirectory*) PhysicalMemory_allocateFrame();
    process->pageDir = dir;
    dir = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR, dir); /* Map it so that we can access it */
    Memory_set(dir, 0, sizeof(PageDirectory));

    /* Map directory to last virtual 4MB - recursive mapping, lets us manipulate the page directory after paging is enabled */
    dir->entries[1023].frameIndex = (ADDR_TO_FRAME_INDEX(process->pageDir));
    dir->entries[1023].inMemory = TRUE;
    dir->entries[1023].rwFlag = TRUE;
    dir->entries[1023].mode = MODE_KERNEL;

    /* Set all other entries as kernel mode as default */
    for(int i = 0; i < 1023; i++)
        dir->entries[i].mode = MODE_KERNEL;

    VirtualMemory_quickUnmap((void*) TEMPORARY_MAP_VADDR);

}

PUBLIC void VirtualMemory_destroyPageDirectory(Process* process) {

    Debug_assert(process->pageDir != NULL);

    /* Free every page table starting at 1GB(everything except kernel which is bottom 4MB + kernel heap) */
    for(int i = PDE_INDEX(USER_CODE_BASE_VADDR); i < 1023; i++) {

        /* Map page directory so that we can access it */
        PageDirectory* dir = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR, process->pageDir);
        PageDirectoryEntry* pde = &dir->entries[i];

        if(pde->inMemory) {

            PageTable* pageTable = (PageTable*) FRAME_INDEX_TO_ADDR(pde->frameIndex);
            void* pageTablePhys = pageTable;
            Debug_assert(pageTable != NULL);
            pageTable = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR, pageTable); /* Map page table so that we can access it */

            /* Free all page table entries*/
            for(int y = 0; y < 1024; y++) {

                PageTableEntry* pte = &pageTable->entries[y];

                if(pte->inMemory) {

                    void* phys = FRAME_INDEX_TO_ADDR(pte->frameIndex);
                    Debug_assert(phys != NULL);
                    PhysicalMemory_freeFrame(phys);

                }

            }

            PhysicalMemory_freeFrame(pageTablePhys);

        }

    }

    VirtualMemory_quickUnmap((void*) TEMPORARY_MAP_VADDR);
    PhysicalMemory_freeFrame(process->pageDir);

}

PUBLIC void* VirtualMemory_mapPage(PageDirectory* dir, void* virtualAddr, void* physicalAddr, bool mode) {

    /* Addresses should be page aligned */
    Debug_assert((u32int) virtualAddr % FRAME_SIZE == 0 && (u32int) physicalAddr % FRAME_SIZE == 0);
    Debug_assert(dir != NULL);

    dir = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR, dir);
    PageDirectoryEntry* pde = &dir->entries[PDE_INDEX(virtualAddr)];

    if(!pde->inMemory) { /* Need to allocate a page table */

        PageTable* pageTable = PhysicalMemory_allocateFrame();

        Debug_assert(pageTable != NULL); /* Out of physical memory */

        VirtualMemory_setPDE(pde, pageTable, mode);
        pageTable = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR + 0x1000, pageTable);
        Memory_set(pageTable, 0, sizeof(PageTable));

    }

    PageTable* pageTable = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR + 0x1000, FRAME_INDEX_TO_ADDR(pde->frameIndex));
    PageTableEntry* pte = &pageTable->entries[PTE_INDEX(virtualAddr)];
    VirtualMemory_setPTE(pte, physicalAddr, mode);
    VirtualMemory_invalidateTLBEntry(virtualAddr);

    VirtualMemory_quickUnmap((void*) TEMPORARY_MAP_VADDR);
    VirtualMemory_quickUnmap((void*) TEMPORARY_MAP_VADDR + 0x1000);

    return virtualAddr;

}

PUBLIC void VirtualMemory_unmapPage(PageDirectory* dir, void* virtualAddr) {

    /* Address should be page aligned */
    Debug_assert((u32int) virtualAddr % FRAME_SIZE == 0);
    Debug_assert(dir != NULL);

    dir = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR, dir);
    PageDirectoryEntry* pde = &dir->entries[PDE_INDEX(virtualAddr)];

    Debug_assert(pde->inMemory);

    PageTable* pageTable = VirtualMemory_quickMap((void*) TEMPORARY_MAP_VADDR, FRAME_INDEX_TO_ADDR(pde->frameIndex));
    PageTableEntry* pte = &pageTable->entries[PTE_INDEX(virtualAddr)];

    Debug_assert(pte->inMemory);

    Memory_set(pte, 0, sizeof(PageTableEntry));
    VirtualMemory_invalidateTLBEntry(virtualAddr);
    VirtualMemory_quickUnmap((void*) TEMPORARY_MAP_VADDR);

}

PUBLIC void* VirtualMemory_getPhysicalAddress(void* virtualAddr) {

    /* Address should be page aligned */
    Debug_assert((u32int) virtualAddr % FRAME_SIZE == 0);

    PageDirectory* dir = (PageDirectory*) 0xFFFFF000;
    PageDirectoryEntry* pde = &dir->entries[PDE_INDEX(virtualAddr)];

    Debug_assert(pde->inMemory);

    PageTable* pageTable = (PageTable*) (((u32int*) 0xFFC00000) + (0x400 * PDE_INDEX(virtualAddr)));
    PageTableEntry* pte = &pageTable->entries[PTE_INDEX(virtualAddr)];

    Debug_assert(pte->inMemory);

    return (void*) FRAME_INDEX_TO_ADDR(pte->frameIndex);
}

PUBLIC Module* VirtualMemory_getModule(void) {

    if(!vmmModule.isLoaded) {

        vmmModule.moduleName = "Virtual Memory Manager";
        vmmModule.moduleID = MODULE_VMM;
        vmmModule.init = &VirtualMemory_init;
        vmmModule.numberOfDependencies = 2;
        vmmModule.dependencies[0] = MODULE_IDT;
        vmmModule.dependencies[1] = MODULE_PMM;

    }

    return &vmmModule;

}