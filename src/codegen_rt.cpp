#include "codegen.h"

/*
 * Self-contained generic MIPS32 (MIPS I/II integer subset) runtime library.
 * Organized into independent modules so codegen emits only the routines
 * actually referenced by the compiled COBOL program (plus transitive deps).
 */

/* =========================================================================
 * OS Syscall Layer: Linux MIPS32 o32 (primary) & MARS/SPIM (isolated)
 * ========================================================================= */

static const char *const kLinuxExit = R"mips(
_rt_exit:
    li   $v0, 4001
    syscall
    jr   $ra
)mips";

static const char *const kLinuxWriteFd = R"mips(
# _rt_write_fd(a0=fd, a1=buf, a2=len) -> v0=bytes_written or -1
_rt_write_fd:
    li   $v0, 4004
    syscall
    beq  $a3, $zero, _rt_wfd_ok
    li   $v0, -1
_rt_wfd_ok:
    jr   $ra
)mips";

static const char *const kLinuxReadFd = R"mips(
# _rt_read_fd(a0=fd, a1=buf, a2=len) -> v0=bytes_read or -1
_rt_read_fd:
    li   $v0, 4003
    syscall
    beq  $a3, $zero, _rt_rfd_ok
    li   $v0, -1
_rt_rfd_ok:
    jr   $ra
)mips";

static const char *const kLinuxOpenClose = R"mips(
# _rt_open_file(a0=path_zstr, a1=mode: 0=IN, 1=OUT, 2=IO, 3=EXTEND) -> v0=fd or -1
# Linux MIPS o32 flags: O_RDONLY=0, O_WRONLY=1, O_RDWR=2, O_APPEND=8, O_CREAT=0x100, O_TRUNC=0x200
_rt_open_file:
    li   $t0, 0
    beq  $a1, $zero, _rt_open_go
    li   $t1, 1
    beq  $a1, $t1, _rt_open_out
    li   $t1, 2
    beq  $a1, $t1, _rt_open_io
    li   $t0, 0x109
    j    _rt_open_go
_rt_open_out:
    li   $t0, 0x301
    j    _rt_open_go
_rt_open_io:
    li   $t0, 0x102
_rt_open_go:
    move $a1, $t0
    li   $a2, 420
    li   $v0, 4005
    syscall
    beq  $a3, $zero, _rt_open_ok
    li   $v0, -1
_rt_open_ok:
    jr   $ra

_rt_close_file:
    bltz $a0, _rt_close_ret
    li   $v0, 4006
    syscall
_rt_close_ret:
    jr   $ra
)mips";

static const char *const kLinuxEpoch = R"mips(
_rt_get_epoch:
    move $a0, $zero
    li   $v0, 4013
    syscall
    beq  $a3, $zero, _rt_ep_ok
    li   $v0, 1795132800
_rt_ep_ok:
    jr   $ra
)mips";

/* --- Isolated MARS / SPIM OS Syscall Layer --- */

static const char *const kMarsExit = R"mips(
_rt_exit:
    li   $v0, 10
    syscall
    jr   $ra
)mips";

static const char *const kMarsWriteFd = R"mips(
_rt_write_fd:
    li   $t0, 1
    bne  $a0, $t0, _rt_mars_wfile
    move $t1, $a1
    move $t2, $a2
_rt_mars_wloop:
    blez $t2, _rt_mars_wdone
    lbu  $a0, 0($t1)
    li   $v0, 11
    syscall
    addiu $t1, $t1, 1
    addiu $t2, $t2, -1
    j    _rt_mars_wloop
_rt_mars_wdone:
    move $v0, $a2
    jr   $ra
_rt_mars_wfile:
    li   $v0, 15
    syscall
    jr   $ra
)mips";

static const char *const kMarsReadFd = R"mips(
_rt_mars_stdin_line:
    move $t6, $a0
    move $t7, $a1
    la   $a0, _rt_inbuf
    li   $a1, 255
    li   $v0, 8
    syscall
    la   $t0, _rt_inbuf
    li   $t1, 0
_rt_msl_copy:
    lbu  $t2, 0($t0)
    beq  $t2, $zero, _rt_msl_pad
    li   $t3, 10
    beq  $t2, $t3, _rt_msl_pad
    li   $t3, 13
    beq  $t2, $t3, _rt_msl_skip
    bge  $t1, $t7, _rt_msl_skip
    addu $t4, $t6, $t1
    sb   $t2, 0($t4)
    addiu $t1, $t1, 1
_rt_msl_skip:
    addiu $t0, $t0, 1
    j    _rt_msl_copy
_rt_msl_pad:
    bge  $t1, $t7, _rt_msl_done
    addu $t4, $t6, $t1
    li   $t2, 32
    sb   $t2, 0($t4)
    addiu $t1, $t1, 1
    j    _rt_msl_pad
_rt_msl_done:
    li   $v0, 1
    jr   $ra

_rt_read_fd:
    bne  $a0, $zero, _rt_mars_rfile
    li   $v0, 12
    syscall
    sb   $v0, 0($a1)
    li   $v0, 1
    jr   $ra
_rt_mars_rfile:
    li   $v0, 14
    syscall
    jr   $ra
)mips";

static const char *const kMarsOpenClose = R"mips(
_rt_open_file:
    li   $t0, 0
    beq  $a1, $zero, _rt_mopen_go
    li   $t1, 3
    beq  $a1, $t1, _rt_mopen_app
    li   $t0, 1
    j    _rt_mopen_go
