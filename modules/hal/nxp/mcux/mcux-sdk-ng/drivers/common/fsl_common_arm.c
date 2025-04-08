void DelayLoop(uint32_t count)
{
#if defined(__XTENSA__)
	__asm__ volatile("1: addi %0, %0, -1\n"
			 "   bnez %0, 1b\n"
			 : "+r"(count)
			 :
			 : "memory");
#else
	__ASM volatile("    MOV    R0, %0" : : "r"(count));
	__ASM volatile("1:  \n"
		       "    SUBS   R0, R0, #1  \n"
		       "    BNE    1b         \n"
		       :
		       :
		       : "r0", "cc");
#endif
}