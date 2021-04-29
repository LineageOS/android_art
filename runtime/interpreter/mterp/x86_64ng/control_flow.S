%def bincmp(revcmp=""):
/*
 * Generic two-operand compare-and-branch operation.  Provide a "revcmp"
 * fragment that specifies the *reverse* comparison to perform, e.g.
 * for "if-le" you would use "gt".
 *
 * For: if-eq, if-ne, if-lt, if-ge, if-gt, if-le
 */
    /* if-cmp vA, vB, +CCCC */
    movl    rINST, %ecx                     # rcx <- A+
    sarl    $$4, rINST                      # rINST <- B
    andb    $$0xf, %cl                      # rcx <- A
    GET_VREG %eax, %rcx                     # eax <- vA
    cmpl    VREG_ADDRESS(rINSTq), %eax      # compare (vA, vB)
    j${revcmp}   1f
    movswq  2(rPC), rINSTq                  # Get signed branch offset
    BRANCH
1:
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def zcmp(revcmp=""):
/*
 * Generic one-operand compare-and-branch operation.  Provide a "revcmp"
 * fragment that specifies the *reverse* comparison to perform, e.g.
 * for "if-le" you would use "gt".
 *
 * for: if-eqz, if-nez, if-ltz, if-gez, if-gtz, if-lez
 */
    /* if-cmp vAA, +BBBB */
    cmpl    $$0, VREG_ADDRESS(rINSTq)       # compare (vA, 0)
    j${revcmp}   1f
    movswq  2(rPC), rINSTq                  # fetch signed displacement
    BRANCH
1:
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_goto():
/*
 * Unconditional branch, 8-bit offset.
 *
 * The branch distance is a signed code-unit offset, which we need to
 * double to get a byte offset.
 */
    /* goto +AA */
    movsbq  rINSTbl, rINSTq                 # rINSTq <- ssssssAA
    BRANCH

%def op_goto_16():
/*
 * Unconditional branch, 16-bit offset.
 *
 * The branch distance is a signed code-unit offset, which we need to
 * double to get a byte offset.
 */
    /* goto/16 +AAAA */
    movswq  2(rPC), rINSTq                  # rINSTq <- ssssAAAA
    BRANCH

%def op_goto_32():
/*
 * Unconditional branch, 32-bit offset.
 *
 * The branch distance is a signed code-unit offset, which we need to
 * double to get a byte offset.
 *
 * Because we need the SF bit set, we'll use an adds
 * to convert from Dalvik offset to byte offset.
 */
    /* goto/32 +AAAAAAAA */
    movslq  2(rPC), rINSTq                  # rINSTq <- AAAAAAAA
    BRANCH

%def op_if_eq():
%  bincmp(revcmp="ne")

%def op_if_eqz():
%  zcmp(revcmp="ne")

%def op_if_ge():
%  bincmp(revcmp="l")

%def op_if_gez():
%  zcmp(revcmp="l")

%def op_if_gt():
%  bincmp(revcmp="le")

%def op_if_gtz():
%  zcmp(revcmp="le")

%def op_if_le():
%  bincmp(revcmp="g")

%def op_if_lez():
%  zcmp(revcmp="g")

%def op_if_lt():
%  bincmp(revcmp="ge")

%def op_if_ltz():
%  zcmp(revcmp="ge")

%def op_if_ne():
%  bincmp(revcmp="e")

%def op_if_nez():
%  zcmp(revcmp="e")

%def op_packed_switch(func="NterpDoPackedSwitch"):
/*
 * Handle a packed-switch or sparse-switch instruction.  In both cases
 * we decode it and hand it off to a helper function.
 *
 * We don't really expect backward branches in a switch statement, but
 * they're perfectly legal, so we check for them here.
 *
 * for: packed-switch, sparse-switch
 */
    /* op vAA, +BBBB */
    movslq  2(rPC), OUT_ARG0                # rcx <- ssssssssBBBBbbbb
    leaq    (rPC,OUT_ARG0,2), OUT_ARG0      # rcx <- PC + ssssssssBBBBbbbb*2
    GET_VREG OUT_32_ARG1, rINSTq            # eax <- vAA
    call    SYMBOL($func)
    movslq  %eax, rINSTq
    BRANCH

/*
 * Return a 32-bit value.
 */
%def op_return(is_object="0"):
    GET_VREG %eax, rINSTq                   # eax <- vAA
    .if !$is_object
    // In case we're going back to compiled code, put the
    // result also in a xmm register.
    movd %eax, %xmm0
    .endif
    CFI_REMEMBER_STATE
    movq -8(rREFS), %rsp
    CFI_DEF_CFA(rsp, CALLEE_SAVES_SIZE)
    RESTORE_ALL_CALLEE_SAVES
    ret
    CFI_RESTORE_STATE

%def op_return_object():
%  op_return(is_object="1")

%def op_return_void():
    // Thread fence for constructor is a no-op on x86_64.
    CFI_REMEMBER_STATE
    movq -8(rREFS), %rsp
    CFI_DEF_CFA(rsp, CALLEE_SAVES_SIZE)
    RESTORE_ALL_CALLEE_SAVES
    ret
    CFI_RESTORE_STATE

%def op_return_wide():
    GET_WIDE_VREG %rax, rINSTq   # eax <- vAA
    // In case we're going back to compiled code, put the
    // result also in a xmm register.
    movq    %rax, %xmm0
    CFI_REMEMBER_STATE
    movq    -8(rREFS), %rsp
    CFI_DEF_CFA(rsp, CALLEE_SAVES_SIZE)
    RESTORE_ALL_CALLEE_SAVES
    ret
    CFI_RESTORE_STATE

%def op_sparse_switch():
%  op_packed_switch(func="NterpDoSparseSwitch")

%def op_throw():
  EXPORT_PC
  GET_VREG %edi, rINSTq                   # edi<- vAA (exception object)
  movq rSELF:THREAD_SELF_OFFSET, %rsi
  call SYMBOL(art_quick_deliver_exception)
  int3
