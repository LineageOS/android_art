%def op_check_cast():
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE x1, 3f
   cbnz    wMR, 4f
1:
   lsr     w2, wINST, #8               // w2<- A
   GET_VREG w0, w2                     // w0<- vA (object)
   cbz     w0, 2f
   bl      art_quick_check_instance_of
2:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov     x0, xSELF
   ldr     x1, [sp]
   mov     x2, xPC
   bl      nterp_get_class_or_allocate_object
   mov     x1, x0
   b       1b
4:
   bl      art_quick_read_barrier_mark_reg01
   b       1b

%def op_instance_of():
   /* instance-of vA, vB, class@CCCC */
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE x1, 3f
   cbnz    wMR, 4f
1:
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w0, w2                     // w0<- vB (object)
   cbz     w0, 2f
   bl      artInstanceOfFromCode
2:
   ubfx    w1, wINST, #8, #4           // w1<- A
   SET_VREG w0, w1
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov     x0, xSELF
   ldr     x1, [sp]
   mov     x2, xPC
   bl      nterp_get_class_or_allocate_object
   mov     x1, x0
   b       1b
4:
   bl      art_quick_read_barrier_mark_reg01
   b       1b

%def op_iget_boolean():
%  op_iget(load="ldrb", volatile_load="ldarb", maybe_extend="", wide="0", is_object="0")

%def op_iget_byte():
%  op_iget(load="ldrsb", volatile_load="ldarb", maybe_extend="sxtb w0, w0", wide="0", is_object="0")

%def op_iget_char():
%  op_iget(load="ldrh", volatile_load="ldarh", maybe_extend="", wide="0", is_object="0")

%def op_iget_short():
%  op_iget(load="ldrsh", volatile_load="ldarh", maybe_extend="sxth w0, w0", wide="0", is_object="0")

%def op_iget(load="ldr", volatile_load="ldar", maybe_extend="", wide="0", is_object="0"):
%  slow_path = add_helper(lambda: op_iget_slow_path(volatile_load, maybe_extend, wide, is_object))
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, ${slow_path}
.L${opcode}_resume:
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w3, w2                     // w3<- object we're operating on
   ubfx    w2, wINST, #8, #4           // w2<- A
   cbz     w3, common_errNullObject    // object was null
   .if $wide
   $load   x0, [x3, x0]
   SET_VREG_WIDE x0, w2                // fp[A] <- value
   .elseif $is_object
   $load   w0, [x3, x0]
   cbnz    wMR, .L${opcode}_read_barrier
.L${opcode}_resume_after_read_barrier:
   SET_VREG_OBJECT w0, w2              // fp[A] <- value
   .else
   $load   w0, [x3, x0]
   SET_VREG w0, w2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
   .if $is_object
.L${opcode}_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_resume_after_read_barrier
   .endif

%def op_iget_slow_path(volatile_load, maybe_extend, wide, is_object):
   mov     x0, xSELF
   ldr     x1, [sp]
   mov     x2, xPC
   mov     x3, #0
   EXPORT_PC
   bl      nterp_get_instance_field_offset
   tbz     w0, #31, .L${opcode}_resume
   CLEAR_INSTANCE_VOLATILE_MARKER w0
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w3, w2                     // w3<- object we're operating on
   ubfx    w2, wINST, #8, #4           // w2<- A
   cbz     w3, common_errNullObject    // object was null
   add     x3, x3, x0
   .if $wide
   $volatile_load x0, [x3]
   SET_VREG_WIDE x0, w2                // fp[A] <- value
   .elseif $is_object
   $volatile_load w0, [x3]
   cbnz wMR, .L${opcode}_read_barrier
   SET_VREG_OBJECT w0, w2              // fp[A] <- value
   .else
   $volatile_load w0, [x3]
   $maybe_extend
   SET_VREG w0, w2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip

%def op_iget_wide():
%  op_iget(load="ldr", volatile_load="ldar", maybe_extend="", wide="1", is_object="0")

%def op_iget_object():
%  op_iget(load="ldr", volatile_load="ldar", maybe_extend="", wide="0", is_object="1")

%def op_iput_boolean():
%  op_iput(store="strb", volatile_store="stlrb", wide="0", is_object="0")

%def op_iput_byte():
%  op_iput(store="strb", volatile_store="stlrb", wide="0", is_object="0")

%def op_iput_char():
%  op_iput(store="strh", volatile_store="stlrh", wide="0", is_object="0")

%def op_iput_short():
%  op_iput(store="strh", volatile_store="stlrh", wide="0", is_object="0")

