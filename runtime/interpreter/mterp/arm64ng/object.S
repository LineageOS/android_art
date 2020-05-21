%def op_check_cast():
   b NterpCheckCast

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
   /* op vA, vB, offset//CCCC */
   lsr     w2, wINST, #12              // w2<- B
   FETCH w1, 1                         // w1<- field byte offset
   GET_VREG w0, w2                     // w0<- object we're operating on
   cbz     x0, common_errNullObject    // bail if null object.
   ldr     w0, [x0, x1]
   cbnz wMR, 2f
1:
   ubfx    w2, wINST, #8, #4           // w2<- A
   PREFETCH_INST 2
   SET_VREG_OBJECT w0, w2              // fp[A]<- w0
   ADVANCE 2                           // advance rPC
   GET_INST_OPCODE ip                  // extract opcode from wINST
   GOTO_OPCODE ip                      // jump to next instruction
2:
   bl art_quick_read_barrier_mark_reg00
   b 1b

%def op_iget_quick(load="ldr", wide="0"):
   /* For: iget-quick, iget-boolean-quick, iget-byte-quick, iget-char-quick, iget-short-quick, iget-wide-quick */
   /* op vA, vB, offset@CCCC */
   lsr     w2, wINST, #12              // w2<- B
   FETCH w1, 1                         // w1<- field byte offset
   GET_VREG w0, w2                     // w0<- object we're operating on
   cbz     x0, common_errNullObject    // bail if null object.
   ubfx    w2, wINST, #8, #4           // w2<- A
   PREFETCH_INST 2
   .if $wide
   ${load} x0, [x0, x1]
   SET_VREG_WIDE x0, w2                // fp[A]<- x0
   .else
   ${load} w0, [x0, x1]
   SET_VREG w0, w2                     // fp[A]<- w0
   .endif
   ADVANCE 2                           // advance rPC
   GET_INST_OPCODE ip                  // extract opcode from wINST
   GOTO_OPCODE ip                      // jump to next instruction

%def op_iget_short():
   b NterpGetShortInstanceField

%def op_iget_short_quick():
%  op_iget_quick(load="ldrsh")

%def op_iget_wide():
   b NterpGetWideInstanceField

%def op_iget_wide_quick():
%  op_iget_quick(load="ldr", wide="1")

%def op_instance_of():
   b NterpInstanceOf

%def op_iget():
   b NterpGetInstanceField

%def op_iput():
   b NterpPutInstanceField

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

%def op_iput_quick(store="str", wide="0", is_object="0"):
   /* op vA, vB, offset@CCCC */
   lsr     w2, wINST, #12              // w2<- B
   FETCH w1, 1                         // w1<- field byte offset
   GET_VREG w3, w2                     // w3<- fp[B], the object pointer
   ubfx    w2, wINST, #8, #4           // w2<- A
   cbz     w3, common_errNullObject    // object was null
   .if $wide
   GET_VREG_WIDE x0, w2                // x0<- fp[A]
   FETCH_ADVANCE_INST 2                // advance rPC, load rINST
   $store     x0, [x3, x1]             // obj.field<- x0
   .else
   GET_VREG w0, w2                     // w0<- fp[A]
   FETCH_ADVANCE_INST 2                // advance rPC, load rINST
   $store     w0, [x3, x1]             // obj.field<- w0
   .endif
   .if $is_object
   cbz w0, 1f
   ldr x1, [xSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr w2, w3, #CARD_TABLE_CARD_SHIFT
   strb w1, [x1, x2]
1:
   .endif
   GET_INST_OPCODE ip                  // extract opcode from rINST
   GOTO_OPCODE ip                      // jump to next instruction

%def op_iput_object_quick():
%  op_iput_quick(store="str", wide="0", is_object="1")

%def op_iput_short():
   b NterpPutShortInstanceField

%def op_iput_short_quick():
%  op_iput_quick(store="strh")

%def op_iput_wide():
   b NterpPutWideInstanceField

%def op_iput_wide_quick():
%  op_iput_quick(store="str", wide="1", is_object="0")

%def op_sget(load="ldr", wide="0"):
   b NterpGetIntStaticField

%def op_sget_boolean():
   b NterpGetBooleanStaticField

%def op_sget_byte():
   b NterpGetByteStaticField

%def op_sget_char():
   b NterpGetCharStaticField

%def op_sget_object():
   b NterpGetObjectStaticField

%def op_sget_short():
   b NterpGetShortStaticField

%def op_sget_wide():
   b NterpGetWideStaticField

%def op_sput():
   b NterpPutStaticField

%def op_sput_boolean():
   b NterpPutBooleanStaticField

%def op_sput_byte():
   b NterpPutByteStaticField

%def op_sput_char():
   b NterpPutCharStaticField

%def op_sput_object():
   b NterpPutObjectStaticField

%def op_sput_short():
   b NterpPutShortStaticField

%def op_sput_wide():
   b NterpPutWideStaticField

%def op_new_instance():
   // The routine is too big to fit in a handler, so jump to it.
   b NterpNewInstance
