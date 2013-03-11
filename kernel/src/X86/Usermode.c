/**
| Copyright(C) 2012 Ali Ersenal
| License: WTFPL v2
| URL: http://sam.zoy.org/wtfpl/COPYING
|
|--------------------------------------------------------------------------
| Usermode.c
|--------------------------------------------------------------------------
|
| DESCRIPTION:  Handles ring0 to ring3 switch and syscalls.
|
| AUTHOR:       Ali Ersenal, aliersenal@gmail.com
\------------------------------------------------------------------------*/


#include <X86/Usermode.h>
#include <Common.h>
#include <Debug.h>
#include <X86/IDT.h>
#include <X86/GDT.h>
#include <Process/ProcessManager.h>
#include <Memory/VirtualMemory.h>

/*=======================================================
    DEFINE
=========================================================*/
#define SYSCALL_INTERRUPT   0x80
#define NUMBER_OF_CALLS        4

/*=======================================================
    PRIVATE DATA
=========================================================*/
PRIVATE Module userModule;
PRIVATE void*  usermodeEntryAddr;

PRIVATE void* syscalls[NUMBER_OF_CALLS] = {

    &Console_printString,
    &Console_printChar,
    &ProcessManager_killProcess,
    &ProcessManager_spawnProcess,

};

/*=======================================================
    FUNCTION
=========================================================*/

PRIVATE void Usermode_syscallHandler(Regs* regs) {

    /* Valid call request? */
    Debug_assert(regs->eax < NUMBER_OF_CALLS);

    int ret = 0;

    asm volatile("\
        push %1;   \
        push %2;   \
        push %3;   \
        push %4;   \
        push %5;   \
        call *%6;  \
        pop %%ebx; \
        pop %%ebx; \
        pop %%ebx; \
        pop %%ebx; \
        pop %%ebx; \
    " : "=a" (ret) : "r" (regs->edi), "r" (regs->esi), "r" (regs->edx), "r" (regs->ecx), "r" (regs->ebx), "r" (syscalls[regs->eax]));

    regs->eax = ret; /* Store return value in eax */

}

PRIVATE void Usermode_init(void) {

    IDT_registerHandler(&Usermode_syscallHandler, SYSCALL_INTERRUPT);

    Process* init = ProcessManager_spawnProcess("/HelloWorld");
    GDT_setTSS(KERNEL_DATA_SEGMENT, (u32int) init->kernelStack);
    asm volatile("mov %0, %%esp" : : "r" (init->userStack)); /* Store initial process ESP in DR0 register */
    VirtualMemory_switchPageDir(init->pageDir);

    asm volatile("        \
        cli;              \
        mov $0x23, %%ax;   \
        mov %%ax, %%ds;     \
        mov %%ax, %%es;     \
        mov %%ax, %%fs;     \
        mov %%ax, %%gs;     \
                          \
        mov %%esp, %%eax;   \
        pushl $0x23;      \
        pushl %%eax;       \
        pushf;            \
        pop %%eax;         \
        or $0x200, %%eax;  \
        push %%eax;        \
        pushl $0x1B;      \
        push  %0;         \
        iret;             \
    " : : "S" (usermodeEntryAddr));

}

PUBLIC Module* Usermode_getModule(void* usermodeEntry) {

    Debug_assert(usermodeEntry != NULL);

    if(!userModule.isLoaded) {

        userModule.moduleName = "Usermode";
        userModule.init = &Usermode_init;
        userModule.moduleID = MODULE_USERMODE;
        userModule.numberOfDependencies = 1;
        userModule.dependencies[0] = MODULE_PROCESS;

        usermodeEntryAddr = usermodeEntry;

    }

    return &userModule;
}