_rt_mopen_app:
    li   $t0, 9
_rt_mopen_go:
    move $a1, $t0
    li   $a2, 0
    li   $v0, 13
    syscall
    jr   $ra

_rt_close_file:
    bltz $a0, _rt_mclose_ret
    li   $v0, 16
    syscall
_rt_mclose_ret:
    jr   $ra
)mips";

static const char *const kMarsEpoch = R"mips(
_rt_get_epoch:
    li   $v0, 1795132800
    jr   $ra
)mips";

/* =========================================================================
 * Target-Independent COBOL Runtime Modules
 * ========================================================================= */

static const char *const kRtPrint = R"mips(
_rt_print_bytes:
    addiu $sp, $sp, -8
    sw   $ra, 0($sp)
    move $a2, $a1
    move $a1, $a0
    li   $a0, 1
    jal  _rt_write_fd
    lw   $ra, 0($sp)
    addiu $sp, $sp, 8
    jr   $ra

_rt_print_nl:
    addiu $sp, $sp, -8
    sw   $ra, 0($sp)
    li   $a0, 1
    la   $a1, _rt_nl
    li   $a2, 1
    jal  _rt_write_fd
    lw   $ra, 0($sp)
    addiu $sp, $sp, 8
    jr   $ra
)mips";

static const char *const kRtPrintNum = R"mips(
_rt_print_num_field:
    addiu $sp, $sp, -24
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    sw   $s2, 12($sp)
    sw   $s3, 16($sp)
    move $s0, $a0
    move $s1, $a1
    move $s2, $a2
    move $s3, $a3
    blez $s2, _rt_pnf_plain
    subu $t0, $s1, $s2
    blez $t0, _rt_pnf_plain
    move $a0, $s0
    move $a1, $t0
    jal  _rt_print_bytes
    li   $t1, 46
    beq  $s3, $zero, _rt_pnf_sep
    li   $t1, 44
_rt_pnf_sep:
    la   $t2, _rt_chbuf
    sb   $t1, 0($t2)
    move $a0, $t2
    li   $a1, 1
    jal  _rt_print_bytes
    subu $t0, $s1, $s2
    addu $a0, $s0, $t0
    move $a1, $s2
    jal  _rt_print_bytes
    j    _rt_pnf_done
_rt_pnf_plain:
    move $a0, $s0
    move $a1, $s1
    jal  _rt_print_bytes
_rt_pnf_done:
    lw   $s3, 16($sp)
    lw   $s2, 12($sp)
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 24
    jr   $ra
)mips";

static const char *const kRtMem = R"mips(
_rt_memset:
    blez $a2, _rt_ms_done
    sb   $a1, 0($a0)
    addiu $a0, $a0, 1
    addiu $a2, $a2, -1
    j    _rt_memset
_rt_ms_done:
    jr   $ra

_rt_move_alnum:
    li   $t0, 0
_rt_ma_loop:
    bge  $t0, $a1, _rt_ma_done
    li   $t1, 32
    bge  $t0, $a3, _rt_ma_store
    addu $t2, $a2, $t0
    lbu  $t1, 0($t2)
_rt_ma_store:
    addu $t3, $a0, $t0
    sb   $t1, 0($t3)
    addiu $t0, $t0, 1
    j    _rt_ma_loop
_rt_ma_done:
    jr   $ra
)mips";

static const char *const kRtCmpAlnum = R"mips(
_rt_cmp_alnum:
    move $t0, $a1
    bge  $t0, $a3, _rt_ca_maxok
    move $t0, $a3
_rt_ca_maxok:
    li   $t1, 0
_rt_ca_loop:
    bge  $t1, $t0, _rt_ca_eq
    li   $t2, 32
    bge  $t1, $a1, _rt_ca_c2
    addu $t4, $a0, $t1
    lbu  $t2, 0($t4)
_rt_ca_c2:
    li   $t3, 32
    bge  $t1, $a3, _rt_ca_diff
    addu $t4, $a2, $t1
    lbu  $t3, 0($t4)
_rt_ca_diff:
    subu $v0, $t2, $t3
    bne  $v0, $zero, _rt_ca_ret
    addiu $t1, $t1, 1
    j    _rt_ca_loop
_rt_ca_eq:
    li   $v0, 0
_rt_ca_ret:
    jr   $ra
)mips";

static const char *const kRtCmpDigits = R"mips(
_rt_cmp_digits:
    move $t0, $a1
    bge  $t0, $a3, _rt_cd_maxok
    move $t0, $a3
_rt_cd_maxok:
    li   $t1, 0
_rt_cd_loop:
    bge  $t1, $t0, _rt_cd_eq
    subu $t5, $t0, $a1
    li   $t2, 48
    blt  $t1, $t5, _rt_cd_c2
    subu $t6, $t1, $t5
    addu $t4, $a0, $t6
    lbu  $t2, 0($t4)
    li   $t7, 32
    bne  $t2, $t7, _rt_cd_c2
    li   $t2, 48
_rt_cd_c2:
    subu $t5, $t0, $a3
    li   $t3, 48
    blt  $t1, $t5, _rt_cd_diff
    subu $t6, $t1, $t5
    addu $t4, $a2, $t6
    lbu  $t3, 0($t4)
    li   $t7, 32
    bne  $t3, $t7, _rt_cd_diff
    li   $t3, 48
_rt_cd_diff:
    subu $v0, $t2, $t3
    bne  $v0, $zero, _rt_cd_ret
    addiu $t1, $t1, 1
    j    _rt_cd_loop
