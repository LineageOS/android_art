%def op_aget(load="ldr", shift="2", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0", is_object="0"):
/*
 * Array get.  vAA <- vBB[vCC].
 *
 * for: aget, aget-boolean, aget-byte, aget-char, aget-short, aget-wide, aget-object
 *
 */
    FETCH_B r2, 1, 0                    @ r2<- BB
    mov     r4, rINST, lsr #8           @ r4<- AA
    FETCH_B r3, 1, 1                    @ r3<- CC
    GET_VREG r0, r2                     @ r0<- vBB (array object)
    GET_VREG r1, r3                     @ r1<- vCC (requested index)
    cmp     r0, #0                      @ null array object?
    beq     common_errNullObject        @ yes, bail
    ldr     r3, [r0, #MIRROR_ARRAY_LENGTH_OFFSET]    @ r3<- arrayObj->length
    add     r0, r0, r1, lsl #$shift     @ r0<- arrayObj + index*width
    cmp     r1, r3                      @ compare unsigned index, length
    bcs     common_errArrayIndex        @ index >= length, bail
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    .if $wide
    CLEAR_SHADOW_PAIR r4, lr, ip        @ Zero out the shadow regs
    ldrd    r2, [r0, #$data_offset]     @ r2/r3<- vBB[vCC]
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &fp[AA]
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r2, r3, r4    @ vAA/vAA+1<- r2/r3
    GOTO_OPCODE ip                      @ jump to next instruction
    .elseif $is_object
    $load   r2, [r0, #$data_offset]     @ w2<- vBB[vCC]
    cmp rMR, #0
    bne 2f
1:
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_OBJECT r2, r4              @ vAA<- w2
    GOTO_OPCODE ip                      @ jump to next instruction
2:
    bl art_quick_read_barrier_mark_reg02
    b 1b
    .else
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    $load   r2, [r0, #$data_offset]     @ r2<- vBB[vCC]
    SET_VREG r2, r4                     @ vAA<- r2
    GOTO_OPCODE ip                      @ jump to next instruction
    .endif

%def op_aget_boolean():
%  op_aget(load="ldrb", shift="0", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_byte():
%  op_aget(load="ldrsb", shift="0", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_char():
%  op_aget(load="ldrh", shift="1", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_object():
%  op_aget(load="ldr", shift="2", data_offset="MIRROR_OBJECT_ARRAY_DATA_OFFSET", wide="0", is_object="1")

%def op_aget_short():
%  op_aget(load="ldrsh", shift="1", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_wide():
%  op_aget(load="ldrd", shift="3", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1", is_object="0")

%def op_aput(store="str", shift="2", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0", is_object="0"):
/*
 * Array put.  vBB[vCC] <- vAA.
 *
 * for: aput, aput-boolean, aput-byte, aput-char, aput-short, aput-wide, aput-object
 *
 */
    FETCH_B r2, 1, 0                    @ r2<- BB
    mov     r4, rINST, lsr #8           @ r4<- AA
    FETCH_B r3, 1, 1                    @ r3<- CC
    GET_VREG r0, r2                     @ r0<- vBB (array object)
    GET_VREG r1, r3                     @ r1<- vCC (requested index)
    cmp     r0, #0                      @ null array object?
    beq     common_errNullObject        @ yes, bail
    ldr     r3, [r0, #MIRROR_ARRAY_LENGTH_OFFSET]     @ r3<- arrayObj->length
    cmp     r1, r3                      @ compare unsigned index, length
    bcs     common_errArrayIndex        @ index >= length, bail
    .if $is_object
    EXPORT_PC                           // Export PC before overwriting it.
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    GET_VREG r2, r4                     @ r2<- vAA
    bl art_quick_aput_obj
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    .elseif $wide
    add     r0, r0, r1, lsl #$shift     @ r0<- arrayObj + index*width
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &fp[AA]
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    GET_VREG_WIDE_BY_ADDR r2, r3, r4    @ r2/r3<- vAA/vAA+1
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    strd    r2, [r0, #$data_offset]     @ r2/r3<- vBB[vCC]
    .else
    add     r0, r0, r1, lsl #$shift     @ r0<- arrayObj + index*width
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    GET_VREG r2, r4                     @ r2<- vAA
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    $store  r2, [r0, #$data_offset]     @ vBB[vCC]<- r2
    .endif
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_aput_boolean():
%  op_aput(store="strb", shift="0", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_byte():
%  op_aput(store="strb", shift="0", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_char():
%  op_aput(store="strh", shift="1", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_short():
%  op_aput(store="strh", shift="1", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_wide():
%  op_aput(store="str", shift="3", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1", is_object="0")

%def op_aput_object():
%  op_aput(store="str", shift="2", data_offset="MIRROR_OBJECT_ARRAY_DATA_OFFSET", wide="0", is_object="1")

%def op_array_length():
    /*
     * Return the length of an array.
     */
    mov     r1, rINST, lsr #12          @ r1<- B
    ubfx    r2, rINST, #8, #4           @ r2<- A
    GET_VREG r0, r1                     @ r0<- vB (object ref)
    cmp     r0, #0                      @ is object null?
    beq     common_errNullObject        @ yup, fail
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    ldr     r3, [r0, #MIRROR_ARRAY_LENGTH_OFFSET]    @ r3<- array length
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG r3, r2                     @ vB<- length
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_fill_array_data():
    /* fill-array-data vAA, +BBBBBBBB */
    EXPORT_PC
    FETCH r0, 1                         @ r0<- bbbb (lo)
    FETCH r1, 2                         @ r1<- BBBB (hi)
    mov     r3, rINST, lsr #8           @ r3<- AA
    orr     r0, r0, r1, lsl #16         @ r0<- BBBBbbbb
    GET_VREG r1, r3                     @ r1<- vAA (array object)
    add     r0, rPC, r0, lsl #1         @ r1<- PC + BBBBbbbb*2 (array data off.)
    bl      art_quick_handle_fill_data  @ (payload, object)
    FETCH_ADVANCE_INST 3                @ advance rPC, load rINST
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_filled_new_array(helper="nterp_filled_new_array"):
/*
 * Create a new array with elements filled from registers.
 *
 * for: filled-new-array, filled-new-array/range
 */
    /* op vB, {vD, vE, vF, vG, vA}, class@CCCC */
    /* op {vCCCC..v(CCCC+AA-1)}, type@BBBB */
    EXPORT_PC
    mov     r0, rSELF
    ldr     r1, [sp]
    mov     r2, rFP
    mov     r3, rPC
    bl      $helper
    FETCH_ADVANCE_INST 3                // advance rPC, load rINST
    GET_INST_OPCODE ip                  // extract opcode from rINST
    GOTO_OPCODE ip                      // jump to next instruction

%def op_filled_new_array_range():
%  op_filled_new_array(helper="nterp_filled_new_array_range")

%def op_new_array():
  b NterpNewArray
