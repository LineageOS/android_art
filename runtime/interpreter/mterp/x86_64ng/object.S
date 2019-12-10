%def op_check_cast():
  jmp NterpCheckCast

%def op_iget_boolean():
   jmp NterpGetBooleanInstanceField

%def op_iget_boolean_quick():
%  op_iget_quick(load="movsbl")

%def op_iget_byte():
   jmp NterpGetByteInstanceField

%def op_iget_byte_quick():
%  op_iget_quick(load="movsbl")

%def op_iget_char():
   jmp NterpGetCharInstanceField

%def op_iget_char_quick():
%  op_iget_quick(load="movzwl")

%def op_iget_object():
    jmp NterpGetObjectInstanceField

%def op_iget_object_quick():
   movzwq  2(rPC), %rax                    # eax <- field byte offset
   movl    rINST, %ecx                     # rcx <- BA
   sarl    $$4, %ecx                       # ecx <- B
   GET_VREG %ecx, %rcx                     # vB (object we're operating on)
   testl   %ecx, %ecx                      # is object null?
   je      common_errNullObject
   testb $$READ_BARRIER_TEST_VALUE, GRAY_BYTE_OFFSET(%ecx)
   movl (%rcx,%rax,1), %eax
   jnz 2f
1:
   andb    $$0xf,rINSTbl                   # rINST <- A
   SET_VREG_OBJECT %eax, rINSTq            # fp[A] <- value
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
   // reg00 is eax
   call art_quick_read_barrier_mark_reg00
   jmp 1b

%def op_iget_quick(load="movl", wide="0"):
    /* For: iget-quick, iget-boolean-quick, iget-byte-quick, iget-char-quick, iget-short-quick, iget-wide-quick */
    /* op vA, vB, offset@CCCC */
    movl    rINST, %ecx                     # rcx <- BA
    sarl    $$4, %ecx                       # ecx <- B
    GET_VREG %ecx, %rcx                     # vB (object we're operating on)
    movzwq  2(rPC), %rax                    # eax <- field byte offset
    testl   %ecx, %ecx                      # is object null?
    je      common_errNullObject
    andb    $$0xf,rINSTbl                   # rINST <- A
    .if $wide
    movq (%rcx,%rax,1), %rax
    SET_WIDE_VREG %rax, rINSTq              # fp[A] <- value
    .else
    ${load} (%rcx,%rax,1), %eax
    SET_VREG %eax, rINSTq                   # fp[A] <- value
    .endif
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_iget_short():
   jmp NterpGetShortInstanceField

%def op_iget_short_quick():
%  op_iget_quick(load="movswl")

%def op_iget_wide():
   jmp NterpGetWideInstanceField

%def op_iget_wide_quick():
%  op_iget_quick(load="movq", wide="1")

%def op_instance_of():
   jmp NterpInstanceOf

%def op_iget():
   jmp NterpGetInstanceField

%def op_iput():
   jmp NterpPutInstanceField

%def op_iput_boolean():
   jmp NterpPutBooleanInstanceField

%def op_iput_boolean_quick():
%  op_iput_quick(reg="rINSTbl", store="movb")

%def op_iput_byte():
   jmp NterpPutByteInstanceField

%def op_iput_byte_quick():
%  op_iput_quick(reg="rINSTbl", store="movb")

%def op_iput_char():
   jmp NterpPutCharInstanceField

%def op_iput_char_quick():
%  op_iput_quick(reg="rINSTw", store="movw")

%def op_iput_object():
    jmp NterpPutObjectInstanceField

%def op_iput_object_quick():
   movzwq  2(rPC), %rax                    # eax <- field byte offset
   movzbq  rINSTbl, %rcx                   # rcx <- BA
   sarl    $$4, %ecx                       # ecx <- B
   GET_VREG %ecx, %rcx                     # vB (object we're operating on)
   testl   %ecx, %ecx                      # is object null?
   je      common_errNullObject
   andb    $$0xf, rINSTbl                  # rINST <- A
   GET_VREG rINST, rINSTq                  # rINST <- v[A]
   movl rINST, (%rcx,%rax,1)
   testl rINST, rINST
   je 1f
   movq rSELF:THREAD_CARD_TABLE_OFFSET, %rax
   shrq $$CARD_TABLE_CARD_SHIFT, %rcx
   movb %al, (%rcx, %rax, 1)
1:
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_iput_quick(reg="rINST", store="movl"):
    /* For: iput-quick, iput-object-quick */
    /* op vA, vB, offset@CCCC */
    movzbq  rINSTbl, %rcx                   # rcx <- BA
    sarl    $$4, %ecx                       # ecx <- B
    GET_VREG %ecx, %rcx                     # vB (object we're operating on)
    testl   %ecx, %ecx                      # is object null?
    je      common_errNullObject
    andb    $$0xf, rINSTbl                  # rINST <- A
    GET_VREG rINST, rINSTq                  # rINST <- v[A]
    movzwq  2(rPC), %rax                    # rax <- field byte offset
    ${store}    ${reg}, (%rcx,%rax,1)
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_iput_short():
   jmp NterpPutShortInstanceField

%def op_iput_short_quick():
%  op_iput_quick(reg="rINSTw", store="movw")

%def op_iput_wide():
   jmp NterpPutWideInstanceField

%def op_iput_wide_quick():
    /* iput-wide-quick vA, vB, offset@CCCC */
    movzbq    rINSTbl, %rcx                 # rcx<- BA
    sarl      $$4, %ecx                     # ecx<- B
    GET_VREG  %ecx, %rcx                    # vB (object we're operating on)
    testl     %ecx, %ecx                    # is object null?
    je        common_errNullObject
    movzwq    2(rPC), %rax                  # rax<- field byte offset
    leaq      (%rcx,%rax,1), %rcx           # ecx<- Address of 64-bit target
    andb      $$0xf, rINSTbl                # rINST<- A
    GET_WIDE_VREG %rax, rINSTq              # rax<- fp[A]/fp[A+1]
    movq      %rax, (%rcx)                  # obj.field<- r0/r1
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_sget(load="movl", wide="0"):
   jmp NterpGetIntStaticField

%def op_sget_boolean():
   jmp NterpGetBooleanStaticField

%def op_sget_byte():
   jmp NterpGetByteStaticField

%def op_sget_char():
   jmp NterpGetCharStaticField

%def op_sget_object():
   jmp NterpGetObjectStaticField

%def op_sget_short():
   jmp NterpGetShortStaticField

%def op_sget_wide():
   jmp NterpGetWideStaticField

%def op_sput():
   jmp NterpPutStaticField

%def op_sput_boolean():
   jmp NterpPutBooleanStaticField

%def op_sput_byte():
   jmp NterpPutByteStaticField

%def op_sput_char():
   jmp NterpPutCharStaticField

%def op_sput_object():
   jmp NterpPutObjectStaticField

%def op_sput_short():
   jmp NterpPutShortStaticField

%def op_sput_wide():
   jmp NterpPutWideStaticField

%def op_new_instance():
   // The routine is too big to fit in a handler, so jump to it.
   jmp NterpNewInstance