%def op_iput(store="str", volatile_store="stlr", wide="0", is_object="0"):
   // Share slow paths for boolean and byte (strb) and slow paths for char and short (strh).
   // It does not matter to which `.L${opcode}_resume` the slow path returns.
%  slow_path = "nterp_op_iput_helper_" + store + wide + is_object
%  add_helper(lambda: op_iput_slow_path(volatile_store, wide, is_object), slow_path)
   ubfx    w1, wINST, #8, #4           // w1<- A
   .if $wide
   GET_VREG_WIDE x26, w1               // x26<- fp[A]/fp[A+1]
   .else
   GET_VREG w26, w1                    // w26 <- v[A]
   .endif
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, ${slow_path}
.L${opcode}_resume:
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz w2, common_errNullObject
   .if $wide
   $store  x26, [x2, x0]
   .else
   $store  w26, [x2, x0]
   WRITE_BARRIER_IF_OBJECT $is_object, w26, w2, .L${opcode}_skip_write_barrier
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip

%def op_iput_slow_path(volatile_store, wide, is_object):
   mov     x0, xSELF
   ldr     x1, [sp]
   mov     x2, xPC
   .if $is_object
   mov     x3, x26
   .else
   mov     x3, #0
   .endif
   EXPORT_PC
   bl      nterp_get_instance_field_offset
   .if $is_object
   // Reload the value as it may have moved.
   ubfx    w1, wINST, #8, #4           // w1<- A
   GET_VREG w26, w1                    // w26 <- v[A]
   .endif
   tbz     w0, #31, .L${opcode}_resume
   CLEAR_INSTANCE_VOLATILE_MARKER w0
   lsr     w2, wINST, #12              // w2<- B
   GET_VREG w2, w2                     // vB (object we're operating on)
   cbz     w2, common_errNullObject
   add     x3, x2, x0
   .if $wide
   $volatile_store x26, [x3]
   .else
   $volatile_store w26, [x3]
   WRITE_BARRIER_IF_OBJECT $is_object, w26, w2, .L${opcode}_slow_path_skip_write_barrier
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip

%def op_iput_wide():
%  op_iput(store="str", volatile_store="stlr", wide="1", is_object="0")

%def op_iput_object():
%  op_iput(store="str", volatile_store="stlr", wide="0", is_object="1")

%def op_sget_boolean():
%  op_sget(load="ldrb", volatile_load="ldarb", maybe_extend="", wide="0", is_object="0")

%def op_sget_byte():
%  op_sget(load="ldrsb", volatile_load="ldarb", maybe_extend="sxtb w0, w0", wide="0", is_object="0")

%def op_sget_char():
%  op_sget(load="ldrh", volatile_load="ldarh", maybe_extend="", wide="0", is_object="0")

%def op_sget_short():
%  op_sget(load="ldrsh", volatile_load="ldarh", maybe_extend="sxth w0, w0", wide="0", is_object="0")

%def op_sget(load="ldr", volatile_load="ldar", maybe_extend="", wide="0", is_object="0"):
%  slow_path = add_helper(lambda: op_sget_slow_path(volatile_load, maybe_extend, wide, is_object))
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, ${slow_path}
.L${opcode}_resume:
   ldr     w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   lsr     w2, wINST, #8               // w2 <- A
   ldr     w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz    wMR, .L${opcode}_read_barrier
.L${opcode}_resume_after_read_barrier:
   .if $wide
   ldr     x0, [x0, x1]
   SET_VREG_WIDE x0, w2                // fp[A] <- value
   .elseif $is_object
   $load   w0, [x0, x1]
   // No need to check the marking register, we know it's not set here.
.L${opcode}_after_reference_load:
   SET_VREG_OBJECT w0, w2              // fp[A] <- value
   .else
   $load   w0, [x0, x1]
   SET_VREG w0, w2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   .if $is_object
   $load   w0, [x0, x1]
.L${opcode}_mark_after_load:
   // Here, we know the marking register is set.
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_after_reference_load
   .else
   b       .L${opcode}_resume_after_read_barrier
   .endif

%def op_sget_slow_path(volatile_load, maybe_extend, wide, is_object):
   mov     x0, xSELF
   ldr     x1, [sp]
   mov     x2, xPC
   mov     x3, #0
   EXPORT_PC
   bl      nterp_get_static_field
   tbz     x0, #0, .L${opcode}_resume
   CLEAR_STATIC_VOLATILE_MARKER x0
   ldr     w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   lsr     w2, wINST, #8               // w2 <- A
   ldr     w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz    wMR, .L${opcode}_slow_path_read_barrier
