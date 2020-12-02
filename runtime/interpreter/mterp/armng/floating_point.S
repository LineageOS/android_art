%def fbinop(instr=""):
    /*
     * Generic 32-bit floating-point operation.  Provide an "instr" line that
     * specifies an instruction that performs "s2 = s0 op s1".  Because we
     * use the "softfp" ABI, this must be an instruction, not a function call.
     *
     * For: add-float, sub-float, mul-float, div-float
     */
    /* floatop vAA, vBB, vCC */
    FETCH r0, 1                         @ r0<- CCBB
    mov     r4, rINST, lsr #8           @ r4<- AA
    mov     r3, r0, lsr #8              @ r3<- CC
    and     r2, r0, #255                @ r2<- BB
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vCC
    VREG_INDEX_TO_ADDR r2, r2           @ r2<- &vBB
    GET_VREG_FLOAT_BY_ADDR s1, r3       @ s1<- vCC
    GET_VREG_FLOAT_BY_ADDR s0, r2       @ s0<- vBB

    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    $instr                              @ s2<- op
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_FLOAT s2, r4, lr           @ vAA<- s2
    GOTO_OPCODE ip                      @ jump to next instruction

%def fbinop2addr(instr=""):
    /*
     * Generic 32-bit floating point "/2addr" binary operation.  Provide
     * an "instr" line that specifies an instruction that performs
     * "s2 = s0 op s1".
     *
     * For: add-float/2addr, sub-float/2addr, mul-float/2addr, div-float/2addr
     */
    /* binop/2addr vA, vB */
    mov     r3, rINST, lsr #12          @ r3<- B
    ubfx    r4, rINST, #8, #4           @ r4<- A
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vB
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &vA
    GET_VREG_FLOAT_BY_ADDR s1, r3       @ s1<- vB
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    GET_VREG_FLOAT_BY_ADDR s0, r4       @ s0<- vA
    $instr                              @ s2<- op
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_FLOAT_BY_ADDR s2, r4       @ vAA<- s2 No need to clear as it's 2addr
    GOTO_OPCODE ip                      @ jump to next instruction

%def fbinopWide(instr=""):
    /*
     * Generic 64-bit double-precision floating point binary operation.
     * Provide an "instr" line that specifies an instruction that performs
     * "d2 = d0 op d1".
     *
     * for: add-double, sub-double, mul-double, div-double
     */
    /* doubleop vAA, vBB, vCC */
    FETCH r0, 1                         @ r0<- CCBB
    mov     r4, rINST, lsr #8           @ r4<- AA
    mov     r3, r0, lsr #8              @ r3<- CC
    and     r2, r0, #255                @ r2<- BB
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vCC
    VREG_INDEX_TO_ADDR r2, r2           @ r2<- &vBB
    GET_VREG_DOUBLE_BY_ADDR d1, r3      @ d1<- vCC
    GET_VREG_DOUBLE_BY_ADDR d0, r2      @ d0<- vBB
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    $instr                              @ d2<- op
    CLEAR_SHADOW_PAIR r4, ip, lr        @ Zero shadow regs
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &vAA
    SET_VREG_DOUBLE_BY_ADDR d2, r4      @ vAA<- d2
    GOTO_OPCODE ip                      @ jump to next instruction

%def fbinopWide2addr(instr=""):
    /*
     * Generic 64-bit floating point "/2addr" binary operation.  Provide
     * an "instr" line that specifies an instruction that performs
     * "d2 = d0 op d1".
     *
     * For: add-double/2addr, sub-double/2addr, mul-double/2addr,
     *      div-double/2addr
     */
    /* binop/2addr vA, vB */
    mov     r3, rINST, lsr #12          @ r3<- B
    ubfx    r4, rINST, #8, #4           @ r4<- A
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vB
    CLEAR_SHADOW_PAIR r4, ip, r0        @ Zero out shadow regs
    GET_VREG_DOUBLE_BY_ADDR d1, r3      @ d1<- vB
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &vA
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    GET_VREG_DOUBLE_BY_ADDR d0, r4      @ d0<- vA
    $instr                              @ d2<- op
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_DOUBLE_BY_ADDR d2, r4      @ vAA<- d2
    GOTO_OPCODE ip                      @ jump to next instruction