_rt_cd_eq:
    li   $v0, 0
_rt_cd_ret:
    jr   $ra
)mips";

static const char *const kRtClass = R"mips(
_rt_is_numeric:
    blez $a1, _rt_in_no
    li   $t0, 0
_rt_in_loop:
    bge  $t0, $a1, _rt_in_yes
    addu $t1, $a0, $t0
    lbu  $t2, 0($t1)
    li   $t3, 48
    blt  $t2, $t3, _rt_in_no
    li   $t3, 57
    bgt  $t2, $t3, _rt_in_no
    addiu $t0, $t0, 1
    j    _rt_in_loop
_rt_in_yes:
    li   $v0, 1
    jr   $ra
_rt_in_no:
    li   $v0, 0
    jr   $ra

_rt_is_alpha:
    blez $a1, _rt_ia_no
    li   $t0, 0
_rt_ia_l:
    bge  $t0, $a1, _rt_ia_yes
    addu $t1, $a0, $t0
    lbu  $t2, 0($t1)
    li   $t3, 32
    beq  $t2, $t3, _rt_ia_ok
    li   $t3, 65
    blt  $t2, $t3, _rt_ia_no
    li   $t3, 90
    ble  $t2, $t3, _rt_ia_ok
    li   $t3, 97
    blt  $t2, $t3, _rt_ia_no
    li   $t3, 122
    bgt  $t2, $t3, _rt_ia_no
_rt_ia_ok:
    addiu $t0, $t0, 1
    j    _rt_ia_l
_rt_ia_yes:
    li   $v0, 1
    jr   $ra
_rt_ia_no:
    li   $v0, 0
    jr   $ra
)mips";

static const char *const kRtNum = R"mips(
_rt_parse_num:
    addiu $sp, $sp, -24
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    sw   $s2, 12($sp)
    sw   $s3, 16($sp)
    move $s0, $a0
    move $s1, $a1
    move $s2, $a2
    move $s3, $a3
    li   $t0, 0
    li   $t1, 0
    li   $t2, 0
    li   $t3, 0
    li   $t4, 0
_rt_pn_loop:
    bge  $t0, $s1, _rt_pn_scale
    addu $t5, $s0, $t0
    lbu  $t6, 0($t5)
    li   $t7, 45
    bne  $t6, $t7, _rt_pn_chkdot
    li   $t2, 1
    j    _rt_pn_next
_rt_pn_chkdot:
    li   $t7, 46
    beq  $t6, $t7, _rt_pn_dot
    li   $t7, 44
    bne  $t6, $t7, _rt_pn_chkdig
_rt_pn_dot:
    li   $t3, 1
    j    _rt_pn_next
_rt_pn_chkdig:
    li   $t7, 48
    blt  $t6, $t7, _rt_pn_next
    li   $t7, 57
    bgt  $t6, $t7, _rt_pn_next
    addiu $t6, $t6, -48
    li   $t7, 10
    mult $t1, $t7
    mflo $t1
    addu $t1, $t1, $t6
    beq  $t3, $zero, _rt_pn_next
    addiu $t4, $t4, 1
_rt_pn_next:
    addiu $t0, $t0, 1
    j    _rt_pn_loop
_rt_pn_scale:
    beq  $t3, $zero, _rt_pn_use_imp
    move $t5, $t4
    j    _rt_pn_adj
_rt_pn_use_imp:
    move $t5, $s2
_rt_pn_adj:
    subu $t6, $s3, $t5
    beq  $t6, $zero, _rt_pn_sign
    bltz $t6, _rt_pn_div
_rt_pn_mul:
    blez $t6, _rt_pn_sign
    li   $t7, 10
    mult $t1, $t7
    mflo $t1
    addiu $t6, $t6, -1
    j    _rt_pn_mul
_rt_pn_div:
    bgez $t6, _rt_pn_sign
    li   $t7, 10
    div  $t1, $t7
    mflo $t1
    addiu $t6, $t6, 1
    j    _rt_pn_div
_rt_pn_sign:
    beq  $t2, $zero, _rt_pn_ret
    subu $t1, $zero, $t1
_rt_pn_ret:
    move $v0, $t1
    lw   $s3, 16($sp)
    lw   $s2, 12($sp)
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 24
    jr   $ra

_rt_format_num:
    srl  $t0, $a3, 8
    andi $t1, $a3, 0xFE
    srl  $t1, $t1, 1
    andi $t2, $a3, 1
    move $t3, $a2
    li   $t4, 0
    bgez $t3, _rt_fn_pos
    li   $t4, 1
    subu $t3, $zero, $t3
_rt_fn_pos:
    subu $t5, $t1, $t0
_rt_fn_m:
    blez $t5, _rt_fn_d
    li   $t6, 10
    mult $t3, $t6
    mflo $t3
    addiu $t5, $t5, -1
    j    _rt_fn_m
_rt_fn_d:
    bgez $t5, _rt_fn_fill
    li   $t6, 10
    div  $t3, $t6
    mflo $t3
    addiu $t5, $t5, 1
    j    _rt_fn_d
_rt_fn_fill:
    move $t5, $a1
    li   $t7, 0
    beq  $t2, $zero, _rt_fn_digloop
    li   $t7, 1
    li   $t6, 43
    beq  $t4, $zero, _rt_fn_stsign
    li   $t6, 45
_rt_fn_stsign:
    sb   $t6, 0($a0)
