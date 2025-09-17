#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "console.h"
#include "threads/mmu.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void *addr);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	/* 시스템 콜 번호는 %rax레지스터에 저장 */
	switch(f->R.rax){
		case SYS_WRITE:		//Write면
		/* write는 fd,buffer,size순으로 레지스터에 담겨옴 */
			uint64_t fd = f->R.rdi;
			uint64_t buffer = f->R.rsi;
			uint64_t size = f->R.rdx;

			/* 버퍼 시작과 끝 검사하면 연속된 버퍼가 유효하다는 뜻 */
			check_address((void *)buffer);
			if (size > 0)
				check_address((void *)buffer + size -1);

			if (fd == 1){		//STDOUT이면
				putbuf((const char *)buffer, size);		//버퍼내용을 콘솔에 출력
				f->R.rax = size;			//성공했으면 size 반환
			}
			else if (fd == 0){ f->R.rax = -1; }	//STDIN이면 요류 처리 - 여기선 할거 아님
			else {f->R.rax = -1;}			//그 외도 오류 처리
			break;

		case SYS_EXIT:
			uint64_t status = (uint64_t) f->R.rdi;		//인자 개수? 종료 상태 코드
			thread_current()->exit_status = status;		//부모 깨우기 전에 상태 저장

			printf("%s: exit(%d)\n", thread_current()->name, status);		//0개 겠지뭐
			sema_up(&thread_current()->wait_sema);		//자식이 종료될때 부모를 up 시켜 깨움, 부모를 가리키고 있음
			thread_exit();		//부모 프로세스도 종료
			break;

		case SYS_HALT:
			power_off();
	}
	//thread_exit ();
}

void check_address(void *addr){
	struct thread *curr = thread_current();
	/* 커널 영역 침범 여부(KERN_BASE 미만 + NULL인지) */
	if(!is_user_vaddr(addr) || addr == NULL){
		thread_exit();		//즉시 프로세스 종료
	}
	/* 미할당 영역 접근 여부(가상주소 -> 페이지 테이블 매핑?) */
	if(pml4_get_page(curr->pml4, addr) == NULL){
		thread_exit();		//즉시 프로세스 종료
	}
}