%def funop(instr=""):
    /*
     * Generic 32-bit unary floating-point operation.  Provide an "instr"
     * line that specifies an instruction that performs "s1 = op s0".
     *
     * for: int-to-float, float-to-int
     */
    /* unop vA, vB */
    mov     r3, rINST, lsr #12          @ r3<- B
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vB
    GET_VREG_FLOAT_BY_ADDR s0, r3       @ s0<- vB
    ubfx    r4, rINST, #8, #4           @ r4<- A
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    $instr                              @ s1<- op
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_FLOAT s1, r4, lr           @ vA<- s1
    GOTO_OPCODE ip                      @ jump to next instruction

%def funopNarrower(instr=""):
    /*
     * Generic 64bit-to-32bit unary floating point operation.  Provide an
     * "instr" line that specifies an instruction that performs "s0 = op d0".
     *
     * For: double-to-int, double-to-float
     */
    /* unop vA, vB */
    mov     r3, rINST, lsr #12          @ r3<- B
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vB
    GET_VREG_DOUBLE_BY_ADDR d0, r3      @ d0<- vB
    ubfx    r4, rINST, #8, #4           @ r4<- A
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    $instr                              @ s0<- op
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_FLOAT s0, r4, lr           @ vA<- s0
    GOTO_OPCODE ip                      @ jump to next instruction

%def funopWider(instr=""):
    /*
     * Generic 32bit-to-64bit floating point unary operation.  Provide an
     * "instr" line that specifies an instruction that performs "d0 = op s0".
     *
     * For: int-to-double, float-to-double
     */
    /* unop vA, vB */
    mov     r3, rINST, lsr #12          @ r3<- B
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vB
    GET_VREG_FLOAT_BY_ADDR s0, r3       @ s0<- vB
    ubfx    r4, rINST, #8, #4           @ r4<- A
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST
    $instr                              @ d0<- op
    CLEAR_SHADOW_PAIR r4, ip, lr        @ Zero shadow regs
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &vA
    SET_VREG_DOUBLE_BY_ADDR d0, r4      @ vA<- d0
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_add_double():
%  fbinopWide(instr="faddd   d2, d0, d1")

%def op_add_double_2addr():
%  fbinopWide2addr(instr="faddd   d2, d0, d1")

%def op_add_float():
%  fbinop(instr="fadds   s2, s0, s1")

%def op_add_float_2addr():
%  fbinop2addr(instr="fadds   s2, s0, s1")

