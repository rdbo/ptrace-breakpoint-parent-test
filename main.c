#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <errno.h>

/*
 * NOTE: Important information at: http://x86asm.net/articles/debugging-in-amd64-64-bit-mode-in-theory/
 * Accessed in December, 29, 2023.
 */

int main()
{
	pid_t pid;
	int mynumber = 10;
	
	printf("Forking...\n");
	printf("&mynumber: %p\n", &mynumber);
	printf("mynumber: %d\n", mynumber);

	pid = fork();
	if (pid) {
		printf("[parent] Child process ID: %d\n", pid);
		printf("[parent] MyNumber: %d\n", mynumber);
		printf("[parent] Waiting...\n");

		sleep(1); // wait for child to attach

		printf("[parent] Writing to 'mynumber'...\n");
		mynumber = 20;
		printf("[parent] 'mynumber' changed to: %d\n", mynumber);
		printf("[parent] Done\n");
	} else {
		long result;
		pid_t ppid = getppid();
		unsigned long dr0, dr7; // Debug registers
		int status;

		printf("[child] Parent process ID: %d\n", ppid);
		printf("[child] &mynumber: %p\n", &mynumber);

		/* NOTE: This will fail if '/proc/sys/kernel/yama/ptrace_scope' is set to '1' */
		result = ptrace(PTRACE_ATTACH, ppid, NULL, NULL);
		printf("[child] PTRACE_ATTACH result: %ld\n", result);
		if (result)
			return -1;

		waitpid(ppid, NULL, WSTOPPED);

		errno = 0;
		printf("[child] Offset of DR7 in USER: %p\n", (void *)offsetof(struct user, u_debugreg[7]));
		dr7 = ptrace(PTRACE_PEEKUSER, ppid, offsetof(struct user, u_debugreg[7]), NULL);
		printf("[child] PTRACE_PEEKUSER result: %d\n", errno);
		if (errno)
			goto _detach;

		printf("[child] DR7 value: %p\n", (void *)dr7);

		dr0 = (unsigned long)&mynumber; // Set watchpoint address on dr0
		dr7 = 0xD0003; /* 0b00000000000011010000000000000011 */; // Set length and parameters for dr0 on dr7

		printf("[child] New DR0: %p\n", (void *)dr0);
		printf("[child] New DR7: %p\n", (void *)dr7);

		result = ptrace(PTRACE_POKEUSER, ppid, offsetof(struct user, u_debugreg[0]), dr0);
		printf("[child] PTRACE_POKEUSER result: %ld\n", result);
		if (result)
			goto _detach;

		result = ptrace(PTRACE_POKEUSER, ppid, offsetof(struct user, u_debugreg[7]), dr7);
		printf("[child] PTRACE_POKEUSER result: %ld\n", result);
		if (result)
			goto _detach;

		printf("[child] Continuing parent process...\n");
		ptrace(PTRACE_CONT, ppid, NULL, NULL);
		waitpid(ppid, &status, WUNTRACED | WSTOPPED);

		printf("[child] Parent process stopped with status: %d\n", status);
		if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
			printf("[child] Caught write to 'mynumber'!\n");
			printf("[child] Current value of 'mynumber': %d\n", mynumber);
		}

		printf("[child] Removing breakpoint...\n");
		dr7 = 0; // No breakpoints
		result = ptrace(PTRACE_POKEUSER, ppid, offsetof(struct user, u_debugreg[7]), dr7);
		printf("[child] PTRACE_POKEUSER result: %ld\n", result);
		
		printf("[child] Resuming parent process normally...\n");
		ptrace(PTRACE_CONT, ppid, NULL, NULL);

		printf("[child] Waiting for process to stop...\n");
		waitpid(ppid, &status, WUNTRACED | WSTOPPED);
		printf("[child] Process stopped with status: %d\n", status);

		_detach:
		ptrace(PTRACE_DETACH, ppid, NULL, NULL);
		printf("[child] Detached from parent\n");
	}
	
	return 0;
}
