%def unused():
    bkpt

%def op_const():
    /* const vAA, #+BBBBbbbb */
    mov     r3, rINST, lsr #8           @ r3<- AA
    FETCH r0, 1                         @ r0<- bbbb (low)
    FETCH r1, 2                         @ r1<- BBBB (high)
    FETCH_ADVANCE_INST 3                @ advance rPC, load rINST
    orr     r0, r0, r1, lsl #16         @ r0<- BBBBbbbb
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG r0, r3                     @ vAA<- r0
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_const_16():
    /* const/16 vAA, #+BBBB */
    FETCH_S r0, 1                       @ r0<- ssssBBBB (sign-extended)
    mov     r3, rINST, lsr #8           @ r3<- AA
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    SET_VREG r0, r3                     @ vAA<- r0
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_const_4():
    /* const/4 vA, #+B */
    sbfx    r1, rINST, #12, #4          @ r1<- sssssssB (sign-extended)
    ubfx    r0, rINST, #8, #4           @ r0<- A
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    GET_INST_OPCODE ip                  @ ip<- opcode from rINST
    SET_VREG r1, r0                     @ fp[A]<- r1
    GOTO_OPCODE ip                      @ execute next instruction

%def op_const_high16():
    /* const/high16 vAA, #+BBBB0000 */
    FETCH r0, 1                         @ r0<- 0000BBBB (zero-extended)
    mov     r3, rINST, lsr #8           @ r3<- AA
    mov     r0, r0, lsl #16             @ r0<- BBBB0000
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    SET_VREG r0, r3                     @ vAA<- r0
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_const_object(jumbo="0", helper="nterp_load_object"):
   // Fast-path which gets the object from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 2f
   cmp rMR, #0
   bne 3f
1:
   mov     r1, rINST, lsr #8           @ r1<- AA
   .if $jumbo
   FETCH_ADVANCE_INST 3                // advance rPC, load rINST
   .else
   FETCH_ADVANCE_INST 2                // advance rPC, load rINST
   .endif
   GET_INST_OPCODE ip                  // extract opcode from rINST
   SET_VREG_OBJECT r0, r1              // vAA <- value
   GOTO_OPCODE ip                      // jump to next instruction
2:
   EXPORT_PC
   mov r0, rSELF
   ldr r1, [sp]
   mov r2, rPC
   bl $helper
   b 1b
3:
   bl art_quick_read_barrier_mark_reg00
   b 1b

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
    FETCH r0, 1                         @ r0<- bbbb (low)
    FETCH r1, 2                         @ r1<- BBBB (low middle)
    FETCH r2, 3                         @ r2<- hhhh (high middle)
    orr     r0, r0, r1, lsl #16         @ r0<- BBBBbbbb (low word)
    FETCH r3, 4                         @ r3<- HHHH (high)
    mov     r4, rINST, lsr #8           @ r4<- AA
    orr     r1, r2, r3, lsl #16         @ r1<- HHHHhhhh (high word)
    CLEAR_SHADOW_PAIR r4, r2, r3        @ Zero out the shadow regs
    FETCH_ADVANCE_INST 5                @ advance rPC, load rINST
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &fp[AA]
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r4    @ vAA<- r0/r1
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_const_wide_16():
    /* const-wide/16 vAA, #+BBBB */
    FETCH_S r0, 1                       @ r0<- ssssBBBB (sign-extended)
    mov     r3, rINST, lsr #8           @ r3<- AA
    mov     r1, r0, asr #31             @ r1<- ssssssss
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    CLEAR_SHADOW_PAIR r3, r2, lr        @ Zero out the shadow regs
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &fp[AA]
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r3    @ vAA<- r0/r1
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_const_wide_32():
    /* const-wide/32 vAA, #+BBBBbbbb */
    FETCH r0, 1                         @ r0<- 0000bbbb (low)
    mov     r3, rINST, lsr #8           @ r3<- AA
    FETCH_S r2, 2                       @ r2<- ssssBBBB (high)
    FETCH_ADVANCE_INST 3                @ advance rPC, load rINST
    orr     r0, r0, r2, lsl #16         @ r0<- BBBBbbbb
    CLEAR_SHADOW_PAIR r3, r2, lr        @ Zero out the shadow regs
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &fp[AA]
    mov     r1, r0, asr #31             @ r1<- ssssssss
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r3    @ vAA<- r0/r1
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_const_wide_high16():
    /* const-wide/high16 vAA, #+BBBB000000000000 */
    FETCH r1, 1                         @ r1<- 0000BBBB (zero-extended)
    mov     r3, rINST, lsr #8           @ r3<- AA
    mov     r0, #0                      @ r0<- 00000000
    mov     r1, r1, lsl #16             @ r1<- BBBB0000
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    CLEAR_SHADOW_PAIR r3, r0, r2        @ Zero shadow regs
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &fp[AA]
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r3    @ vAA<- r0/r1
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_monitor_enter():
    /*
     * Synchronize on an object.
     */
    /* monitor-enter vAA */
    EXPORT_PC
    mov      r2, rINST, lsr #8           @ r2<- AA
    GET_VREG r0, r2                      @ r0<- vAA (object)
    bl       art_quick_lock_object
    FETCH_ADVANCE_INST 1
    GET_INST_OPCODE ip                   @ extract opcode from rINST
    GOTO_OPCODE ip                       @ jump to next instruction

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
    mov      r2, rINST, lsr #8          @ r2<- AA
    GET_VREG r0, r2                     @ r0<- vAA (object)
    bl       art_quick_unlock_object
    FETCH_ADVANCE_INST 1                @ before throw: advance rPC, load rINST
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move(is_object="0"):
    /* for move, move-object, long-to-int */
    /* op vA, vB */
    mov     r1, rINST, lsr #12          @ r1<- B from 15:12
    ubfx    r0, rINST, #8, #4           @ r0<- A from 11:8
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    GET_VREG r2, r1                     @ r2<- fp[B]
    GET_INST_OPCODE ip                  @ ip<- opcode from rINST
    .if $is_object
    SET_VREG_OBJECT r2, r0              @ fp[A]<- r2
    .else
    SET_VREG r2, r0                     @ fp[A]<- r2
    .endif
    GOTO_OPCODE ip                      @ execute next instruction

