# Faulty Module Oops Analysis

## Command Used

The following command was executed from the QEMU command line:

echo "hello_world" > /dev/faulty

## Observation (oops output )

Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041c0a000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#2] SMP
Modules linked in: faulty(O) hello(O) scull(O) [last unloaded: faulty(O)]
CPU: 0 PID: 134 Comm: sh Tainted: G      D    O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc0/0x3a0
sp : ffffffc008dbbd10
x29: ffffffc008dbbd80 x28: ffffff8001aa4f80 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040000000 x22: 000000000000000c x21: ffffffc008dbbdc0
x20: 00000055678a64c0 x19: ffffff8001b7c900 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000788000 x3 : ffffffc008dbbdc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x120
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0x114/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---

## Analysis 

The kernel oops is caused by a NULL pointer dereference inside the faulty driver.
In the driver code, the faulty_write() function contains an intentional bug:

*(int *)0 = 0;

This line tries to write to memory address 0x0, which is not valid in kernel space.
As a result, the kernel throws an exception and generates an oops.

Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
This means the kernel attempted to access address 0x0, which is illegal.

from kernel output( dmesg) :

Exception class
***
EC = 0x25: DABT (current EL) 
***
DABT= Data Abort , indicates a memory access failure 

Fault Status Code 
***
FSC = 0x05: level 1 translation fault 
***
this means the virtual address could not be translated to a physical address 

## Key Register Information 

Program Counter (PC)
***
pc : faulty_write+0x10/0x20 [faulty]
***
Shows the exact location of the crash , Confirms the fault occurred inside faulty_write().


Link Register (LR)
***
lr : vfs_write+0xc0/0x3a0
***
Indicates the function that called faulty_write()

## Call trace 
The call trace shows the path from the user-space write() system call into the kernel:
faulty_write
ksys_write
__arm64_sys_write
invoke_syscall
do_el0_svc

This demonstrates that writing to /dev/faulty enters the character driver’s write handler, which then crashes due to the intentional NULL pointer 
dereference.

## Conclusion 
The oops is expected behavior for this module. Writing to /dev/faulty causes the driver to access an invalid NULL pointer, which makes the kernel 
crash and print the oops information









