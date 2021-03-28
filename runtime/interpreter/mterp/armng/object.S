%def op_check_cast():
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE r1, 3f
   cmp     rMR, #0
   bne     4f
1:
   lsr     r2, rINST, #8               // r2<- A
   GET_VREG r0, r2                     // r0<- vA (object)
   cmp     r0, #0
   beq     2f
   bl      art_quick_check_instance_of
2:
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   bl      nterp_get_class_or_allocate_object
   mov     r1, r0
   b       1b
4:
   bl      art_quick_read_barrier_mark_reg01
   b       1b

%def op_instance_of():
   /* instance-of vA, vB, class@CCCC */
   // Fast-path which gets the class from thread-local cache.
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE r1, 3f
   cmp     rMR, #0
   bne     4f
1:
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r0, r2                     // r0<- vB (object)
   cmp     r0, #0
   beq     2f
   bl      artInstanceOfFromCode
2:
   ubfx    r1, rINST, #8, #4           // r1<- A
   SET_VREG r0, r1
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
3:
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   bl      nterp_get_class_or_allocate_object
   mov     r1, r0
   b       1b
4:
   bl      art_quick_read_barrier_mark_reg01
   b       1b

%def op_iget_boolean():
%  op_iget(load="ldrb", wide="0", is_object="0")

%def op_iget_byte():
%  op_iget(load="ldrsb", wide="0", is_object="0")

%def op_iget_char():
%  op_iget(load="ldrh", wide="0", is_object="0")

%def op_iget_short():
%  op_iget(load="ldrsh", wide="0", is_object="0")

%def op_iget(load="ldr", wide="0", is_object="0"):
%  slow_path = add_helper(lambda: op_iget_slow_path(load, wide, is_object))
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, ${slow_path}
.L${opcode}_resume:
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r3, r2                     // r3<- object we're operating on
   ubfx    r2, rINST, #8, #4           // r2<- A
   cmp     r3, #0
   beq     common_errNullObject        // object was null
   .if $wide
   add     r3, r3, r0
   ldrd    r0, r1, [r3]
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2    // fp[A] <- value
   .elseif $is_object
   $load   r0, [r3, r0]
   cmp     rMR, #0
   bne     .L${opcode}_read_barrier
.L${opcode}_resume_after_read_barrier:
   SET_VREG_OBJECT r0, r2              // fp[A] <- value
   .else
   $load   r0, [r3, r0]
   SET_VREG r0, r2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
   .if $is_object
.L${opcode}_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_resume_after_read_barrier
   .endif

%def op_iget_slow_path(load, wide, is_object):
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   mov     r3, #0
   EXPORT_PC
   bl      nterp_get_instance_field_offset
   cmp     r0, #0
   bge     .L${opcode}_resume
   CLEAR_INSTANCE_VOLATILE_MARKER r0
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r3, r2                     // r3<- object we're operating on
   ubfx    r2, rINST, #8, #4           // r2<- A
   cmp     r3, #0
   beq     common_errNullObject            // object was null
   .if $wide
   add     ip, r3, r0
   ATOMIC_LOAD64 ip, r0, r1, r3, .L${opcode}_slow_path_atomic_load
   dmb     ish
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2    // fp[A] <- value
   .else
   $load   r0, [r3, r0]
   dmb     ish
   .if $is_object
   cmp     rMR, #0
   bne     .L${opcode}_read_barrier
   SET_VREG_OBJECT r0, r2              // fp[A] <- value
   .else
   SET_VREG r0, r2                     // fp[A] <- value
   .endif
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip

%def op_iget_wide():
%  op_iget(load="ldr", wide="1", is_object="0")

%def op_iget_object():
%  op_iget(load="ldr", wide="0", is_object="1")

%def op_iput_boolean():
%  op_iput(store="strb", wide="0", is_object="0")

%def op_iput_byte():
%  op_iput(store="strb", wide="0", is_object="0")

%def op_iput_char():
%  op_iput(store="strh", wide="0", is_object="0")

%def op_iput_short():
%  op_iput(store="strh", wide="0", is_object="0")

%def op_iput(store="str", wide="0", is_object="0"):
   // Share slow paths for boolean and byte (strb) and slow paths for char and short (strh).
   // It does not matter to which `.L${opcode}_resume` the slow path returns.