%def op_cmpg_double():
    /*
     * Compare two floating-point values.  Puts 0, 1, or -1 into the
     * destination register based on the results of the comparison.
     *
     * int compare(x, y) {
     *     if (x == y) {
     *         return 0;
     *     } else if (x < y) {
     *         return -1;
     *     } else if (x > y) {
     *         return 1;
     *     } else {
     *         return 1;
     *     }
     * }
     */
    /* op vAA, vBB, vCC */
    FETCH r0, 1                         @ r0<- CCBB
    mov     r4, rINST, lsr #8           @ r4<- AA
    and     r2, r0, #255                @ r2<- BB
    mov     r3, r0, lsr #8              @ r3<- CC
    VREG_INDEX_TO_ADDR r2, r2           @ r2<- &vBB
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vCC
    GET_VREG_DOUBLE_BY_ADDR d0, r2      @ d0<- vBB
    GET_VREG_DOUBLE_BY_ADDR d1, r3      @ d1<- vCC
    vcmpe.f64 d0, d1                    @ compare (vBB, vCC)
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    mvn     r0, #0                      @ r0<- -1 (default)
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    fmstat
    it      hi
    movhi   r0, #1                      @ (greater than, or unordered) r0<- 1
    moveq   r0, #0                      @ (equal) r0<- 0
    SET_VREG r0, r4                     @ vAA<- r0
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_cmpg_float():
    /*
     * Compare two floating-point values.  Puts 0, 1, or -1 into the
     * destination register based on the results of the comparison.
     *
     * int compare(x, y) {
     *     if (x == y) {
     *         return 0;
     *     } else if (x < y) {
     *         return -1;
     *     } else if (x > y) {
     *         return 1;
     *     } else {
     *         return 1;
     *     }
     * }
     */
    /* op vAA, vBB, vCC */
    FETCH r0, 1                         @ r0<- CCBB
    mov     r4, rINST, lsr #8           @ r4<- AA
    and     r2, r0, #255                @ r2<- BB
    mov     r3, r0, lsr #8              @ r3<- CC
    VREG_INDEX_TO_ADDR r2, r2           @ r2<- &vBB
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vCC
    GET_VREG_FLOAT_BY_ADDR s0, r2       @ s0<- vBB
    GET_VREG_FLOAT_BY_ADDR s1, r3       @ s1<- vCC
    vcmpe.f32 s0, s1                    @ compare (vBB, vCC)
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    mvn     r0, #0                      @ r0<- -1 (default)
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    fmstat
    it      hi
    movhi   r0, #1                      @ (greater than, or unordered) r0<- 1
    moveq   r0, #0                      @ (equal) r0<- 0
    SET_VREG r0, r4                     @ vAA<- r0
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_cmpl_double():
    /*
     * Compare two floating-point values.  Puts 0, 1, or -1 into the
     * destination register based on the results of the comparison.
     *
     * int compare(x, y) {
     *     if (x == y) {
     *         return 0;
     *     } else if (x > y) {
     *         return 1;
     *     } else if (x < y) {
     *         return -1;
     *     } else {
     *         return -1;
     *     }
     * }
     */
    /* op vAA, vBB, vCC */
    FETCH r0, 1                         @ r0<- CCBB
    mov     r4, rINST, lsr #8           @ r4<- AA
    and     r2, r0, #255                @ r2<- BB
    mov     r3, r0, lsr #8              @ r3<- CC
    VREG_INDEX_TO_ADDR r2, r2           @ r2<- &vBB
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vCC
    GET_VREG_DOUBLE_BY_ADDR d0, r2      @ d0<- vBB
    GET_VREG_DOUBLE_BY_ADDR d1, r3      @ d1<- vCC
    vcmpe.f64 d0, d1                    @ compare (vBB, vCC)
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    mvn     r0, #0                      @ r0<- -1 (default)
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    fmstat                              @ export status flags
    it gt
    movgt   r0, #1                      @ (greater than) r1<- 1
    it eq
    moveq   r0, #0                      @ (equal) r1<- 0
    SET_VREG r0, r4                     @ vAA<- r0
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_cmpl_float():
    /*
     * Compare two floating-point values.  Puts 0, 1, or -1 into the
     * destination register based on the results of the comparison.
     *
     * int compare(x, y) {
     *     if (x == y) {
     *         return 0;
     *     } else if (x > y) {
     *         return 1;
     *     } else if (x < y) {
     *         return -1;
     *     } else {
     *         return -1;
     *     }
     * }
     */
    /* op vAA, vBB, vCC */
    FETCH r0, 1                         @ r0<- CCBB
    mov     r4, rINST, lsr #8           @ r4<- AA
    and     r2, r0, #255                @ r2<- BB
    mov     r3, r0, lsr #8              @ r3<- CC
    VREG_INDEX_TO_ADDR r2, r2           @ r2<- &vBB
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &vCC
    GET_VREG_FLOAT_BY_ADDR s0, r2       @ s0<- vBB
    GET_VREG_FLOAT_BY_ADDR s1, r3       @ s1<- vCC
    vcmpe.f32  s0, s1                   @ compare (vBB, vCC)
    FETCH_ADVANCE_INST 2                @ advance rPC, load rINST
    mvn     r0, #0                      @ r0<- -1 (default)
    GET_INST_OPCODE ip                  @ extract opcode from rINST
    fmstat                              @ export status flags
    it gt
    movgt   r0, #1                      @ (greater than) r1<- 1
    it eq
    moveq   r0, #0                      @ (equal) r1<- 0
    SET_VREG r0, r4                     @ vAA<- r0
    GOTO_OPCODE ip                      @ jump to next instruction