_rt_fn_digloop:
    ble  $t5, $t7, _rt_fn_done
    addiu $t5, $t5, -1
    li   $t6, 10
    div  $t3, $t6
    mfhi $t6
    mflo $t3
    addiu $t6, $t6, 48
    addu $t0, $a0, $t5
    sb   $t6, 0($t0)
    j    _rt_fn_digloop
_rt_fn_done:
    jr   $ra
)mips";

static const char *const kRtEdited = R"mips(
_rt_format_edited:
    addiu $sp, $sp, -24
    sw   $s0, 0($sp)
    sw   $s1, 4($sp)
    sw   $s2, 8($sp)
    sw   $s3, 12($sp)
    sw   $s4, 16($sp)
    sw   $s5, 20($sp)
    move $s0, $a0
    move $s1, $a1
    srl  $t0, $a3, 8
    andi $t1, $a3, 0xFE
    srl  $t1, $t1, 1
    andi $s5, $a3, 1
    move $s2, $a2
    li   $s3, 0
    bgez $s2, _rt_fe_pos
    li   $s3, 1
    subu $s2, $zero, $s2
_rt_fe_pos:
    subu $t5, $t1, $t0
_rt_fe_m:
    blez $t5, _rt_fe_d
    li   $t6, 10
    mult $s2, $t6
    mflo $s2
    addiu $t5, $t5, -1
    j    _rt_fe_m
_rt_fe_d:
    bgez $t5, _rt_fe_len
    li   $t6, 10
    div  $s2, $t6
    mflo $s2
    addiu $t5, $t5, 1
    j    _rt_fe_d
_rt_fe_len:
    li   $s4, 0
_rt_fe_ll:
    addu $t0, $s1, $s4
    lbu  $t1, 0($t0)
    beq  $t1, $zero, _rt_fe_rfill
    addiu $s4, $s4, 1
    j    _rt_fe_ll
_rt_fe_rfill:
    move $t0, $s4
_rt_fe_rl:
    blez $t0, _rt_fe_suppress
    addiu $t0, $t0, -1
    addu $t1, $s1, $t0
    lbu  $t2, 0($t1)
    li   $t3, 57
    beq  $t2, $t3, _rt_fe_dig
    li   $t3, 90
    beq  $t2, $t3, _rt_fe_dig
    li   $t3, 122
    beq  $t2, $t3, _rt_fe_dig
    li   $t3, 45
    bne  $t2, $t3, _rt_fe_lit
    li   $t4, 32
    beq  $s3, $zero, _rt_fe_st
    li   $t4, 45
    j    _rt_fe_st
_rt_fe_dig:
    li   $t3, 10
    div  $s2, $t3
    mfhi $t4
    mflo $s2
    addiu $t4, $t4, 48
    j    _rt_fe_st
_rt_fe_lit:
    move $t4, $t2
_rt_fe_st:
    addu $t5, $s0, $t0
    sb   $t4, 0($t5)
    j    _rt_fe_rl
_rt_fe_suppress:
    li   $t0, 0
    li   $t7, 46
    beq  $s5, $zero, _rt_fe_sl
    li   $t7, 44
_rt_fe_sl:
    bge  $t0, $s4, _rt_fe_done
    addu $t1, $s1, $t0
    lbu  $t2, 0($t1)
    beq  $t2, $t7, _rt_fe_done
    li   $t3, 57
    beq  $t2, $t3, _rt_fe_done
    addu $t4, $s0, $t0
    lbu  $t5, 0($t4)
    li   $t3, 90
    beq  $t2, $t3, _rt_fe_chkz
    li   $t3, 122
    beq  $t2, $t3, _rt_fe_chkz
    li   $t3, 45
    beq  $t2, $t3, _rt_fe_snext
    li   $t6, 32
    sb   $t6, 0($t4)
    j    _rt_fe_snext
_rt_fe_chkz:
    li   $t3, 48
    bne  $t5, $t3, _rt_fe_done
    li   $t6, 32
    sb   $t6, 0($t4)
_rt_fe_snext:
    addiu $t0, $t0, 1
    j    _rt_fe_sl
_rt_fe_done:
    lw   $s5, 20($sp)
    lw   $s4, 16($sp)
    lw   $s3, 12($sp)
    lw   $s2, 8($sp)
    lw   $s1, 4($sp)
    lw   $s0, 0($sp)
    addiu $sp, $sp, 24
    jr   $ra
)mips";

static const char *const kRtArith = R"mips(
_rt_mul_scaled:
    li   $t0, 1
    bgt  $a2, $t0, _rt_ms_sc
    mult $a0, $a1
    mflo $v0
    jr   $ra
_rt_ms_sc:
    div  $a0, $a2
    mflo $t0
    mfhi $t1
    div  $a1, $a2
    mflo $t2
    mfhi $t3
    mult $t0, $a1
    mflo $v0
    mult $t1, $t2
    mflo $t4
    addu $v0, $v0, $t4
    mult $t1, $t3
    mflo $t4
    div  $t4, $a2
    mflo $t4
    addu $v0, $v0, $t4
    jr   $ra

_rt_div_scaled:
    beq  $a1, $zero, _rt_ds_zero
    div  $a0, $a1
    mflo $t0
    mfhi $t1
    mult $t0, $a2
    mflo $v0
    move $t2, $a1
_rt_ds_shrink:
    bgez $t1, _rt_ds_absok
    subu $t3, $zero, $t1
    j    _rt_ds_chk
