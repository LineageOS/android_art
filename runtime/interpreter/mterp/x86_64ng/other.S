%def unused():
    int3

%def op_const():
    /* const vAA, #+BBBBbbbb */
    movl    2(rPC), %eax                    # grab all 32 bits at once
    SET_VREG %eax, rINSTq                   # vAA<- eax
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 3

%def op_const_16():
    /* const/16 vAA, #+BBBB */
    movswl  2(rPC), %ecx                    # ecx <- ssssBBBB
    SET_VREG %ecx, rINSTq                   # vAA <- ssssBBBB
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_const_4():
    /* const/4 vA, #+B */
    movsbl  rINSTbl, %eax                   # eax <-ssssssBx
    andl    MACRO_LITERAL(0xf), rINST       # rINST <- A
    sarl    MACRO_LITERAL(4), %eax
    SET_VREG %eax, rINSTq
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_const_high16():
    /* const/high16 vAA, #+BBBB0000 */
    movzwl  2(rPC), %eax                    # eax <- 0000BBBB
    sall    MACRO_LITERAL(16), %eax         # eax <- BBBB0000
    SET_VREG %eax, rINSTq                   # vAA <- eax
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_const_object(jumbo="0", helper="nterp_load_object"):
   // Fast-path which gets the object from thread-local cache.
   FETCH_FROM_THREAD_CACHE %rax, 2f
   cmpq MACRO_LITERAL(0), rSELF:THREAD_READ_BARRIER_MARK_REG00_OFFSET
   jne 3f
1:
   SET_VREG_OBJECT %eax, rINSTq            # vAA <- value
   .if $jumbo
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 3
   .else
   ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
   .endif
2:
   EXPORT_PC
   movq rSELF:THREAD_SELF_OFFSET, %rdi
   movq 0(%rsp), %rsi
   movq rPC, %rdx
   call SYMBOL($helper)
   jmp 1b
3:
   // 00 is %rax
   call art_quick_read_barrier_mark_reg00
   jmp 1b

%def op_const_class():
%  op_const_object(jumbo="0", helper="nterp_get_class_or_allocate_object")

%def op_const_method_handle():
%  op_const_object(jumbo="0")

%def op_const_method_type():
%  op_const_object(jumbo="0")

%def op_const_string():
   /* const/string vAA, String@BBBB */
%  op_const_object(jumbo="0")

%def op_const_string_jumbo():
   /* const/string vAA, String@BBBBBBBB */
%  op_const_object(jumbo="1")

%def op_const_wide():
    /* const-wide vAA, #+HHHHhhhhBBBBbbbb */
    movq    2(rPC), %rax                    # rax <- HHHHhhhhBBBBbbbb
    SET_WIDE_VREG %rax, rINSTq
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 5

%def op_const_wide_16():
    /* const-wide/16 vAA, #+BBBB */
    movswq  2(rPC), %rax                    # rax <- ssssssssssssBBBB
    SET_WIDE_VREG %rax, rINSTq              # store
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_const_wide_32():
    /* const-wide/32 vAA, #+BBBBbbbb */
    movslq   2(rPC), %rax                   # eax <- ssssssssBBBBbbbb
    SET_WIDE_VREG %rax, rINSTq              # store
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 3

%def op_const_wide_high16():
    /* const-wide/high16 vAA, #+BBBB000000000000 */
    movzwq  2(rPC), %rax                    # eax <- 000000000000BBBB
    salq    $$48, %rax                      # eax <- 00000000BBBB0000
    SET_WIDE_VREG %rax, rINSTq              # v[AA+0] <- eax
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_monitor_enter():
/*
 * Synchronize on an object.
 */
    /* monitor-enter vAA */
    EXPORT_PC
    GET_VREG %edi, rINSTq
    call art_quick_lock_object
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_monitor_exit():
/*
 * Unlock an object.
 *
 * Exceptions that occur when unlocking a monitor need to appear as
 * if they happened at the following instruction.  See the Dalvik
 * instruction spec.
 */
    /* monitor-exit vAA */
    EXPORT_PC
    GET_VREG %edi, rINSTq
    call art_quick_unlock_object
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_move(is_object="0"):
    /* for move, move-object, long-to-int */
    /* op vA, vB */
    movl    rINST, %eax                     # eax <- BA
    andb    $$0xf, %al                      # eax <- A
    shrl    $$4, rINST                      # rINST <- B
    GET_VREG %edx, rINSTq
    .if $is_object
    SET_VREG_OBJECT %edx, %rax              # fp[A] <- fp[B]
    .else
    SET_VREG %edx, %rax                     # fp[A] <- fp[B]
    .endif
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_move_16(is_object="0"):
    /* for: move/16, move-object/16 */
    /* op vAAAA, vBBBB */
    movzwq  4(rPC), %rcx                    # ecx <- BBBB
    movzwq  2(rPC), %rax                    # eax <- AAAA
    GET_VREG %edx, %rcx
    .if $is_object
    SET_VREG_OBJECT %edx, %rax              # fp[A] <- fp[B]
    .else
    SET_VREG %edx, %rax                     # fp[A] <- fp[B]
    .endif
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 3