%  slow_path = "nterp_op_iput_helper_" + store + wide + is_object
%  add_helper(lambda: op_iput_slow_path(store, wide, is_object), slow_path)
   .if !$wide
   ubfx    r4, rINST, #8, #4           // r4<- A
   GET_VREG r4, r4                     // r4 <- v[A]
   .endif
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, ${slow_path}
.L${opcode}_resume:
   lsr     r1, rINST, #12              // r1<- B
   GET_VREG r1, r1                     // vB (object we're operating on)
   cmp     r1, #0
   beq     common_errNullObject
   .if $wide
   ubfx    r4, rINST, #8, #4           // r4<- A
   VREG_INDEX_TO_ADDR r4, r4
   GET_VREG_WIDE_BY_ADDR r2, r3, r4      // fp[A] <- value
   add     r1, r1, r0
   strd    r2, r3, [r1]
   .else
   $store  r4, [r1, r0]
   WRITE_BARRIER_IF_OBJECT $is_object, r4, r1, .L${opcode}_skip_write_barrier, r0
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip

%def op_iput_slow_path(store, wide, is_object):
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   .if $is_object
   mov     r3, r4
   .else
   mov     r3, #0
   .endif
   EXPORT_PC
   bl      nterp_get_instance_field_offset
   .if $is_object
   // Reload the value as it may have moved.
   ubfx    r4, rINST, #8, #4           // r4<- A
   GET_VREG r4, r4                     // r4 <- v[A]
   .endif
   cmp     r0, #0
   bge     .L${opcode}_resume
   CLEAR_INSTANCE_VOLATILE_MARKER r0
   .if $wide
   lsr     r4, rINST, #12              // r4<- B
   ubfx    r1, rINST, #8, #4           // r1<- A
   GET_VREG r4, r4                     // vB (object we're operating on)
   cmp     r4, #0
   beq     common_errNullObject
   VREG_INDEX_TO_ADDR r1, r1
   GET_VREG_WIDE_BY_ADDR r2, r3, r1
   add     ip, r4, r0
   dmb     ish
   ATOMIC_STORE64 ip, r2, r3, r0, r1, .L${opcode}_slow_path_atomic_store
   dmb     ish
   .else
   lsr     r1, rINST, #12              // r4<- B
   GET_VREG r1, r1                     // vB (object we're operating on)
   cmp     r1, #0
   beq     common_errNullObject
   dmb     ish
   $store  r4, [r1, r0]
   dmb     ish
   WRITE_BARRIER_IF_OBJECT $is_object, r4, r1, .L${opcode}_slow_path_skip_write_barrier, r0
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip

%def op_iput_wide():
%  op_iput(store="str", wide="1", is_object="0")

%def op_iput_object():
%  op_iput(store="str", wide="0", is_object="1")

%def op_sget_boolean():
%  op_sget(load="ldrb", wide="0", is_object="0")

%def op_sget_byte():
%  op_sget(load="ldrsb", wide="0", is_object="0")

%def op_sget_char():
%  op_sget(load="ldrh", wide="0", is_object="0")

%def op_sget_short():
%  op_sget(load="ldrsh", wide="0", is_object="0")

%def op_sget(load="ldr", wide="0", is_object="0"):
%  slow_path = add_helper(lambda: op_sget_slow_path(load, wide, is_object))
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, ${slow_path}
.L${opcode}_resume:
   ldr     r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   lsr     r2, rINST, #8               // r2 <- A
   ldr     r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp     rMR, #0
   bne     .L${opcode}_read_barrier
.L${opcode}_resume_after_read_barrier:
   .if $wide
   add     r0, r0, r1
   ldrd    r0, r1, [r0]
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2    // fp[A] <- value
   .elseif $is_object
   $load   r0, [r0, r1]
   // No need to check the marking register, we know it's not set here.
.L${opcode}_after_reference_load:
   SET_VREG_OBJECT r0, r2              // fp[A] <- value
   .else
   $load   r0, [r0, r1]
   SET_VREG r0, r2                     // fp[A] <- value
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   .if $is_object
   ldr     r0, [r0, r1]
.L${opcode}_mark_after_load:
   // Here, we know the marking register is set.
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_after_reference_load
   .else
   b       .L${opcode}_resume_after_read_barrier
   .endif

%def op_sget_slow_path(load="ldr", wide="0", is_object="0"):
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   mov     r3, #0
   EXPORT_PC
   bl      nterp_get_static_field
   tst     r0, #1
   beq     .L${opcode}_resume
   CLEAR_STATIC_VOLATILE_MARKER r0
   ldr     r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   lsr     r2, rINST, #8               // r2 <- A
   ldr     r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp     rMR, #0
   bne     .L${opcode}_slow_path_read_barrier
.L${opcode}_slow_path_resume_after_read_barrier:
   .if $wide
   add     ip, r0, r1
   ATOMIC_LOAD64 ip, r0, r1, r3, .L${opcode}_slow_path_atomic_load
   dmb     ish
   CLEAR_SHADOW_PAIR r2, ip, lr
   VREG_INDEX_TO_ADDR r2, r2
   SET_VREG_WIDE_BY_ADDR r0, r1, r2    // fp[A] <- value
   .else
   $load   r0, [r0, r1]
   dmb     ish
   .if $is_object
   cmp     rMR, #0
   bne     .L${opcode}_mark_after_load
   SET_VREG_OBJECT r0, r2              // fp[A] <- value
   .else
   SET_VREG r0, r2                     // fp[A] <- value
   .endif
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_slow_path_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_slow_path_resume_after_read_barrier

