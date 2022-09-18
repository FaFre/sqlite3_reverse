#include <assert.h>
#include <string.h>

#include "sqlite3ext.h" /* Do not use <sqlite3.h>! */
SQLITE_EXTENSION_INIT1

/* LMH from sqlite3 3.3.13 */
/*
** This table maps from the first byte of a UTF-8 character to the number
** of trailing bytes expected. A value '4' indicates that the table key
** is not a legal first byte for a UTF-8 character.
*/
static const unsigned char xtra_utf8_bytes[256]  = {
/* 0xxxxxxx */
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,

/* 10wwwwww */
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,

/* 110yyyyy */
1, 1, 1, 1, 1, 1, 1, 1,     1, 1, 1, 1, 1, 1, 1, 1,
1, 1, 1, 1, 1, 1, 1, 1,     1, 1, 1, 1, 1, 1, 1, 1,

/* 1110zzzz */
2, 2, 2, 2, 2, 2, 2, 2,     2, 2, 2, 2, 2, 2, 2, 2,

/* 11110yyy */
3, 3, 3, 3, 3, 3, 3, 3,     4, 4, 4, 4, 4, 4, 4, 4,
};


/*
** This table maps from the number of trailing bytes in a UTF-8 character
** to an integer constant that is effectively calculated for each character
** read by a naive implementation of a UTF-8 character reader. The code
** in the READ_UTF8 macro explains things best.
*/
static const int xtra_utf8_bits[] =  {
  0,
  12416,          /* (0xC0 << 6) + (0x80) */
  925824,         /* (0xE0 << 12) + (0x80 << 6) + (0x80) */
  63447168        /* (0xF0 << 18) + (0x80 << 12) + (0x80 << 6) + 0x80 */
};

/*
** If a UTF-8 character contains N bytes extra bytes (N bytes follow
** the initial byte so that the total character length is N+1) then
** masking the character with utf8_mask[N] must produce a non-zero
** result.  Otherwise, we have an (illegal) overlong encoding.
*/
static const int utf_mask[] = {
  0x00000000,
  0xffffff80,
  0xfffff800,
  0xffff0000,
};

#define EXT_READ_UTF8(zIn, c)                                                  \
  {                                                                            \
    int xtra;                                                                  \
    c = *(zIn)++;                                                              \
    xtra = xtra_utf8_bytes[c];                                                 \
    switch (xtra) {                                                            \
    case 4:                                                                    \
      c = (int)0xFFFD;                                                         \
      break;                                                                   \
    case 3:                                                                    \
      c = (c << 6) + *(zIn)++;                                                 \
    case 2:                                                                    \
      c = (c << 6) + *(zIn)++;                                                 \
    case 1:                                                                    \
      c = (c << 6) + *(zIn)++;                                                 \
      c -= xtra_utf8_bits[xtra];                                               \
      if ((utf_mask[xtra] & c) == 0 || (c & 0xFFFFF800) == 0xD800 ||           \
          (c & 0xFFFFFFFE) == 0xFFFE) {                                        \
        c = 0xFFFD;                                                            \
      }                                                                        \
    }                                                                          \
  }

static int sqlite3ReadUtf8(const unsigned char *z) {
  int c;
  EXT_READ_UTF8(z, c);
  return c;
}

/*
** X is a pointer to the first byte of a UTF-8 character.  Increment
** X so that it points to the next character.  This only works right
** if X points to a well-formed UTF-8 string.
*/
#define sqliteNextChar(X)                                                      \
  while ((0xc0 & *++(X)) == 0x80) {                                            \
  }
#define sqliteCharVal(X) sqlite3ReadUtf8(X)

/*
** given a string returns the same string but with the characters in reverse
*order
*/
static void reverseFunc(sqlite3_context *context, int argc,
                        sqlite3_value **argv) {
  const char *z;
  const char *zt;
  char *rz;
  char *rzt;
  int l = 0;
  int i = 0;

  assert(1 == argc);

  if (SQLITE_NULL == sqlite3_value_type(argv[0])) {
    sqlite3_result_null(context);
    return;
  }
  z = (char *)sqlite3_value_text(argv[0]);
  l = strlen(z);
  rz = sqlite3_malloc(l + 1);
  if (!rz) {
    sqlite3_result_error_nomem(context);
    return;
  }
  rzt = rz + l;
  *(rzt--) = '\0';

  zt = z;
  while (sqliteCharVal((unsigned char *)zt) != 0) {
    z = zt;
    sqliteNextChar(zt);
    for (i = 1; zt - i >= z; ++i) {
      *(rzt--) = *(zt - i);
    }
  }

  sqlite3_result_text(context, rz, -1, SQLITE_TRANSIENT);
  sqlite3_free(rz);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
    int sqlite3_sqlitereverse_init(sqlite3 *db, char **pzErrMsg,
                             const sqlite3_api_routines *pApi) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  rc = sqlite3_create_function_v2(db, "strrev", -1,
                                  SQLITE_UTF8 | SQLITE_DETERMINISTIC, NULL,
                                  reverseFunc, 0, 0, 0);

  return rc;
}