%def op_move_exception():
    /* move-exception vAA */
    movl    rSELF:THREAD_EXCEPTION_OFFSET, %eax
    SET_VREG_OBJECT %eax, rINSTq            # fp[AA] <- exception object
    movl    $$0, rSELF:THREAD_EXCEPTION_OFFSET
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_move_from16(is_object="0"):
    /* for: move/from16, move-object/from16 */
    /* op vAA, vBBBB */
    movzwq  2(rPC), %rax                    # eax <- BBBB
    GET_VREG %edx, %rax                     # edx <- fp[BBBB]
    .if $is_object
    SET_VREG_OBJECT %edx, rINSTq            # fp[A] <- fp[B]
    .else
    SET_VREG %edx, rINSTq                   # fp[A] <- fp[B]
    .endif
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_move_object():
%  op_move(is_object="1")

%def op_move_object_16():
%  op_move_16(is_object="1")

%def op_move_object_from16():
%  op_move_from16(is_object="1")

%def op_move_result(is_object="0"):
    /* for: move-result, move-result-object */
    /* op vAA */
    .if $is_object
    SET_VREG_OBJECT %eax, rINSTq            # fp[A] <- fp[B]
    .else
    SET_VREG %eax, rINSTq                   # fp[A] <- fp[B]
    .endif
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_move_result_object():
%  op_move_result(is_object="1")

%def op_move_result_wide():
    /* move-result-wide vAA */
    SET_WIDE_VREG %rax, rINSTq                   # v[AA] <- rdx
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_move_wide():
    /* move-wide vA, vB */
    /* NOTE: regs can overlap, e.g. "move v6,v7" or "move v7,v6" */
    movl    rINST, %ecx                     # ecx <- BA
    sarl    $$4, rINST                      # rINST <- B
    andb    $$0xf, %cl                      # ecx <- A
    GET_WIDE_VREG %rdx, rINSTq              # rdx <- v[B]
    SET_WIDE_VREG %rdx, %rcx                # v[A] <- rdx
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_move_wide_16():
    /* move-wide/16 vAAAA, vBBBB */
    /* NOTE: regs can overlap, e.g. "move v6,v7" or "move v7,v6" */
    movzwq  4(rPC), %rcx                    # ecx<- BBBB
    movzwq  2(rPC), %rax                    # eax<- AAAA
    GET_WIDE_VREG %rdx, %rcx                # rdx <- v[B]
    SET_WIDE_VREG %rdx, %rax                # v[A] <- rdx
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 3

%def op_move_wide_from16():
    /* move-wide/from16 vAA, vBBBB */
    /* NOTE: regs can overlap, e.g. "move v6,v7" or "move v7,v6" */
    movzwl  2(rPC), %ecx                    # ecx <- BBBB
    GET_WIDE_VREG %rdx, %rcx                # rdx <- v[B]
    SET_WIDE_VREG %rdx, rINSTq              # v[A] <- rdx
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_nop():
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_unused_3e():
%  unused()

%def op_unused_3f():
%  unused()

%def op_unused_40():
%  unused()

%def op_unused_41():
%  unused()

%def op_unused_42():
%  unused()

%def op_unused_43():
%  unused()

%def op_unused_79():
%  unused()

%def op_unused_7a():
%  unused()

%def op_unused_f3():
%  unused()

%def op_unused_f4():
%  unused()

%def op_unused_f5():
%  unused()

%def op_unused_f6():
%  unused()

%def op_unused_f7():
%  unused()

%def op_unused_f8():
%  unused()

%def op_unused_f9():
%  unused()

%def op_unused_fc():
%  unused()

%def op_unused_fd():
%  unused()