%def op_sget_wide():
%  op_sget(load="ldr", wide="1", is_object="0")

%def op_sget_object():
%  op_sget(load="ldr", wide="0", is_object="1")

%def op_sput_boolean():
%  op_sput(store="strb", wide="0", is_object="0")

%def op_sput_byte():
%  op_sput(store="strb", wide="0", is_object="0")

%def op_sput_char():
%  op_sput(store="strh", wide="0", is_object="0")

%def op_sput_short():
%  op_sput(store="strh", wide="0", is_object="0")

%def op_sput(store="str", wide="0", is_object="0"):
   // Share slow paths for boolean and byte (strb) and slow paths for char and short (strh).
   // It does not matter to which `.L${opcode}_resume` the slow path returns.
%  slow_path = "nterp_op_sput_helper_" + store + wide + is_object
%  add_helper(lambda: op_sput_slow_path(store, wide, is_object), slow_path)
   .if !$wide
   lsr     r4, rINST, #8               // r4 <- A
   GET_VREG r4, r4                     // r4 <- v[A]
   .endif
   // Fast-path which gets the field from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, ${slow_path}
.L${opcode}_resume:
   ldr     r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr     r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp     rMR, #0
   bne     .L${opcode}_read_barrier
.L${opcode}_resume_after_read_barrier:
   .if $wide
   lsr     r2, rINST, #8               // r2 <- A
   VREG_INDEX_TO_ADDR r2, r2
   GET_VREG_WIDE_BY_ADDR r2, r3, r2    // fp[A] <- value
   add     r0, r0, r1
   strd    r2, r3, [r0]
   .else
   $store  r4, [r0, r1]
   WRITE_BARRIER_IF_OBJECT $is_object, r4, r0, .L${opcode}_skip_write_barrier, r1
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_resume_after_read_barrier

%def op_sput_slow_path(store, wide, is_object):
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   .if $is_object
   mov     r3, r4
   .else
   mov     r3, #0
   .endif
   EXPORT_PC
   bl      nterp_get_static_field
   .if $is_object
   // Reload the value as it may have moved.
   lsr     r4, rINST, #8               // r4 <- A
   GET_VREG r4, r4                     // r4 <- v[A]
   .endif
   tst     r0, #1
   beq     .L${opcode}_resume
   CLEAR_STATIC_VOLATILE_MARKER r0
   ldr     r1, [r0, #ART_FIELD_OFFSET_OFFSET]
   ldr     r0, [r0, #ART_FIELD_DECLARING_CLASS_OFFSET]
   cmp     rMR, #0
   bne     .L${opcode}_slow_path_read_barrier
.L${opcode}_slow_path_resume_after_read_barrier:
   .if $wide
   lsr     r2, rINST, #8               // r2 <- A
   VREG_INDEX_TO_ADDR r2, r2
   GET_VREG_WIDE_BY_ADDR r2, r3, r2
   add     ip, r0, r1
   dmb     ish
   ATOMIC_STORE64 ip, r2, r3, r0, r1, .L${opcode}_slow_path_atomic_store
   dmb     ish
   .else
   dmb     ish
   $store  r4, [r0, r1]
   dmb     ish
   WRITE_BARRIER_IF_OBJECT $is_object, r4, r0, .L${opcode}_slow_path_skip_write_barrier, r1
   .endif
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
.L${opcode}_slow_path_read_barrier:
   bl      art_quick_read_barrier_mark_reg00
   b       .L${opcode}_slow_path_resume_after_read_barrier

%def op_sput_wide():
%  op_sput(store="str", wide="1", is_object="0")

%def op_sput_object():
%  op_sput(store="str", wide="0", is_object="1")

%def op_new_instance():
   // The routine is too big to fit in a handler, so jump to it.
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
   FETCH_FROM_THREAD_CACHE r0, 2f
   cmp     rMR, #0
   bne     3f
4:
   ldr     lr, [rSELF, #THREAD_ALLOC_OBJECT_ENTRYPOINT_OFFSET]
   blx     lr
1:
   lsr     r1, rINST, #8                    // r1 <- A
   SET_VREG_OBJECT r0, r1               // fp[A] <- value
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE ip
   GOTO_OPCODE ip
2:
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   bl      nterp_get_class_or_allocate_object
   b       1b
3:
   bl      art_quick_read_barrier_mark_reg00
   b       4b
