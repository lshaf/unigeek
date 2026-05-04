/*
** luaconf.h — ESP32 configuration (no PSRAM, float numbers)
** Replaces standard Lua 5.1.5 luaconf.h.
*/
#ifndef lconfig_h
#define lconfig_h

#include <limits.h>
#include <stddef.h>
#include <math.h>
#include <setjmp.h>

/* ── Integer storage types (used by llimits.h) ───────────────────────── */
#define LUAI_UINT32    unsigned int
#define LUAI_INT32     int
#define LUAI_MAXINT32  INT_MAX
#define LUAI_UMEM      size_t
#define LUAI_MEM       ptrdiff_t

/* ── Alignment (llimits.h does: typedef LUAI_USER_ALIGNMENT_T L_Umaxalign) */
#define LUAI_USER_ALIGNMENT_T  union { double u; void *s; long l; }

/* ── Number ↔ string conversion ──────────────────────────────────────── */
#define lua_number2str(s,n)   sprintf((s), LUA_NUMBER_FMT, (n))
#define lua_str2number(s,p)   strtof((s), (p))

/* ── Number: float (4 B) instead of double (8 B) ─────────────────────── */
#define LUA_NUMBER          float
#define LUA_NUMBER_SCAN     "%g"
#define LUA_NUMBER_FMT      "%.7g"
#define LUAI_UACNUMBER      float

#define lua_number2int(i,d)      ((i) = (int)(d))
#define lua_number2integer(i,n)  ((i) = (lua_Integer)(n))

#define luai_numunm(a)    (-(a))
#define luai_numeq(a,b)   ((a) == (b))
#define luai_numlt(a,b)   ((a) <  (b))
#define luai_numle(a,b)   ((a) <= (b))
#define luai_numadd(a,b)  ((a) + (b))
#define luai_numsub(a,b)  ((a) - (b))
#define luai_nummul(a,b)  ((a) * (b))
#define luai_numdiv(a,b)  ((a) / (b))
#define luai_nummod(a,b)  ((a) - floorf((a) / (b)) * (b))
#define luai_numpow(a,b)  (powf((a), (b)))
#define luai_numisnan(a)  (!luai_numeq((a), (a)))

/* ── Integer type ─────────────────────────────────────────────────────── */
#define LUA_INTEGER     ptrdiff_t
#define LUA_INTFRM_T    long
#define LUA_INTFRMLEN   "l"

/* ── Stack / call limits (RAM-critical on no-PSRAM ESP32) ────────────── */
#define LUAI_MAXSTACK    200
#define LUAI_MAXCSTACK   200
#define LUA_MAXCAPTURES  16
#define LUAI_MAXVARS     200
#define LUAI_MAXUPVALUES 60
#define LUAI_BITSINT     32

/* ── GC: run more aggressively to reduce fragmentation ───────────────── */
#define LUAI_GCPAUSE  110
#define LUAI_GCMUL    150

/* ── Buffers ──────────────────────────────────────────────────────────── */
#define LUA_BUFFERSIZE       256
#define LUAL_BUFFERSIZE      256
#define LUAI_MAXNUMBER2STR   32
#define LUA_MAXINPUT         256

/* ── String quoting (used in lauxlib.c / lbaselib.c error messages) ──── */
#define LUA_QL(x)   "'" x "'"
#define LUA_QS      LUA_QL("%s")

/* ── User state hooks — all no-ops ───────────────────────────────────── */
#define luai_userstateopen(L)       ((void)0)
#define luai_userstateclose(L)      ((void)0)
#define luai_userstatethread(L,L1)  ((void)0)
#define luai_userstatefree(L)       ((void)0)
#define luai_userstateresume(L,n)   ((void)0)
#define luai_userstateyield(L,n)    ((void)0)

/* ── Paths (loading done via IStorage, not stdio) ────────────────────── */
#define LUA_PATH_DEFAULT   ""
#define LUA_CPATH_DEFAULT  ""
#define LUA_PATH_SEP       ";"
#define LUA_PATH_MARK      "?"
#define LUA_EXECDIR        "!"
#define LUA_IGMARK         "-"

/* ── API visibility ───────────────────────────────────────────────────── */
#define LUA_API    extern
#define LUALIB_API extern
#define LUAI_FUNC  extern
#define LUAI_DATA  extern

/* ── Call depth limits (ldo.c / lgc.c) ───────────────────────────────── */
#define LUAI_MAXCALLS   200
#define LUAI_MAXCCALLS  200

/* ── Error handling: setjmp/longjmp ──────────────────────────────────── */
#define LUAI_THROW(L,c)  longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)  if (setjmp((c)->b) == 0) { a }
#define l_jmpbuf         jmp_buf
#define luai_jmpbuf      jmp_buf

/* ── No extra space in lua_State ─────────────────────────────────────── */
#define LUAI_EXTRASPACE 0

/* ── Misc ─────────────────────────────────────────────────────────────── */
#define LUA_IDSIZE   60
#define LUA_PROMPT   "> "
#define LUA_PROMPT2  ">> "
#define LUA_PROGNAME "lua"

/* ── Assertions disabled for release ─────────────────────────────────── */
#define lua_assert(c)      ((void)0)
#define luai_apicheck(L,o) ((void)(L))

#endif /* lconfig_h */