_rt_ds_absok:
    move $t3, $t1
_rt_ds_chk:
    li   $t4, 200000
    blt  $t3, $t4, _rt_ds_rem
    li   $t4, 10
    div  $t1, $t4
    mflo $t1
    div  $t2, $t4
    mflo $t2
    bne  $t2, $zero, _rt_ds_shrink
    jr   $ra
_rt_ds_rem:
    mult $t1, $a2
    mflo $t3
    div  $t3, $t2
    mflo $t3
    addu $v0, $v0, $t3
    jr   $ra
_rt_ds_zero:
    li   $v0, 0
    jr   $ra

_rt_pow_scaled:
    addiu $sp, $sp, -16
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    sw   $s2, 12($sp)
    move $s0, $a0
    move $s2, $a2
    div  $a1, $s2
    mflo $s1
    move $v0, $s2
_rt_pow_l:
    blez $s1, _rt_pow_d
    move $a0, $v0
    move $a1, $s0
    move $a2, $s2
    jal  _rt_mul_scaled
    addiu $s1, $s1, -1
    j    _rt_pow_l
_rt_pow_d:
    lw   $s2, 12($sp)
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 16
    jr   $ra
)mips";

static const char *const kRtLineIoHead = R"mips(
_rt_read_line_fd:
)mips";

static const char *const kRtLineIoMarsHook = R"mips(    bne  $a0, $zero, _rt_rl_file
    move $a0, $a1
    move $a1, $a2
    j    _rt_mars_stdin_line
_rt_rl_file:
)mips";

static const char *const kRtLineIoBody = R"mips(    addiu $sp, $sp, -24
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    sw   $s2, 12($sp)
    sw   $s3, 16($sp)
    sw   $s4, 20($sp)
    move $s0, $a0
    move $s1, $a1
    move $s2, $a2
    li   $s3, 0
    li   $s4, 0
    bltz $s0, _rt_rl_eof
_rt_rl_loop:
    move $a0, $s0
    la   $a1, _rt_chbuf
    li   $a2, 1
    jal  _rt_read_fd
    blez $v0, _rt_rl_end
    li   $s4, 1
    la   $t0, _rt_chbuf
    lbu  $t1, 0($t0)
    li   $t2, 10
    beq  $t1, $t2, _rt_rl_end
    li   $t2, 13
    beq  $t1, $t2, _rt_rl_loop
    bge  $s3, $s2, _rt_rl_loop
    addu $t3, $s1, $s3
    sb   $t1, 0($t3)
    addiu $s3, $s3, 1
    j    _rt_rl_loop
_rt_rl_end:
    beq  $s4, $zero, _rt_rl_eof
_rt_rl_pad:
    bge  $s3, $s2, _rt_rl_ok
    addu $t3, $s1, $s3
    li   $t1, 32
    sb   $t1, 0($t3)
    addiu $s3, $s3, 1
    j    _rt_rl_pad
_rt_rl_ok:
    li   $v0, 1
    j    _rt_rl_ret
_rt_rl_eof:
    li   $v0, 0
_rt_rl_ret:
    lw   $s4, 20($sp)
    lw   $s3, 16($sp)
    lw   $s2, 12($sp)
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 24
    jr   $ra

_rt_write_line_fd:
    addiu $sp, $sp, -16
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    move $s0, $a0
    bltz $s0, _rt_wl_ret
    jal  _rt_write_fd
    move $a0, $s0
    la   $a1, _rt_nl
    li   $a2, 1
    jal  _rt_write_fd
_rt_wl_ret:
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 16
    jr   $ra

_rt_trim_zstr:
    move $t0, $a2
_rt_tz_tr:
    blez $t0, _rt_tz_cp
    addiu $t1, $t0, -1
    addu $t2, $a1, $t1
    lbu  $t3, 0($t2)
    li   $t4, 32
    bne  $t3, $t4, _rt_tz_cp
    move $t0, $t1
    j    _rt_tz_tr
_rt_tz_cp:
    li   $t1, 0
_rt_tz_cl:
    bge  $t1, $t0, _rt_tz_nul
    addu $t2, $a1, $t1
    lbu  $t3, 0($t2)
    addu $t2, $a0, $t1
    sb   $t3, 0($t2)
    addiu $t1, $t1, 1
    j    _rt_tz_cl
_rt_tz_nul:
    addu $t2, $a0, $t0
    sb   $zero, 0($t2)
    jr   $ra
)mips";

static const char *const kRtAccept = R"mips(
_rt_accept_str:
    addiu $sp, $sp, -16
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    move $s0, $a0
    move $s1, $a1
    li   $a0, 0
    move $a1, $s0
    move $a2, $s1
    jal  _rt_read_line_fd
    bne  $v0, $zero, _rt_as_ret
    move $a0, $s0
    li   $a1, 32
    move $a2, $s1
    jal  _rt_memset
_rt_as_ret:
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 16
    jr   $ra

_rt_accept_num:
    addiu $sp, $sp, -24
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    sw   $s2, 12($sp)
    sw   $s3, 16($sp)
    move $s0, $a0
    move $s1, $a1
    move $s2, $a2
    move $s3, $a3
    li   $a0, 0
    la   $a1, _rt_linebuf
    li   $a2, 64
    jal  _rt_read_line_fd
    la   $a0, _rt_linebuf
    li   $a1, 64
    li   $a2, 0
    srl  $a3, $s3, 1
    jal  _rt_parse_num
    move $a0, $s0
    move $a1, $s1
    move $a2, $v0
    srl  $t0, $s3, 1
    sll  $t0, $t0, 8
    sll  $t1, $s2, 1
    or   $t0, $t0, $t1
    andi $t2, $s3, 1
    or   $a3, $t0, $t2
    jal  _rt_format_num
    lw   $s3, 16($sp)
    lw   $s2, 12($sp)
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 24
    jr   $ra
)mips";

