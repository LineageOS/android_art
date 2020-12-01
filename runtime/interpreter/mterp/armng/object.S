%def op_check_cast():
   b NterpCheckCast

%def op_instance_of():
   b NterpInstanceOf

%def op_iget_boolean():
   b NterpGetBooleanInstanceField

%def op_iget_boolean_quick():
%  op_iget_quick(load="ldrb")

%def op_iget_byte():
   b NterpGetByteInstanceField

%def op_iget_byte_quick():
%  op_iget_quick(load="ldrsb")

%def op_iget_char():
   b NterpGetCharInstanceField

%def op_iget_char_quick():
%  op_iget_quick(load="ldrh")

%def op_iget_object():
   b NterpGetObjectInstanceField

%def op_iget_object_quick():
    /* For: iget-object-quick */
    /* op vA, vB, offset@CCCC */
    mov     r2, rINST, lsr #12          @ r2<- B
    FETCH r1, 1                         @ r1<- field byte offset
    EXPORT_PC
    GET_VREG r0, r2                     @ r0<- object we're operating on
    cmp r0, #0
    beq common_errNullObject
    ldr r0, [r0, r1]
    cmp rMR, #0
    bne 2f
1:
    ubfx    r2, rINST, #8, #4           @ r2<- A
    FETCH_ADVANCE_INST 2
    SET_VREG_OBJECT r0, r2              @ fp[A]<- r0
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction
2:
    bl art_quick_read_barrier_mark_reg00
    b 1b

%def op_iget():
   b NterpGetInstanceField

%def op_iget_quick(load="ldr", wide="0"):
    /* For: iget-quick, iget-boolean-quick, iget-byte-quick, iget-char-quick, iget-short-quick, iget-wide-quick*/
    /* op vA, vB, offset@CCCC */
    mov     r2, rINST, lsr #12          @ r2<- B
    FETCH r1, 1                         @ r1<- field byte offset
    GET_VREG r3, r2                     @ r3<- object we're operating on
    ubfx    r2, rINST, #8, #4           @ r2<- A
    cmp     r3, #0                      @ check object for null
    beq     common_errNullObject        @ object was null
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    .if $wide
    ldrd    r0, [r3, r1]                @ r0<- obj.field (64 bits, aligned)
    VREG_INDEX_TO_ADDR r3, r2           @ r3<- &fp[A]
    CLEAR_SHADOW_PAIR r2, ip, lr        @ Zero out the shadow regs
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_WIDE_BY_ADDR r0, r1, r3    @ fp[A]<- r0/r1
    .else
    $load   r0, [r3, r1]                @ r0<- obj.field
    SET_VREG r0, r2                     @ fp[A]<- r0
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    .endif
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_iget_short():
   b NterpGetShortInstanceField

%def op_iget_short_quick():
%  op_iget_quick(load="ldrsh")

%def op_iget_wide():
   b NterpGetWideInstanceField

%def op_iget_wide_quick():
%  op_iget_quick(load="ldr", wide="1")

%def op_iput_boolean():
   b NterpPutBooleanInstanceField

%def op_iput_boolean_quick():
%  op_iput_quick(store="strb")

%def op_iput_byte():
   b NterpPutByteInstanceField

%def op_iput_byte_quick():
%  op_iput_quick(store="strb")

%def op_iput_char():
   b NterpPutCharInstanceField

%def op_iput_char_quick():
%  op_iput_quick(store="strh")

%def op_iput_object():
   b NterpPutObjectInstanceField

%def op_iput_object_quick():
%  op_iput_quick(store="str", wide="0", is_object="1")

%def op_iput():
   b NterpPutInstanceField

%def op_iput_quick(store="str", wide="0", is_object="0"):
    /* For: iput-quick, iput-object-quick */
    /* op vA, vB, offset@CCCC */
    mov     r2, rINST, lsr #12          @ r2<- B
    FETCH ip, 1                         @ r1<- field byte offset
    GET_VREG r3, r2                     @ r3<- fp[B], the object pointer
    ubfx    r2, rINST, #8, #4           @ r2<- A
    cmp     r3, #0                      @ check object for null
    beq     common_errNullObject        @ object was null
    .if $wide
    VREG_INDEX_TO_ADDR r0, r2           @ r0<- &fp[A]
    GET_VREG_WIDE_BY_ADDR r0, r1, r0    @ r0/r1<- fp[A]/fp[A+1]
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    strd    r0, [r3, ip]                @ obj.field<- r0/r1
    .else
    GET_VREG r0, r2                     @ r0<- fp[A]
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    $store     r0, [r3, ip]             @ obj.field<- r0
    .endif
    .if $is_object
    cmp r0, #0
    beq 1f
    ldr r1, [rSELF, #THREAD_CARD_TABLE_OFFSET]
    lsr r0, r3, #CARD_TABLE_CARD_SHIFT
    strb r1, [r1, r0]
1:
   .endif
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_iput_short():
   b NterpPutShortInstanceField

%def op_iput_short_quick():
%  op_iput_quick(store="strh")

%def op_iput_wide():
   b NterpPutWideInstanceField

%def op_iput_wide_quick():
%  op_iput_quick(store="str", wide="1", is_object="0")

%def op_sget_boolean():
   b NterpGetBooleanStaticField

%def op_sget_byte():
   b NterpGetByteStaticField

%def op_sget_char():
   b NterpGetCharStaticField

%def op_sget_object():
   b NterpGetObjectStaticField

%def op_sget():
   b NterpGetIntStaticField

%def op_sget_short():
   b NterpGetShortStaticField

%def op_sget_wide():
   b NterpGetWideStaticField

%def op_sput_boolean():
   b NterpPutBooleanStaticField

%def op_sput_byte():
   b NterpPutByteStaticField

%def op_sput_char():
   b NterpPutCharStaticField

%def op_sput_object():
   b NterpPutObjectStaticField

%def op_sput():
   b NterpPutStaticField

%def op_sput_short():
   b NterpPutShortStaticField

%def op_sput_wide():
   b NterpPutWideStaticField

%def op_new_instance():
   // The routine is too big to fit in a handler, so jump to it.
   b NterpNewInstance