%def op_move_16(is_object="0"):
    /* for: move/16, move-object/16 */
    /* op vAAAA, vBBBB */
    FETCH r1, 2                         @ r1<- BBBB
    FETCH r0, 1                         @ r0<- AAAA
    FETCH_ADVANCE_INST 3                @ advance rPC, load rINST
    GET_VREG r2, r1                     @ r2<- fp[BBBB]
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    .if $is_object
    SET_VREG_OBJECT r2, r0              @ fp[AAAA]<- r2
    .else
    SET_VREG r2, r0                     @ fp[AAAA]<- r2
    .endif
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move_exception():
    /* move-exception vAA */
    mov     r2, rINST, lsr #8           @ r2<- AA
    ldr     r3, [rSELF, #THREAD_EXCEPTION_OFFSET]
    mov     r1, #0                      @ r1<- 0
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    SET_VREG_OBJECT r3, r2              @ fp[AA]<- exception obj
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    str     r1, [rSELF, #THREAD_EXCEPTION_OFFSET]  @ clear exception
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move_from16(is_object="0"):
    /* for: move/from16, move-object/from16 */
    /* op vAA, vBBBB */
    FETCH r1, 1                         @ r1<- BBBB
    mov     r0, rINST, lsr #8           @ r0<- AA
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    GET_VREG r2, r1                     @ r2<- fp[BBBB]
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    .if $is_object
    SET_VREG_OBJECT r2, r0              @ fp[AA]<- r2
    .else
    SET_VREG r2, r0                     @ fp[AA]<- r2
    .endif
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move_object():
%  op_move(is_object="1")

%def op_move_object_16():
%  op_move_16(is_object="1")

%def op_move_object_from16():
%  op_move_from16(is_object="1")

%def op_move_result(is_object="0"):
    /* for: move-result, move-result-object */
    /* op vAA */
    mov     r2, rINST, lsr #8           @ r2<- AA
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    .if $is_object
    SET_VREG_OBJECT r0, r2              @ fp[AA]<- r0
    .else
    SET_VREG r0, r2                     @ fp[AA]<- r0
    .endif
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move_result_object():
%  op_move_result(is_object="1")

%def op_move_result_wide():
    /* move-result-wide vAA */
    mov     rINST, rINST, lsr #8        @ rINST<- AA
    VREG_INDEX_TO_ADDR r2, rINST        @ r2<- &fp[AA]
    CLEAR_SHADOW_PAIR rINST, ip, lr     @ Zero out the shadow regs
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r2    @ fp[AA]<- r0/r1
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move_wide():
    /* move-wide vA, vB */
    /* NOTE: regs can overlap, e.g. "move v6,v7" or "move v7,v6" */
    mov     r3, rINST, lsr #12          @ r3<- B
    ubfx    rINST, rINST, #8, #4        @ rINST<- A
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &fp[B]
    VREG_INDEX_TO_ADDR r2, rINST        @ r2<- &fp[A]
    GET_VREG_WIDE_BY_ADDR r0, r1, r3    @ r0/r1<- fp[B]
    CLEAR_SHADOW_PAIR rINST, ip, lr     @ Zero out the shadow regs
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r2    @ fp[A]<- r0/r1
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move_wide_16():
    /* move-wide/16 vAAAA, vBBBB */
    /* NOTE: regs can overlap, e.g. "move v6,v7" or "move v7,v6" */
    FETCH r3, 2                         @ r3<- BBBB
    FETCH r2, 1                         @ r2<- AAAA
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &fp[BBBB]
    VREG_INDEX_TO_ADDR lr, r2           @ r2<- &fp[AAAA]
    GET_VREG_WIDE_BY_ADDR r0, r1, r3    @ r0/r1<- fp[BBBB]
    FETCH_ADVANCE_INST 3                @ advance rPC, load rINST
    CLEAR_SHADOW_PAIR r2, r3, ip        @ Zero out the shadow regs
    SET_VREG_WIDE_BY_ADDR r0, r1, lr    @ fp[AAAA]<- r0/r1
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_move_wide_from16():
    /* move-wide/from16 vAA, vBBBB */
    /* NOTE: regs can overlap, e.g. "move v6,v7" or "move v7,v6" */
    FETCH r3, 1                         @ r3<- BBBB
    mov     rINST, rINST, lsr #8        @ rINST<- AA
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &fp[BBBB]
    VREG_INDEX_TO_ADDR r2, rINST        @ r2<- &fp[AA]
    GET_VREG_WIDE_BY_ADDR r0, r1, r3    @ r0/r1<- fp[BBBB]
    CLEAR_SHADOW_PAIR rINST, ip, lr     @ Zero out the shadow regs
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r2    @ fp[AA]<- r0/r1
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_nop():
    FETCH_ADVANCE_INST 1                @ advance to next instr, load rINST
    GET_INST_OPCODE ip                  @ ip<- opcode from rINST
    GOTO_OPCODE ip                      @ execute it

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

%def op_unused_73():
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