static const char *const kRtBigDigits = R"mips(
_rt_move_digits:
    move $t0, $a1
    move $t1, $a3
_rt_md_l:
    blez $t0, _rt_md_d
    addiu $t0, $t0, -1
    li   $t2, 48
_rt_md_find:
    blez $t1, _rt_md_st
    addiu $t1, $t1, -1
    addu $t3, $a2, $t1
    lbu  $t4, 0($t3)
    li   $t5, 48
    blt  $t4, $t5, _rt_md_find
    li   $t5, 57
    bgt  $t4, $t5, _rt_md_find
    move $t2, $t4
_rt_md_st:
    addu $t3, $a0, $t0
    sb   $t2, 0($t3)
    j    _rt_md_l
_rt_md_d:
    jr   $ra

_rt_accept_digits:
    addiu $sp, $sp, -16
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    move $s0, $a0
    move $s1, $a1
    li   $a0, 0
    la   $a1, _rt_linebuf
    li   $a2, 64
    jal  _rt_read_line_fd
    move $a0, $s0
    move $a1, $s1
    la   $a2, _rt_linebuf
    li   $a3, 64
    jal  _rt_move_digits
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 16
    jr   $ra
)mips";

static const char *const kRtStrFuncs = R"mips(
_rt_reverse:
    li   $t0, 0
_rt_rev_l:
    bge  $t0, $a2, _rt_rev_d
    subu $t1, $a2, $t0
    addiu $t1, $t1, -1
    addu $t2, $a1, $t1
    lbu  $t3, 0($t2)
    addu $t4, $a0, $t0
    sb   $t3, 0($t4)
    addiu $t0, $t0, 1
    j    _rt_rev_l
_rt_rev_d:
    jr   $ra

_rt_upper_case:
    li   $t0, 0
_rt_uc_l:
    bge  $t0, $a2, _rt_uc_d
    addu $t1, $a1, $t0
    lbu  $t2, 0($t1)
    li   $t3, 97
    blt  $t2, $t3, _rt_uc_st
    li   $t3, 122
    bgt  $t2, $t3, _rt_uc_st
    addiu $t2, $t2, -32
_rt_uc_st:
    addu $t4, $a0, $t0
    sb   $t2, 0($t4)
    addiu $t0, $t0, 1
    j    _rt_uc_l
_rt_uc_d:
    jr   $ra
)mips";

static const char *const kRtInspect = R"mips(
_rt_inspect_tally:
    srl  $t0, $a3, 16
    andi $t1, $a3, 0xFFFF
    bne  $t0, $zero, _rt_it_pat
    move $v0, $a1
    jr   $ra
_rt_it_pat:
    li   $v0, 0
    blez $t1, _rt_it_done
    li   $t2, 0
    subu $t3, $a1, $t1
_rt_it_loop:
    bgt  $t2, $t3, _rt_it_done
    li   $t4, 0
_rt_it_cmp:
    bge  $t4, $t1, _rt_it_match
    addu $t5, $t2, $t4
    addu $t5, $a0, $t5
    lbu  $t6, 0($t5)
    addu $t5, $a2, $t4
    lbu  $t7, 0($t5)
    bne  $t6, $t7, _rt_it_nomatch
    addiu $t4, $t4, 1
    j    _rt_it_cmp
_rt_it_match:
    addiu $v0, $v0, 1
    addu $t2, $t2, $t1
    j    _rt_it_loop
_rt_it_nomatch:
    li   $t5, 2
    beq  $t0, $t5, _rt_it_done
    addiu $t2, $t2, 1
    j    _rt_it_loop
_rt_it_done:
    jr   $ra

_rt_inspect_replace:
    lw   $t0, 0($sp)
    srl  $t1, $t0, 16
    andi $t2, $t0, 0xFFFF
    bne  $t1, $zero, _rt_ir_pat
    lbu  $t3, 0($a3)
    li   $t4, 0
_rt_ir_cl:
    bge  $t4, $a1, _rt_ir_done
    addu $t5, $a0, $t4
    sb   $t3, 0($t5)
    addiu $t4, $t4, 1
    j    _rt_ir_cl
_rt_ir_pat:
    blez $t2, _rt_ir_done
    li   $t3, 0
    subu $t4, $a1, $t2
_rt_ir_loop:
    bgt  $t3, $t4, _rt_ir_done
    li   $t5, 0
_rt_ir_cmp:
    bge  $t5, $t2, _rt_ir_match
    addu $t6, $t3, $t5
    addu $t6, $a0, $t6
    lbu  $t7, 0($t6)
    addu $t6, $a2, $t5
    lbu  $v0, 0($t6)
    bne  $t7, $v0, _rt_ir_nomatch
    addiu $t5, $t5, 1
    j    _rt_ir_cmp
_rt_ir_match:
    li   $t5, 0
_rt_ir_copy:
    bge  $t5, $t2, _rt_ir_after
    addu $t6, $a3, $t5
    lbu  $t7, 0($t6)
    addu $t6, $t3, $t5
    addu $t6, $a0, $t6
    sb   $t7, 0($t6)
    addiu $t5, $t5, 1
    j    _rt_ir_copy