%def op_div_double():
%  fbinopWide(instr="fdivd   d2, d0, d1")

%def op_div_double_2addr():
%  fbinopWide2addr(instr="fdivd   d2, d0, d1")

%def op_div_float():
%  fbinop(instr="fdivs   s2, s0, s1")

%def op_div_float_2addr():
%  fbinop2addr(instr="fdivs   s2, s0, s1")

%def op_double_to_float():
%  funopNarrower(instr="vcvt.f32.f64  s0, d0")

%def op_double_to_int():
%  funopNarrower(instr="vcvt.s32.f64  s0, d0")

%def op_double_to_long():
%  unopWide(instr="bl      nterp_d2l_doconv")

%def op_float_to_double():
%  funopWider(instr="vcvt.f64.f32  d0, s0")

%def op_float_to_int():
%  funop(instr="vcvt.s32.f32 s1, s0")

%def op_float_to_long():
%  unopWider(instr="bl      nterp_f2l_doconv")

%def op_int_to_double():
%  funopWider(instr="vcvt.f64.s32  d0, s0")

%def op_int_to_float():
%  funop(instr="vcvt.f32.s32  s1, s0")

%def op_long_to_double():
    /*
     * Specialised 64-bit floating point operation.
     *
     * Note: The result will be returned in d2.
     *
     * For: long-to-double
     */
    mov     r3, rINST, lsr #12          @ r3<- B
    ubfx    r4, rINST, #8, #4           @ r4<- A
    CLEAR_SHADOW_PAIR r4, ip, lr        @ Zero shadow regs
    VREG_INDEX_TO_ADDR r3, r3           @ r3<- &fp[B]
    VREG_INDEX_TO_ADDR r4, r4           @ r4<- &fp[A]
    GET_VREG_DOUBLE_BY_ADDR d0, r3      @ d0<- vBB
    FETCH_ADVANCE_INST 1                @ advance rPC, load rINST

    vcvt.f64.s32    d1, s1              @ d1<- (double)(vAAh)
    vcvt.f64.u32    d2, s0              @ d2<- (double)(vAAl)
    vldr            d3, constval$opcode
    vmla.f64        d2, d1, d3          @ d2<- vAAh*2^32 + vAAl

    GET_INST_OPCODE ip                  @ extract opcode from rINST
    SET_VREG_DOUBLE_BY_ADDR d2, r4      @ vAA<- d2
    GOTO_OPCODE ip                      @ jump to next instruction

    /* literal pool helper */
constval${opcode}:
    .8byte          0x41f0000000000000

%def op_long_to_float():
%  unopNarrower(instr="bl      __aeabi_l2f")

%def op_mul_double():
%  fbinopWide(instr="fmuld   d2, d0, d1")

%def op_mul_double_2addr():
%  fbinopWide2addr(instr="fmuld   d2, d0, d1")

%def op_mul_float():
%  fbinop(instr="fmuls   s2, s0, s1")

%def op_mul_float_2addr():
%  fbinop2addr(instr="fmuls   s2, s0, s1")

%def op_neg_double():
%  unopWide(instr="add     r1, r1, #0x80000000")

%def op_neg_float():
%  unop(instr="add     r0, r0, #0x80000000")

%def op_rem_double():
/* EABI doesn't define a double remainder function, but libm does */
%  binopWide(instr="bl      fmod")

%def op_rem_double_2addr():
/* EABI doesn't define a double remainder function, but libm does */
%  binopWide2addr(instr="bl      fmod")

%def op_rem_float():
/* EABI doesn't define a float remainder function, but libm does */
%  binop(instr="bl      fmodf")

%def op_rem_float_2addr():
/* EABI doesn't define a float remainder function, but libm does */
%  binop2addr(instr="bl      fmodf")

%def op_sub_double():
%  fbinopWide(instr="fsubd   d2, d0, d1")

%def op_sub_double_2addr():
%  fbinopWide2addr(instr="fsubd   d2, d0, d1")

%def op_sub_float():
%  fbinop(instr="fsubs   s2, s0, s1")

%def op_sub_float_2addr():
%  fbinop2addr(instr="fsubs   s2, s0, s1")
