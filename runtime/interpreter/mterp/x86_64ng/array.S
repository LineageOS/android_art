%def op_aget(load="movl", shift="4", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0", is_object="0"):
/*
 * Array get.  vAA <- vBB[vCC].
 *
 * for: aget, aget-boolean, aget-byte, aget-char, aget-short, aget-wide, aget-object
 *
 */
    /* op vAA, vBB, vCC */
    movzbq  2(rPC), %rax                    # eax <- BB
    movzbq  3(rPC), %rcx                    # ecx <- CC
    GET_VREG %edi, %rax                     # eax <- vBB (array object)
    GET_VREG %esi, %rcx                     # ecx <- vCC (requested index)
    testl   %edi, %edi                      # null array object?
    je      common_errNullObject            # bail if so
    cmpl    MIRROR_ARRAY_LENGTH_OFFSET(%edi), %esi
    jae     common_errArrayIndex            # index >= length, bail.
    .if $wide
    movq    $data_offset(%rdi,%rsi,8), %rax
    SET_WIDE_VREG %rax, rINSTq
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
    .elseif $is_object
    testb $$READ_BARRIER_TEST_VALUE, GRAY_BYTE_OFFSET(%edi)
    $load   $data_offset(%rdi,%rsi,$shift), %eax
    jnz 2f
1:
    SET_VREG_OBJECT %eax, rINSTq
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
2:
    // reg00 is eax
    call art_quick_read_barrier_mark_reg00
    jmp 1b
    .else
    $load   $data_offset(%rdi,%rsi,$shift), %eax
    SET_VREG %eax, rINSTq
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2
    .endif

%def op_aget_boolean():
%  op_aget(load="movzbl", shift="1", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", is_object="0")

%def op_aget_byte():
%  op_aget(load="movsbl", shift="1", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", is_object="0")

%def op_aget_char():
%  op_aget(load="movzwl", shift="2", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", is_object="0")

%def op_aget_object():
%  op_aget(load="movl", shift="4", data_offset="MIRROR_OBJECT_ARRAY_DATA_OFFSET", is_object="1")

%def op_aget_short():
%  op_aget(load="movswl", shift="2", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", is_object="0")

%def op_aget_wide():
%  op_aget(load="movq", shift="8", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1", is_object="0")

%def op_aput(rINST_reg="rINST", store="movl", shift="4", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0"):
/*
 * Array put.  vBB[vCC] <- vAA.
 *
 * for: aput, aput-boolean, aput-byte, aput-char, aput-short, aput-wide
 *
 */
    /* op vAA, vBB, vCC */
    movzbq  2(rPC), %rax                    # rax <- BB
    movzbq  3(rPC), %rcx                    # rcx <- CC
    GET_VREG %edi, %rax                     # edi <- vBB (array object)
    GET_VREG %esi, %rcx                     # esi <- vCC (requested index)
    testl   %edi, %edi                      # null array object?
    je      common_errNullObject            # bail if so
    cmpl    MIRROR_ARRAY_LENGTH_OFFSET(%edi), %esi
    jae     common_errArrayIndex            # index >= length, bail.
    .if $wide
    GET_WIDE_VREG rINSTq, rINSTq
    .else
    GET_VREG rINST, rINSTq
    .endif
    $store    $rINST_reg, $data_offset(%rdi,%rsi,$shift)
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_aput_boolean():
%  op_aput(rINST_reg="rINSTbl", store="movb", shift="1", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", wide="0")

%def op_aput_byte():
%  op_aput(rINST_reg="rINSTbl", store="movb", shift="1", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", wide="0")

%def op_aput_char():
%  op_aput(rINST_reg="rINSTw", store="movw", shift="2", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", wide="0")

%def op_aput_short():
%  op_aput(rINST_reg="rINSTw", store="movw", shift="2", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", wide="0")

%def op_aput_wide():
%  op_aput(rINST_reg="rINSTq", store="movq", shift="8", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1")

%def op_aput_object():
    movzbq  2(rPC), %rax                    # rax <- BB
    movzbq  3(rPC), %rcx                    # rcx <- CC
    GET_VREG %edi, %rax                     # edi <- vBB (array object)
    GET_VREG %esi, %rcx                     # esi <- vCC (requested index)
    testl   %edi, %edi                      # null array object?
    je      common_errNullObject            # bail if so
    cmpl    MIRROR_ARRAY_LENGTH_OFFSET(%edi), %esi
    jae     common_errArrayIndex            # index >= length, bail.
    GET_VREG %edx, rINSTq
    call art_quick_aput_obj
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 2

%def op_array_length():
/*
 * Return the length of an array.
 */
    movl    rINST, %eax                     # eax <- BA
    sarl    $$4, rINST                      # rINST <- B
    GET_VREG %ecx, rINSTq                   # ecx <- vB (object ref)
    testl   %ecx, %ecx                      # is null?
    je      common_errNullObject
    andb    $$0xf, %al                      # eax <- A
    movl    MIRROR_ARRAY_LENGTH_OFFSET(%rcx), rINST
    SET_VREG rINST, %rax
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 1

%def op_fill_array_data():
    /* fill-array-data vAA, +BBBBBBBB */
    EXPORT_PC
    movslq  2(rPC), %rcx                    # rcx <- ssssssssBBBBbbbb
    leaq    (rPC,%rcx,2), OUT_ARG0          # OUT_ARG0 <- PC + ssssssssBBBBbbbb*2
    GET_VREG OUT_32_ARG1, rINSTq            # OUT_ARG1 <- vAA (array object)
    call    art_quick_handle_fill_data
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 3

%def op_filled_new_array(helper="nterp_filled_new_array"):
/*
 * Create a new array with elements filled from registers.
 *
 * for: filled-new-array, filled-new-array/range
 */
    /* op vB, {vD, vE, vF, vG, vA}, class@CCCC */
    /* op {vCCCC..v(CCCC+AA-1)}, type@BBBB */
    EXPORT_PC
    movq    rSELF:THREAD_SELF_OFFSET, OUT_ARG0
    movq    (%rsp), OUT_ARG1
    movq    rFP, OUT_ARG2
    movq    rPC, OUT_ARG3
    call    SYMBOL($helper)
    ADVANCE_PC_FETCH_AND_GOTO_NEXT 3

%def op_filled_new_array_range():
%  op_filled_new_array(helper="nterp_filled_new_array_range")

%def op_new_array():
  jmp NterpNewArray