_rt_ir_after:
    li   $t6, 3
    beq  $t1, $t6, _rt_ir_done
    addu $t3, $t3, $t2
    j    _rt_ir_loop
_rt_ir_nomatch:
    li   $t6, 2
    beq  $t1, $t6, _rt_ir_done
    addiu $t3, $t3, 1
    j    _rt_ir_loop
_rt_ir_done:
    jr   $ra

_rt_inspect_convert:
    lw   $t0, 0($sp)
    li   $t1, 0
_rt_ic_l:
    bge  $t1, $a1, _rt_ic_d
    addu $t2, $a0, $t1
    lbu  $t3, 0($t2)
    li   $t4, 0
_rt_ic_f:
    bge  $t4, $t0, _rt_ic_n
    addu $t5, $a2, $t4
    lbu  $t6, 0($t5)
    bne  $t3, $t6, _rt_ic_fn
    addu $t5, $a3, $t4
    lbu  $t6, 0($t5)
    sb   $t6, 0($t2)
    j    _rt_ic_n
_rt_ic_fn:
    addiu $t4, $t4, 1
    j    _rt_ic_f
_rt_ic_n:
    addiu $t1, $t1, 1
    j    _rt_ic_l
_rt_ic_d:
    jr   $ra
)mips";

static const char *const kRtDateTime = R"mips(
_rt_accept_datetime:
    addiu $sp, $sp, -32
    sw   $ra, 0($sp)
    sw   $s0, 4($sp)
    sw   $s1, 8($sp)
    sw   $s2, 12($sp)
    sw   $s3, 16($sp)
    sw   $s4, 20($sp)
    sw   $s5, 24($sp)
    move $s0, $a0
    move $s1, $a1
    move $s2, $a2
    jal  _rt_get_epoch
    li   $t0, 86400
    div  $v0, $t0
    mflo $s3
    mfhi $s4
    addiu $t1, $s3, 3
    li   $t2, 7
    div  $t1, $t2
    mfhi $t1
    addiu $s5, $t1, 1
    li   $t3, 1970
_rt_dt_yl:
    andi $t4, $t3, 3
    li   $t5, 365
    bne  $t4, $zero, _rt_dt_ys
    li   $t5, 366
_rt_dt_ys:
    blt  $s3, $t5, _rt_dt_yok
    subu $s3, $s3, $t5
    addiu $t3, $t3, 1
    j    _rt_dt_yl
_rt_dt_yok:
    addiu $t6, $s3, 1
    li   $t7, 0
    move $t0, $s3
    la   $t1, _rt_mdays
_rt_dt_ml:
    sll  $t2, $t7, 2
    addu $t2, $t1, $t2
    lw   $t4, 0($t2)
    li   $t2, 1
    bne  $t7, $t2, _rt_dt_mchk
    andi $t2, $t3, 3
    bne  $t2, $zero, _rt_dt_mchk
    addiu $t4, $t4, 1
_rt_dt_mchk:
    blt  $t0, $t4, _rt_dt_mok
    subu $t0, $t0, $t4
    addiu $t7, $t7, 1
    li   $t2, 11
    blt  $t7, $t2, _rt_dt_ml
_rt_dt_mok:
    addiu $t7, $t7, 1
    addiu $t0, $t0, 1
    li   $t2, 3
    beq  $s2, $t2, _rt_dt_dow
    li   $t2, 2
    beq  $s2, $t2, _rt_dt_day
    li   $t2, 4
    beq  $s2, $t2, _rt_dt_time
    li   $t2, 10000
    mult $t3, $t2
    mflo $a2
    li   $t2, 100
    mult $t7, $t2
    mflo $t4
    addu $a2, $a2, $t4
    addu $a2, $a2, $t0
    j    _rt_dt_fmt
_rt_dt_day:
    li   $t2, 1000
    mult $t3, $t2
    mflo $a2
    addu $a2, $a2, $t6
    j    _rt_dt_fmt
_rt_dt_dow:
    move $a2, $s5
    j    _rt_dt_fmt
_rt_dt_time:
    li   $t2, 3600
    div  $s4, $t2
    mflo $t0
    mfhi $t1
    li   $t2, 60
    div  $t1, $t2
    mflo $t4
    mfhi $t5
    li   $t2, 1000000
    mult $t0, $t2
    mflo $a2
    li   $t2, 10000
    mult $t4, $t2
    mflo $t0
    addu $a2, $a2, $t0
    li   $t2, 100
    mult $t5, $t2
    mflo $t0
    addu $a2, $a2, $t0
_rt_dt_fmt:
    move $a0, $s0
    move $a1, $s1
    li   $a3, 0
    jal  _rt_format_num
    lw   $s5, 24($sp)
    lw   $s4, 20($sp)
    lw   $s3, 16($sp)
    lw   $s2, 12($sp)
    lw   $s1, 8($sp)
    lw   $s0, 4($sp)
    lw   $ra, 0($sp)
    addiu $sp, $sp, 32
    jr   $ra
)mips";

static const char *const kRtPerf = R"mips(
_rt_perf_push:
    la   $t0, _rt_perf_top
    lw   $t1, 0($t0)
    li   $t2, 127
    bge  $t1, $t2, _rt_pp_ret
    la   $t3, _rt_perf_stk
    sll  $t4, $t1, 3
    addu $t3, $t3, $t4
    sw   $a0, 0($t3)
    sw   $a1, 4($t3)
    addiu $t1, $t1, 1
    sw   $t1, 0($t0)
