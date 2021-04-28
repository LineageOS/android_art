%def bincmp(condition=""):
    /*
     * Generic two-operand compare-and-branch operation.  Provide a "condition"
     * fragment that specifies the comparison to perform.
     *
     * For: if-eq, if-ne, if-lt, if-ge, if-gt, if-le
     */
    /* if-cmp vA, vB, +CCCC */
    lsr     w1, wINST, #12              // w1<- B
    ubfx    w0, wINST, #8, #4           // w0<- A
    GET_VREG w3, w1                     // w3<- vB
    GET_VREG w2, w0                     // w2<- vA
    cmp     w2, w3                      // compare (vA, vB)
    b.${condition} 1f
    FETCH_ADVANCE_INST 2
    GET_INST_OPCODE ip                  // extract opcode from wINST
    GOTO_OPCODE ip                      // jump to next instruction
1:
    FETCH_S wINST, 1                    // wINST<- branch offset, in code units
    BRANCH

%def zcmp(compare="1", branch=""):
    /*
     * Generic one-operand compare-and-branch operation.  Provide a "condition"
     * fragment that specifies the comparison to perform.
     *
     * for: if-eqz, if-nez, if-ltz, if-gez, if-gtz, if-lez
     */
    /* if-cmp vAA, +BBBB */
    lsr     w0, wINST, #8               // w0<- AA
    GET_VREG w2, w0                     // w2<- vAA
    .if ${compare}
    cmp     w2, #0                      // compare (vA, 0)
    .endif
    ${branch} 1f
    FETCH_ADVANCE_INST 2
    GET_INST_OPCODE ip                  // extract opcode from wINST
    GOTO_OPCODE ip                      // jump to next instruction
1:
    FETCH_S wINST, 1                    // w1<- branch offset, in code units
    BRANCH

%def op_goto():
/*
 * Unconditional branch, 8-bit offset.
 *
 * The branch distance is a signed code-unit offset, which we need to
 * double to get a byte offset.
 */
    /* goto +AA */
    sbfx    wINST, wINST, #8, #8           // wINST<- ssssssAA (sign-extended)
    BRANCH

%def op_goto_16():
/*
 * Unconditional branch, 16-bit offset.
 *
 * The branch distance is a signed code-unit offset, which we need to
 * double to get a byte offset.
 */
    /* goto/16 +AAAA */
    FETCH_S wINST, 1                    // wINST<- ssssAAAA (sign-extended)
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
    FETCH w0, 1                         // w0<- aaaa (lo)
    FETCH w1, 2                         // w1<- AAAA (hi)
    orr     wINST, w0, w1, lsl #16      // wINST<- AAAAaaaa
    BRANCH

%def op_if_eq():
%  bincmp(condition="eq")

%def op_if_eqz():
%  zcmp(compare="0", branch="cbz     w2,")

%def op_if_ge():
%  bincmp(condition="ge")

%def op_if_gez():
%  zcmp(compare="0", branch="tbz     w2, #31,")

%def op_if_gt():
%  bincmp(condition="gt")

%def op_if_gtz():
%  zcmp(branch="b.gt")

%def op_if_le():
%  bincmp(condition="le")

%def op_if_lez():
%  zcmp(branch="b.le")

%def op_if_lt():
%  bincmp(condition="lt")

%def op_if_ltz():
%  zcmp(compare="0", branch="tbnz    w2, #31,")

%def op_if_ne():
%  bincmp(condition="ne")

%def op_if_nez():
%  zcmp(compare="0", branch="cbnz    w2,")

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
    FETCH   w0, 1                       // x0<- 000000000000bbbb (lo)
    FETCH_S x1, 2                       // x1<- ssssssssssssBBBB (hi)
    lsr     w3, wINST, #8               // w3<- AA
    orr     x0, x0, x1, lsl #16         // x0<- ssssssssBBBBbbbb
    GET_VREG w1, w3                     // w1<- vAA
    add     x0, xPC, x0, lsl #1         // x0<- PC + ssssssssBBBBbbbb*2
    bl      $func                       // w0<- code-unit branch offset
    sxtw    xINST, w0
    BRANCH

%def op_sparse_switch():
%  op_packed_switch(func="NterpDoSparseSwitch")

/*
 * Return a 32-bit value.
 */
%def op_return(is_object="0", is_void="0", is_wide="0"):
    .if $is_void
      // Thread fence for constructor
      dmb ishst
    .else
      lsr     w2, wINST, #8               // w2<- AA
      .if $is_wide
        GET_VREG_WIDE x0, w2                // x0<- vAA
        // In case we're going back to compiled code, put the
        // result also in d0
        fmov d0, x0
      .else
        GET_VREG w0, w2                     // r0<- vAA
        .if !$is_object
        // In case we're going back to compiled code, put the
        // result also in s0.
        fmov s0, w0
        .endif
      .endif
    .endif
    .cfi_remember_state
    ldr ip, [xREFS, #-8]
    mov sp, ip
    .cfi_def_cfa sp, CALLEE_SAVES_SIZE
    RESTORE_ALL_CALLEE_SAVES
    ret
    .cfi_restore_state

%def op_return_object():
%  op_return(is_object="1", is_void="0", is_wide="0")

%def op_return_void():
%  op_return(is_object="0", is_void="1", is_wide="0")

%def op_return_wide():
%  op_return(is_object="0", is_void="0", is_wide="1")

%def op_throw():
  EXPORT_PC
  lsr      w2, wINST, #8               // r2<- AA
  GET_VREG w0, w2                      // r0<- vAA (exception object)
  mov x1, xSELF
  bl art_quick_deliver_exception
  brk 0
