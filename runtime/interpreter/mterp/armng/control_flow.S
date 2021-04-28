%def bincmp(condition=""):
    /*
     * Generic two-operand compare-and-branch operation.  Provide a "condition"
     * fragment that specifies the comparison to perform.
     *
     * For: if-eq, if-ne, if-lt, if-ge, if-gt, if-le
     */
    /* if-cmp vA, vB, +CCCC */
    mov     r1, rINST, lsr #12          @ r1<- B
    ubfx    r0, rINST, #8, #4           @ r0<- A
    GET_VREG r3, r1                     @ r3<- vB
    GET_VREG r0, r0                     @ r0<- vA
    FETCH_S rINST, 1                    @ rINST<- branch offset, in code units
    cmp     r0, r3                      @ compare (vA, vB)
    b${condition} 1f
    FETCH_ADVANCE_INST 2
    GET_INST_OPCODE ip                  // extract opcode from rINST
    GOTO_OPCODE ip                      // jump to next instruction
1:
    FETCH_S rINST, 1                    // rINST<- branch offset, in code units
    BRANCH

%def zcmp(condition=""):
    /*
     * Generic one-operand compare-and-branch operation.  Provide a "condition"
     * fragment that specifies the comparison to perform.
     *
     * for: if-eqz, if-nez, if-ltz, if-gez, if-gtz, if-lez
     */
    /* if-cmp vAA, +BBBB */
    mov     r0, rINST, lsr #8           @ r0<- AA
    GET_VREG r0, r0                     @ r0<- vAA
    FETCH_S rINST, 1                    @ rINST<- branch offset, in code units
    cmp     r0, #0                      // compare (vA, 0)
    b${condition} 1f
    FETCH_ADVANCE_INST 2
    GET_INST_OPCODE ip                  // extract opcode from rINST
    GOTO_OPCODE ip                      // jump to next instruction
1:
    FETCH_S rINST, 1                    // rINST<- branch offset, in code units
    BRANCH

%def op_goto():
/*
 * Unconditional branch, 8-bit offset.
 *
 * The branch distance is a signed code-unit offset, which we need to
 * double to get a byte offset.
 */
    /* goto +AA */
    sbfx    rINST, rINST, #8, #8           // rINST<- ssssssAA (sign-extended)
    BRANCH

%def op_goto_16():
/*
 * Unconditional branch, 16-bit offset.
 *
 * The branch distance is a signed code-unit offset, which we need to
 * double to get a byte offset.
 */
    /* goto/16 +AAAA */
    FETCH_S rINST, 1                    // wINST<- ssssAAAA (sign-extended)
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
    FETCH r0, 1                         // r0<- aaaa (lo)
    FETCH r1, 2                         // r1<- AAAA (hi)
    orrs     rINST, r0, r1, lsl #16      // wINST<- AAAAaaaa
    BRANCH

%def op_if_eq():
%  bincmp(condition="eq")

%def op_if_eqz():
%  zcmp(condition="eq")

%def op_if_ge():
%  bincmp(condition="ge")

%def op_if_gez():
%  zcmp(condition="ge")

%def op_if_gt():
%  bincmp(condition="gt")

%def op_if_gtz():
%  zcmp(condition="gt")

%def op_if_le():
%  bincmp(condition="le")

%def op_if_lez():
%  zcmp(condition="le")

%def op_if_lt():
%  bincmp(condition="lt")

%def op_if_ltz():
%  zcmp(condition="lt")

%def op_if_ne():
%  bincmp(condition="ne")

%def op_if_nez():
%  zcmp(condition="ne")

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
    FETCH r0, 1                         @ r0<- bbbb (lo)
    FETCH r1, 2                         @ r1<- BBBB (hi)
    mov     r3, rINST, lsr #8           @ r3<- AA
    orr     r0, r0, r1, lsl #16         @ r0<- BBBBbbbb
    GET_VREG r1, r3                     @ r1<- vAA
    add     r0, rPC, r0, lsl #1         @ r0<- PC + BBBBbbbb*2
    bl      $func                       @ r0<- code-unit branch offset
    mov     rINST, r0
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
      mov     r2, rINST, lsr #8           @ r2<- AA
      .if $is_wide
        VREG_INDEX_TO_ADDR r2, r2
        GET_VREG_WIDE_BY_ADDR r0, r1, r2 // r0,r1 <- vAA
        // In case we're going back to compiled code, put the
        // result also in d0.
        vmov d0, r0, r1
      .else
        GET_VREG r0, r2                     // r0<- vAA
        .if !$is_object
        // In case we're going back to compiled code, put the
        // result also in s0.
        vmov s0, r0
        .endif
      .endif
    .endif
    .cfi_remember_state
    ldr ip, [rREFS, #-4]
    mov sp, ip
    .cfi_def_cfa sp, CALLEE_SAVES_SIZE
    RESTORE_ALL_CALLEE_SAVES lr_to_pc=1
    .cfi_restore_state

%def op_return_object():
%  op_return(is_object="1", is_void="0", is_wide="0")

%def op_return_void():
%  op_return(is_object="0", is_void="1", is_wide="0")

%def op_return_wide():
%  op_return(is_object="0", is_void="0", is_wide="1")

%def op_throw():
  EXPORT_PC
  mov      r2, rINST, lsr #8           @ r2<- AA
  GET_VREG r0, r2                      @ r0<- vAA (exception object)
  mov r1, rSELF
  bl art_quick_deliver_exception
  bkpt 0