_rt_pp_ret:
    jr   $ra

_rt_perf_chk:
    la   $t0, _rt_perf_top
    lw   $t1, 0($t0)
    blez $t1, _rt_pc_fall
    addiu $t2, $t1, -1
    la   $t3, _rt_perf_stk
    sll  $t4, $t2, 3
    addu $t3, $t3, $t4
    lw   $t5, 0($t3)
    bne  $t5, $a0, _rt_pc_fall
    lw   $t6, 4($t3)
    sw   $t2, 0($t0)
    jr   $t6
_rt_pc_fall:
    jr   $ra
)mips";

void codegen_emit_runtime(FILE *out, MipsTarget target, uint32_t m) {
    /* Resolve transitive runtime dependencies */
    m |= RT_EXIT;
    if (m & RT_PRINT_NUM)  m |= RT_PRINT;
    if (m & RT_ACCEPT)     m |= RT_LINE_IO | RT_MEM | RT_NUM;
    if (m & RT_BIG_DIGITS) m |= RT_LINE_IO;
    if (m & RT_DATETIME)   m |= RT_NUM;
    if (m & RT_PRINT)      m |= RT_WRITE_FD;
    if (m & RT_LINE_IO)    m |= RT_READ_FD | RT_WRITE_FD;

    /* Emit only the runtime .data buffers actually needed */
    std::fputs("\n    .data\n    .align 2\n", out);
    if (m & RT_DATETIME)   std::fputs("_rt_mdays:     .word 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31\n", out);
    if (m & RT_PERF)       std::fputs("_rt_perf_top:  .word 0\n_rt_perf_stk:  .space 1024\n", out);
    if (m & RT_RNG)        std::fputs("_rt_rng_seed:  .word 123456789\n", out);
    if (m & (RT_PRINT | RT_LINE_IO))       std::fputs("_rt_nl:        .byte 10, 0\n", out);
    if (m & (RT_PRINT_NUM | RT_LINE_IO))   std::fputs("_rt_chbuf:     .space 4\n", out);
    if (target == MIPS_TARGET_MARS && (m & (RT_READ_FD | RT_LINE_IO)))
                           std::fputs("_rt_inbuf:     .space 256\n", out);
    if (m & (RT_LINE_IO | RT_ACCEPT | RT_BIG_DIGITS | RT_OPEN_CLOSE))
                           std::fputs("_rt_linebuf:   .space 512\n", out);
    if (m & RT_NUM)        std::fputs("_rt_numbuf:    .space 64\n", out);
    if (m & RT_STR_FUNCS)  std::fputs("_rt_revbuf:    .space 256\n", out);
    if (m & RT_DATETIME)   std::fputs("_rt_datebuf:   .space 32\n", out);
    if (m & RT_RETCODE)    std::fputs("cob_g_retcode: .ascii \"+000000000\"\n", out);

    std::fputs("    .align 2\n    .text\n", out);
    std::fputs("\n# =========================================================================\n", out);
    std::fputs(target == MIPS_TARGET_MARS
                   ? "# Runtime Library (MARS/SPIM syscalls, on-demand modules)\n"
                   : "# Runtime Library (Linux MIPS32 o32 syscalls, on-demand modules)\n",
               out);
    std::fputs("# =========================================================================\n", out);

    bool mars = (target == MIPS_TARGET_MARS);
    if (m & RT_EXIT)       std::fputs(mars ? kMarsExit : kLinuxExit, out);
    if (m & RT_WRITE_FD)   std::fputs(mars ? kMarsWriteFd : kLinuxWriteFd, out);
    if (m & RT_READ_FD)    std::fputs(mars ? kMarsReadFd : kLinuxReadFd, out);
    if (m & RT_OPEN_CLOSE) std::fputs(mars ? kMarsOpenClose : kLinuxOpenClose, out);
    if (m & RT_DATETIME)   std::fputs(mars ? kMarsEpoch : kLinuxEpoch, out);

    if (m & RT_PRINT)      std::fputs(kRtPrint, out);
    if (m & RT_PRINT_NUM)  std::fputs(kRtPrintNum, out);
    if (m & RT_MEM)        std::fputs(kRtMem, out);
    if (m & RT_CMP_ALNUM)  std::fputs(kRtCmpAlnum, out);
    if (m & RT_CMP_DIGITS) std::fputs(kRtCmpDigits, out);
    if (m & RT_CLASS)      std::fputs(kRtClass, out);
    if (m & RT_NUM)        std::fputs(kRtNum, out);
    if (m & RT_EDITED)     std::fputs(kRtEdited, out);
    if (m & RT_ARITH)      std::fputs(kRtArith, out);
    if (m & RT_LINE_IO) {
        std::fputs(kRtLineIoHead, out);
        if (mars) std::fputs(kRtLineIoMarsHook, out);
        std::fputs(kRtLineIoBody, out);
    }
    if (m & RT_ACCEPT)     std::fputs(kRtAccept, out);
    if (m & RT_BIG_DIGITS) std::fputs(kRtBigDigits, out);
    if (m & RT_STR_FUNCS)  std::fputs(kRtStrFuncs, out);
    if (m & RT_INSPECT)    std::fputs(kRtInspect, out);
    if (m & RT_DATETIME)   std::fputs(kRtDateTime, out);
    if (m & RT_PERF)       std::fputs(kRtPerf, out);
}