.L${opcode}_slow_path_resume_after_read_barrier:
   add     x0, x0, x1
   .if $wide
   ldar    x0, [x0]
   SET_VREG_WIDE x0, w2                // fp[A] <- value
   .elseif $is_object
   $volatile_load w0, [x0]
   cbnz    wMR, .L${opcode}_mark_after_load
   SET_VREG_OBJECT w0, w2              // fp[A] <- value
   .else
   $volatile_load w0, [x0]
   $maybe_extend
   SET_VREG w0, w2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_slow_path_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_slow_path_resume_after_read_barrier

%def op_sget_wide():
%  op_sget(load="ldr", volatile_load="ldar", maybe_extend="", wide="1", is_object="0")

%def op_sget_object():
%  op_sget(load="ldr", volatile_load="ldar", maybe_extend="", wide="0", is_object="1")

%def op_sput_boolean():
%  op_sput(store="strb", volatile_store="stlrb", wide="0", is_object="0")

%def op_sput_byte():
%  op_sput(store="strb", volatile_store="stlrb", wide="0", is_object="0")

%def op_sput_char():
%  op_sput(store="strh", volatile_store="stlrh", wide="0", is_object="0")

%def op_sput_short():
%  op_sput(store="strh", volatile_store="stlrh", wide="0", is_object="0")

%def op_sput(store="str", volatile_store="stlr", wide="0", is_object="0"):
   // Share slow paths for boolean and byte (strb) and slow paths for char and short (strh).
   // It does not matter to which `.L${opcode}_resume` the slow path returns.
%  slow_path = "nterp_op_sput_helper_" + store + wide + is_object
%  add_helper(lambda: op_sput_slow_path(volatile_store, wide, is_object), slow_path)
   lsr     w2, wINST, #8               // w2 <- A
   .if $wide
   GET_VREG_WIDE x26, w2               // x26 <- v[A]
   .else
   GET_VREG w26, w2                    // w26 <- v[A]
   .endif
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, ${slow_path}
.L${opcode}_resume:
   ldr     w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr     w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz    wMR, .L${opcode}_read_barrier
.L${opcode}_resume_after_read_barrier:
   .if $wide
   $store  x26, [x0, x1]
   .else
   $store  w26, [x0, x1]
   WRITE_BARRIER_IF_OBJECT $is_object, w26, w0, .L${opcode}_skip_write_barrier
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_resume_after_read_barrier

%def op_sput_slow_path(volatile_store, wide, is_object):
   mov     x0, xSELF
   ldr     x1, [sp]
   mov     x2, xPC
   .if $is_object
   mov     x3, x26
   .else
   mov     x3, #0
   .endif
   EXPORT_PC
   bl      nterp_get_static_field
   .if $is_object
   // Reload the value as it may have moved.
   lsr     w2, wINST, #8               // w2 <- A
   GET_VREG w26, w2                    // w26 <- v[A]
   .endif
   tbz     x0, #0, .L${opcode}_resume
   CLEAR_STATIC_VOLATILE_MARKER x0
   ldr     w1, [x0, #ART_FIELD_OFFSET_OFFSET]
   ldr     w0, [x0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cbnz    wMR, .L${opcode}_slow_path_read_barrier
.L${opcode}_slow_path_resume_after_read_barrier:
   add     x1, x0, x1
   .if $wide
   $volatile_store    x26, [x1]
   .else
   $volatile_store    w26, [x1]
   WRITE_BARRIER_IF_OBJECT $is_object, w26, w0, .L${opcode}_slow_path_skip_write_barrier
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_slow_path_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_slow_path_resume_after_read_barrier

%def op_sput_wide():
%  op_sput(store="str", volatile_store="stlr", wide="1", is_object="0")

%def op_sput_object():
%  op_sput(store="str", volatile_store="stlr", wide="0", is_object="1")

%def op_new_instance():
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
   FETCH_FROM_THREAD_CACHE x0, 2f
   cbnz    wMR, 3f
4:
   ldr     lr, [xSELF, #THREAD_ALLOC_OBJECT_ENTRYPOINT_OFFSET]
   blr     lr
1:
   lsr     w1, wINST, #8               // w1 <- A
   SET_VREG_OBJECT w0, w1              // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov     x0, xSELF
   ldr     x1, [sp]
   mov     x2, xPC
   bl      nterp_get_class_or_allocate_object
   b       1b
3:
   bl      art_quick_read_barrier_mark_reg00
   b       4b
