# Understanding Kernel Oops messages

When there is an error in the kernel, if debugging modules are added during build, messages will show up.
In the example below, a NULL pointer was defereneced, causing the kernel to crash.
```
# echo “hello_world” > /dev/faulty
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
```
When this happens, kernel sends useful messages. Information about the inserted modules can be found a bit below. There were 3 custom modules "scull", "faulty" and "hello" running, with the faulty module having PID of 155.
```
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: scull(O) faulty(O) hello(O)
CPU: 0 PID: 155 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
```
It also spits out the call trace, helping us to debug which function is the problem, in this case it is "faulty_write"
```
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
```
Since it occured near the start of the faulty_write function address, we can deduce that the error occured near the beginning of the function faulty_write(). As seen the code faulty.c below, it was indeed a NULL pointer reference occuring near the beginning of function.
```c
// faulty.c
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
	return 0;
}
```
It tells us the stack pointer and dumps the stack variable as seen below. If needed, check the disassembly of the code to figure out the root cause.

```
sp : ffffffc008dabd20
x29: ffffffc008dabd80 x28: ffffff8001b2c240 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008dabdc0
x20: 000000557d20a710 x19: ffffff8001bcbe00 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000785000 x3 : ffffffc008dabdc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
```
Using these various kernel information during an "oops" can help you debug kernel modules.