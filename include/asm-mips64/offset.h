/* DO NOT TOUCH, AUTOGENERATED BY OFFSET.C */

#ifndef _MIPS_OFFSET_H
#define _MIPS_OFFSET_H

/* MIPS pt_regs offsets. */
#define PT_R0     0
#define PT_R1     8
#define PT_R2     16
#define PT_R3     24
#define PT_R4     32
#define PT_R5     40
#define PT_R6     48
#define PT_R7     56
#define PT_R8     64
#define PT_R9     72
#define PT_R10    80
#define PT_R11    88
#define PT_R12    96
#define PT_R13    104
#define PT_R14    112
#define PT_R15    120
#define PT_R16    128
#define PT_R17    136
#define PT_R18    144
#define PT_R19    152
#define PT_R20    160
#define PT_R21    168
#define PT_R22    176
#define PT_R23    184
#define PT_R24    192
#define PT_R25    200
#define PT_R26    208
#define PT_R27    216
#define PT_R28    224
#define PT_R29    232
#define PT_R30    240
#define PT_R31    248
#define PT_LO     256
#define PT_HI     264
#define PT_EPC    272
#define PT_BVADDR 280
#define PT_STATUS 288
#define PT_CAUSE  296
#define PT_SIZE   304

/* MIPS task_struct offsets. */
#define TASK_STATE         0
#define TASK_FLAGS         8
#define TASK_SIGPENDING    16
#define TASK_NEED_RESCHED  40
#define TASK_COUNTER       56
#define TASK_PRIORITY      64
#define TASK_MM            80
#define TASK_STRUCT_SIZE   1480

/* MIPS specific thread_struct offsets. */
#define THREAD_REG16   896
#define THREAD_REG17   904
#define THREAD_REG18   912
#define THREAD_REG19   920
#define THREAD_REG20   928
#define THREAD_REG21   936
#define THREAD_REG22   944
#define THREAD_REG23   952
#define THREAD_REG29   960
#define THREAD_REG30   968
#define THREAD_REG31   976
#define THREAD_STATUS  984
#define THREAD_FPU     992
#define THREAD_BVADDR  1256
#define THREAD_BUADDR  1264
#define THREAD_ECODE   1272
#define THREAD_TRAPNO  1280
#define THREAD_MFLAGS  1288
#define THREAD_CURDS   1296
#define THREAD_TRAMP   1304
#define THREAD_OLDCTX  1312

/* Linux mm_struct offsets. */
#define MM_USERS      32
#define MM_PGD        24
#define MM_CONTEXT    112

/* Linux sigcontext offsets. */
#define SC_REGS       0
#define SC_FPREGS     256
#define SC_MDHI       512
#define SC_MDLO       520
#define SC_PC         528
#define SC_STATUS     536
#define SC_OWNEDFP    540
#define SC_FPC_CSR    544
#define SC_FPC_EIR    548
#define SC_CAUSE      552
#define SC_BADVADDR   556

#endif /* !(_MIPS_OFFSET_H) */
