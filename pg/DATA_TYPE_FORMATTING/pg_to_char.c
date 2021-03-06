#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <locale.h>

#include <ksu_common.h>
#include <ksu_dates.h>
#include <ksu_pg.h>

#define FORMAT_LEN     200
#define FORMATTED_LEN   64
#define STACK_SZ        20

// Values for computing the output width
#define  RN_SZ           15    // Roman number
#define  CURRENCY_SZ     10    // Added for currency symbol other than $ (U/L)
#define  CUR_SZ           7    // Added for international currency symbol (C)
#define  EEEE_SZ          5    // Added for scientific notation

#define _issign(c) ((c == '+') || (c == '-'))

// Just for Roman conversion ...

#define RN_SIZE  8

typedef struct {
   int  val;
   char roman;
} RN_CONV_T;

static RN_CONV_T G_roman[RN_SIZE] =
               {{1, 'I'},    // can prefix the two next ones
                {5, 'V'},
                {10, 'X'},   // can prefix the two next ones
                {50, 'L'},
                {100, 'C'},  // can prefix the two next ones
                {500, 'D'},
                {1000, 'M'},
                {5000, '?'}};

#define _right_case(x) (upper ? x : tolower(x))

static void right_align(char *b, int width) {
   int len = strlen(b);
   int i;
   int j;

   if (len < width) {
     b[width] = '\0';
     i = width - 1;
     j = len - 1;
     while (j >= 0) {
       b[i--] = b[j--];
     }
     while (i >= 0) {
       b[i--] = ' ';
     }
   }
}

static char *roman(int num, char *buf, char upper, char ltrim){
   int   i;
   int   n;
   char *b = buf;
   char *bb = buf;

   if(num <= 0 || num >=4000){
     *buf='\0';
     return NULL;
   }
   while (num > 0) {
     i = 0;
     while (G_roman[i+1].val < num) {
       i++;
     }
     if (G_roman[i+1].val != num) {
        if (!(i % 2)) {
          if (i < RN_SIZE - 2) {
            if (num >= (G_roman[i+2].val - G_roman[i].val)) {
              n = G_roman[i+2].val - G_roman[i].val;
              *buf++ = _right_case(G_roman[i].roman);
              *buf++ = _right_case(G_roman[i+2].roman);
            } else if (num >= (G_roman[i+1].val - G_roman[i].val)) {
              n = G_roman[i+1].val - G_roman[i].val;
              *buf++ = _right_case(G_roman[i].roman);
              *buf++ = _right_case(G_roman[i+1].roman);
            } else {
              n = G_roman[i].val;
              *buf++ = _right_case(G_roman[i].roman);
            }
          } else {
            n = G_roman[i].val;
            *buf++ = _right_case(G_roman[i].roman);
          }
        } else {
          if (num >= (G_roman[i+2].val - G_roman[i-1].val)) {
            n = G_roman[i+2].val - G_roman[i-1].val;
            *buf++ = _right_case(G_roman[i-1].roman);
            *buf++ = _right_case(G_roman[i+2].roman);
          } else if (num >= (G_roman[i+1].val - G_roman[i-1].val)) {
            n = G_roman[i+1].val - G_roman[i-1].val;
            *buf++ = _right_case(G_roman[i-1].roman);
            *buf++ = _right_case(G_roman[i+1].roman);
          } else {
            n = G_roman[i].val;
            *buf++ = _right_case(G_roman[i].roman);
          }
        }
     } else {
        n = G_roman[i+1].val;
        *buf++ = _right_case(G_roman[i+1].roman);
     }
     num -= n;
   }
   *buf='\0';
   if (!ltrim) {
     right_align(b, RN_SZ);
   }
   return bb;
}

static char *prettify(char *strval, const char *fmt,
                      double val, char sign_pos) {
          char  result[FORMATTED_LEN];
          char  dc; // Decimal point, assumed to be a single char
          char *gs;
          short gslen;
          char *curr;
          char *f;
          short currlen;
          int   j;
          int   i;
          int   k;
          int   code;
          int   last_space;
          char  significant = 0;
          char  fmt_done = 0;
          char  sign;     // + or -
          char  no_number = 1; // Flag
          char  fm = 0;
   struct lconv *lc;
   static unsigned char  thinspace[4] = {226, 128, 137, 0};
   
   if (strval && fmt) {
      // We get a string that should already have been localized
      // by C for the decimal point. The sign should be in first 
      // position.
      sign = strval[0];
      // Replace it with a space
      strval[0] = ' ';
      // Set locale-dependent stuff
      lc = localeconv();
      if (lc->decimal_point
          && *(lc->decimal_point)) {
        dc = *(lc->decimal_point);
      } else {
        dc = '.';
      }
      gs = lc->thousands_sep;
      if ((*gs == '\0') || isspace(*gs)) {
         gs = (char *)thinspace;
      }
      gslen = (short)strlen(gs);
      while (gslen && isspace(gs[gslen-1])) {
        gslen--;
      }
      curr = lc->currency_symbol;
      if (strncasecmp(curr, "Eu", 2) == 0) {
        curr = "€";
      }
      currlen = (short)strlen(curr);
      while (currlen && isspace(curr[currlen-1])) {
        currlen--;
      }
      // Start processing the format
      //
      // Three different indexes to manage:
      //   i = number string in which to insert separators
      //       (and currency symbols?), our input, result of sprintf
      //       (strval)
      //   j = formatted result, our output (result)
      //   k = PostgreSQL format (for reference) (fmt)
      //
      j = 0;
      i = 1;  // Position 0 contains a space (was the sign)
      k = 0;
      // Skip "FM" in format if present
      if (strncmp(fmt, "FM", 2) == 0) {
        k = 2;
        fm = 1;
      } 
      // Ignore "B"
      if (fmt[k] == 'B') {
        k++;
      }
      if (isdigit(*fmt)) {
        result[j++] = ' ';
      }
      //
      f = (char *)&(fmt[k]);
      while (*f) {
         if (*f == '"') {
            k++;
            while (fmt[k] && (fmt[k] != '"')) {
               result[j++] = fmt[k++];
            }
         } else {
           if (!significant
               && (isdigit(*f) 
                   || (*f == '.')
                   || (*f == 'D'))) {
             if ((strval[i] == '.')
                 || (strval[i] == dc) 
                 || (isdigit(strval[i]) && (strval[i] != '0'))
                 || (isdigit(strval[i]) && (*f == '0'))) {
               significant = 1;
             }
             if (significant && no_number && !sign_pos) {
               result[j++] = (sign == '-' ? '-' : ' ');
               no_number = 0;
             } 
           }
           if (!fmt_done) {
             switch (*f) {
                case '\0':
                     fmt_done = 1;
                     break;
                case 'G':
                case ',':
                     if (significant) {
                       if (fmt[k] == ',') {
                        result[j++] = ',';
                       } else {
                         (void)memcpy(&(result[j]), gs, (size_t)gslen);
                         j += gslen;
                       }
                     }
                     break;
                case ' ':
                     // Add a space
                     result[j++] = ' ';
                     break;
                case 'V': // Ignore now
                     break;
                case '0':
                case 'D':
                     result[j++] = strval[i++];
                     break;
                case 'x':
                case 'X':
                     result[j++] = strval[i++];
                     break;
                case '9':
                     if (significant || (strval[i] != '0')) {
                       result[j++] = strval[i];
                     } else {
                       result[j++] = ' ';
                     }
                     i++;
                     break;
                case '$':
                     if (significant) {
                       result[j] = '\0';
                       j = strlen(result);
                     }
                     result[j++] = '$';
                     break;
                case 'C':  // International currency symbol (3-char ISO)
                     if (significant) {
                       result[j] = '\0';
                       j = strlen(result);
                     }
                     (void)memcpy(&(result[j]),
                                  lc->int_curr_symbol,
                                  (size_t)3);
                     j += 3;
                     break;
                case 'L':  // Local currency symbol
                case 'U':  // Alternate - same as local for us
                     if (significant) {
                       result[j] = '\0';
                       j = strlen(result);
                     }
                     (void)memcpy(&(result[j]), curr, (size_t)currlen);
                     j += currlen;
                     break;
                case '.':
                     // Change back to a dot if the locale set something else
                     if (strval[i] != '.') {
                       result[j++] = '.';
                     } else {
                       result[j++] = strval[i];
                     }
                     i++;
                     break;
                default:
                     if (isspace(*f)) {
                       // Add a space
                       result[j++] = ' ';
                     } else {
                       code = pgnum_best_match(f);
                       switch (code) {
                         case PGNUM_NOT_FOUND:
                              // Not supposed to happen at this stage
                              break;
                         case PGNUM_TM:
                         case PGNUM_EEEE:
                              // Already processed by sprintf
                              // Assumed to be the end of the format 
                              do {
                                result[j++] = strval[i++];
                              } while (strval[i]);
                              fmt_done = 1; // Cannot be followed by 9s or 0s
                              break;
                         case PGNUM_TH:
                              {
                               int vint;
  
                               switch (vint = (long)val % 10) {
                                 case 1:
                                 case 2:
                                 case 3:
                                      switch ((long)val % 100) {
                                        case 11:
                                        case 12:
                                        case 13:
                                             result[j++] = (isupper(*f) ?
                                                                  'T':'t');
                                             result[j++] = (isupper(*f) ?
                                                                  'H':'h');
                                             break;
                                        default:
                                             switch(vint) {
                                               case 1:
                                                    result[j++] = (isupper(*f) ?
                                                                  'S':'s');
                                                    result[j++] = (isupper(*f) ?
                                                                  'T':'t');
                                                    break;
                                               case 2:
                                                    result[j++] = (isupper(*f) ?
                                                                  'N':'n');
                                                    result[j++] = (isupper(*f) ?
                                                                  'D':'d');
                                                    break;
                                               case 3:
                                                    result[j++] = (isupper(*f) ?
                                                                  'R':'r');
                                                    result[j++] = (isupper(*f) ?
                                                                  'D':'d');
                                                    break;
                                             }
                                             break;
                                      }
                                      break;
                                 default:
                                      result[j++] = (isupper(*f) ? 'T':'t');
                                      result[j++] = (isupper(*f) ? 'H':'h');
                                      break;
                               }
                              }
                              k++;  // Additional shift
                              break;
                         case PGNUM_S: // Anchored to number
                              if (j && !fm) {
                                (void)memmove(result, &(result[1]), j);
                                j--;
                              }
                              result[j++] = sign;
                              break;
                         case PGNUM_MI:
                              if (j && !fm) {
                                (void)memmove(result, &(result[1]), j);
                                j--;
                              }
                              if (sign == '-') {
                                result[j++] = sign;
                              } else {
                                result[j++] = ' ';
                              }
                              k++;  // Additional shift
                              break;
                         case PGNUM_PR:
                              if (sign == '-') {
                                last_space = 0;
                                while (isspace(result[last_space])) {
                                  last_space++;
                                }
                                if (last_space) {
                                  result[last_space-1] = '<';
                                }
                                result[j++] = '>';
                              }
                              k++;  // Additional shift
                              break;
                         case PGNUM_PL:
                         case PGNUM_SG:
                              if (j && !fm) {
                                (void)memmove(result, &(result[1]), j);
                                j--;
                              }
                              if (sign == ' ') {
                                sign = '+';
                              }
                              result[j++] = sign;
                              k++;  // Additional shift
                              break;
                         default :
                              break;
                   }
                 }
                 break;
             }
          }
        }
        k++;
        f = (char *)&(fmt[k]);
      }           
      result[j] = '\0';
      strval[0] = '\0';
      strncat(strval,
              ((sign_pos || (result[0] != ' ')) ? result : &(result[1])),
              FORMAT_LEN-1);
   }
   return strval;
}

static char *pg_format_number(sqlite3_context *context,
                              sqlite3_value   *val,
                              const char      *fmt,
                              char            *formatted) {
    char   decimal_sep = '.';     // default 
    short  pos = 0;
    char  *f;
    char  *p;
    char  *sgn;
    int    precision = 0;     // Total number of characters printed
    int    scale = 0;         // Number of digits after the decimal point
    int    first_digit = 99999;
    int    last_digit = -1;
    // Bunch of formatting flags
    char   leading_zero = 0;  // flag
    char   b_flag = 0;        // flag - spaces instead of zeroes
    char   ltrim = 0;         // flag - "FM" format model
    char   zerotrim = 0;      // flag - "FM" format model
    char   eeee = 0;          // flag - scientific notation
    char   tm = 0;            // flag - condensed format
    char   hex = 0;           // flag - 1 for uppercase, 2 for lowercase
    char   sign = 0;          // flag - will be 1 for leading, 2 for trailing,
                              //        3 for training minus or space,
                              //        4 for angle brackets if negative
    char   decimal_only_nines = 1;  // flag
    // Flags for knowing where we are in the format
    char   decimal_part = 0;  // flag
    char   v_part = 0;        // flag
    char   format_done = 0;   // flag
    // Other variables
    short  nines = 0;         // Number of nines in the format
    char   negative;          // flag
    char   num_format[FORMAT_LEN];
    char   pg_format[FORMAT_LEN];
    int    pgfmt = 0;  // PostgreSQL format length
    char   isInt = 0;         // flag
    char   buffer[50];
    int    code;
    int    i;
    double digits;
    int    typ;
    short  extrasz = 0;   // For output width
    long   coef = 1;
   
    if ((f = (char *)fmt) != NULL) {
      // PostgreSQL ignores spaces at the beginning of a format
      while (isspace(*f)) {
        f++;
      }
      if (strncasecmp(f, "FM", 2) == 0) {
        ltrim = 1;
        zerotrim = 1;
        f += 2;
      }
      // Special case: Roman numbers
      if (strcasecmp(f, "rn") == 0) {
        int num = sqlite3_value_int(val);

        if ((num < 1) || (num >= 4000)) {
          // Value out of range
          for (i = 0; i < RN_SZ; i++) {
            formatted[i] = '#';
          }
          formatted[RN_SZ] = '\0';
          return formatted;
        }
        if (roman(num, formatted, isupper(*f), ltrim)) {
          return formatted;
        } else {
          // Conversion error - should not happen
          ksu_err_msg(context, KSU_ERR_GENERIC, "to_char");
          return NULL;
        }
      }
      pos = 0;
      num_format[0] = '\0';
      pg_format[pgfmt++] = ' ';
      while (*f) {
        switch (*f) {
          case '"':
              // Ignore quoted text
              f++;
              while (*f && (*f != '"')) {
                f++;
                extrasz++;
              }
              break;
          case ' ':
               // Ignore, for the time being
               extrasz++;
               break;
          case 'B':
               b_flag = 1;
               break;
          case 'D':
          case '.':
               if (decimal_part || hex) {
                 // Already seen or hexadecimal !
                 ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
                 return NULL;
               }
               pg_format[pgfmt++] = *f;
               decimal_sep = *f;
               decimal_part = 1;
               precision++;      // Must be counted
               break;
          case '0':
          case '9':
          case 'x':
          case 'X':
               if (format_done) {
                 ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
                 return NULL;
               }
               if (*f == '9') {
                  nines++;
                  if (v_part) {
                    coef *= 10;
                  }
               } else {
                 if (precision == 0) {
                   if (*f == '0') {
                     leading_zero = 1;
                   } else if (toupper(*f) == 'X') {
                     if (nines) {
                       ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
                       return NULL;
                     }
                     if (!hex) {
                       if (*f == 'X') {
                         hex = 1;
                       } else {
                         hex = 2;
                       }
                     }
                   }
                 } else {
                   if (decimal_part
                       && (*f == '0')
                       && decimal_only_nines) {
                     decimal_only_nines = 0;
                   }
                 }
               }
               precision++;
               if (decimal_part) {
                 scale++;
               }
               pg_format[pgfmt++] = *f;
               break;
          case ',':
          case 'G': 
               if (decimal_part || hex) {
                 ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
                 return NULL;
               }
               extrasz++;
               break;
          case '$':
          case 'C':
          case 'L':
          case 'U':
               if (precision && !format_done) {
                 format_done = 1;
               }
               switch (*f) {
                 case '$':
                   extrasz++;
                   break;
                 case 'C':
                   extrasz += CUR_SZ;
                   break;
                 case 'L':
                 case 'U':
                   extrasz += CURRENCY_SZ;
                   break;
               }
               break;
          case 'V':
               if (v_part || hex) {
                 // Already seen or hexadecimal
                 ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
                 return NULL;
               }
               v_part = 1;
               break;
          default:
               if (isspace(*f)) {
                 // Ignore, for the time being
                 extrasz++;
               } else {
                 code = pgnum_best_match(f);
                 switch (code) {
                   case PGNUM_NOT_FOUND:
                        ksu_err_msg(context, KSU_ERR_INV_FORMAT,
                                    (char *)fmt, "to_char");
                        return NULL;
                   case PGNUM_EEEE:
                        // PostgreSQL accepts either EEEE or eeee,
                        // but no mixed case. However, it doesn't
                        // generate an error but just outputs the
                        // string of Ees
                        if (strncmp(f, "EEEE", 4)
                            && strncmp(f, "eeee", 4)) {
                          ksu_err_msg(context, KSU_ERR_INV_FORMAT,
                                      (char *)fmt, "to_char");
                          return NULL;
                        }
                        eeee = 1;
                        f += 3;
                        extrasz += EEEE_SZ;
                        format_done = 1; // Cannot be followed by 9s or 0s
                        break;
                   case PGNUM_TM:
                        if ((precision == 0)
                            && ((strcasecmp(f, "TM") == 0)
                                || (strcasecmp(f, "TM9") == 0)
                                || (strcasecmp(f, "TME") == 0))) {
                           tm = 1;
                           f += strlen(f) - 1; // To stop the loop
                        } else {
                          ksu_err_msg(context, KSU_ERR_INV_FORMAT,
                                      (char *)fmt, "to_char");
                          return NULL;
                        }
                        break;
                   case PGNUM_TH:
                        f++;  // Additional shift
                        break;
                   case PGNUM_S: // Anchored to number
                        if (f == fmt) {
                          sign = 1;
                          pg_format[0] = *f;
                        } else {
                          sign = 2;
                        }
                        break;
                   case PGNUM_MI:
                   case PGNUM_PL:
                   case PGNUM_SG:
                        // With PostgreSQL can be anywhere
                        if (code == PGNUM_MI) {
                          sign = 3;
                        } else {
                          sign = 4;
                        }
                        f++;  // Additional shift
                        break;
                   case PGNUM_PR:
                        if (*(f+2) != '\0') {
                          ksu_err_msg(context, KSU_ERR_INV_FORMAT,
                                      (char *)fmt, "to_char");
                          return NULL;
                        }
                        sign = 4;
                        f++;  // Additional shift
                        extrasz++;
                        break;
                   default :
                        ksu_err_msg(context, KSU_ERR_INV_FORMAT,
                                    (char *)fmt, "to_char");
                        return NULL;
                 }
               }
               break;
        }
        f++;
        pos++;
      }           
      // Get rid of easy cases
      if (b_flag && (sqlite3_value_double(val) == (double)0)) {
        for (i = 0; i < pgfmt; i++) {
          formatted[i] = ' ';
        }
        formatted[pgfmt] = '\0';
        return formatted;
      }
      negative = (sqlite3_value_double(val) < 0);
      pg_format[pgfmt] = '\0';
      typ = sqlite3_value_type(val);
      if (tm) {
        strcpy(num_format, "%g");
      } else {
        if (precision) {
          if (!eeee) {
            digits = (double)precision - (scale ? 1 + scale : 0);
            if (sqlite3_value_double(val)
                && (log10((negative ? -1 : 1)
                        * sqlite3_value_double(val)) >= digits)) {
               // Format too small
               for (i = 0; i < pgfmt; i++) {
                  formatted[i] = '#';
               }
               formatted[pgfmt] = '\0';
               return formatted;
            }
            // Numbers already seen
            if (scale) {
              // Float/double
              sprintf(num_format,
                      "%%+%s%d.%dlf",
                      (leading_zero ? "0" : ""),
                      precision+1, scale);
            } else {
              if (hex) {
                sprintf(num_format,
                        "%%%s%d%s",
                        (leading_zero ? "0" : ""),
                        precision+1,
                        (hex == 1? "X":"x"));
              } else {
                // Int
                sprintf(num_format,
                        "%%+%s%dlld",
                        (leading_zero ? "0" : ""),
                        precision+1);
                isInt = 1;
              }
            }
          } else {  // Scientific notation
            sprintf(num_format,
                    "%%+0%d.%de",
                    precision+1, scale);
          }
        }
      }
      if (hex) {
        snprintf(formatted, FORMAT_LEN,
                 num_format,
                 (unsigned int)sqlite3_value_int(val));
      } else {
        if (isInt) {
          if (typ == SQLITE_INTEGER) {
            snprintf(formatted, FORMAT_LEN,
                     num_format,
                     (long long)coef * sqlite3_value_int64(val));
          } else {
            snprintf(formatted, FORMAT_LEN,
                     num_format,
                     (long long)(coef * sqlite3_value_double(val)+0.5));
          }
        } else {
          snprintf(formatted, FORMAT_LEN,
                   num_format,
                   coef * sqlite3_value_double(val));
        }
      }
      if (!eeee && (strlen(formatted) > pgfmt)) {
        for (i = 0; i < pgfmt; i++) {
           formatted[i] = '#';
        }
        formatted[pgfmt] = '\0';
        return formatted;
      }
      // Bring sign to first place
      if ((sgn = strchr(formatted, '+')) != NULL) {
        formatted[0] = '+';
        *sgn = ' ';
      } else if ((formatted[0] != '-')
                 && ((sgn = strchr(formatted, '-')) != NULL)) {
        formatted[0] = '-';
        *sgn = ' ';
      }
      i = 1;  // Skip sign
      // Leading zeroes
      while (isspace(formatted[i])) {
        if (((i < pgfmt)
             && (pg_format[i] == '0'))
            || (i > first_digit)) {
          formatted[i] = '0';
          if (i < first_digit) {
            first_digit = i;
          }
        }
        i++;
      }
      // Trailing - Trailing zeroes are suppressed ONLY if
      // the format isn't only made of 9s after the decimal
      // position or if asked explicitly (FM model).
      if (!decimal_only_nines || zerotrim) { 
        i = strlen(formatted) - 1;
        while (i
               && (isspace(formatted[i])
                   || formatted[i] == '0')
               && (i > last_digit)) {
          if ((i < pgfmt) && (pg_format[i] == '0')) {
            formatted[i] = '0';
            if (i > last_digit) {
              last_digit = i;
            }
          } else if ((formatted[i] == '0')
                     && ((i>=pgfmt) || pg_format[i] == '9')) {
            formatted[i] = '\0';
          }
          i--;
        }
      }
      // Set group separators and currencies
      strcpy(buffer, prettify(formatted, fmt, sqlite3_value_double(val), sign));
      // Special case : if the value is all spaces, make it 0
      // unless the b_flag is set
      if (!b_flag) {
        p = formatted;
        while (isspace(*p)) {
          p++;
        }
        if (*p == '\0') {
          p--;
          *p = '0';
        }
      }
      if (ltrim) {
        p = formatted;
        while (isspace(*p)) {
          p++;
        }
        if (p != formatted) {
          i = 0;
          do {
            formatted[i++] = *p++;
          } while (*p);
          formatted[i] = '\0';
        }
      }
    }
    return formatted;
}

// Spelling out of numbers (dates, which limits the range)
static char *G_units[20] = {"zero", "one", "two", "three", "four",
                            "five", "six", "seven", "eight", "nine",
                            "ten", "eleven", "twelve", "thirteen",
                            "fourteen", "fifteen", "sixteen", "seventeen",
                            "eighteen", "nineteen"};
static char *G_unitsth[20] = {"", "first", "second", "third", "fourth",
                            "fifth", "sixth", "seventh", "eighth", "ninth",
                            "tenth", "eleventh", "twelfth", "thirteenth",
                            "fourteenth", "fifteenth", "sixteenth",
                            "seventeenth", "eighteenth", "nineteenth"};
static char *G_tens[10] = {"", "ten", "twenty", "thirty", "forty",
                           "fifty", "sixty", "seventy", "eighty", "ninety"};
static char *G_tensth[10] = {"", "tenth", "twentieth", "thirtieth",
                             "fortieth", "fiftieth", "sixtieth",
                             "seventieth", "eightieth", "ninetieth"};

static void capitalize(char *from_buf, char *to_buf, char cap) {
   // Cap: 0 = lower, 1 = upper, 2 = capitalize initial
   // Note that we might have multi-byte characters
   unsigned char *f = (unsigned char *)from_buf;
   unsigned char *t = (unsigned char *)to_buf;
   unsigned char *p;
   unsigned char  utf8[5];
   unsigned char *utf;
            char  after_letter = 0;

   if (f && t) {
     while (*f) {
       utf = utf8;
       switch (cap) {
         case 0:  // lower
           if (ksu_utf8_charlower((const unsigned char *)f, utf8)) {
             _ksu_utf8_copychar(utf, t);
           }
           SQLITE_SKIP_UTF8(f);
           break;
         case 1:  // upper
           if (ksu_utf8_charupper((const unsigned char *)f, utf8)) {
             _ksu_utf8_copychar(utf, t);
           }
           SQLITE_SKIP_UTF8(f);
           break;
         default: // nothing other than "2" expected (capitalize initial)
           if (ksu_is_letter((const unsigned char *)f)) {
             if (after_letter) {
               p = ksu_utf8_charlower((const unsigned char *)f, utf8);
             } else {
               p = ksu_utf8_charupper((const unsigned char *)f, utf8);
             }
             if (p) {
               _ksu_utf8_copychar(utf, t);
             }
             SQLITE_SKIP_UTF8(f);
             after_letter = 1;
           } else {
             _ksu_utf8_copychar(f, t);
             after_letter = 0;
           }
           break;
       }
     }
     *t = '\0';
   }
}

static void spell_out_2digits(int num, char *buffer) {
    int  n1;
    int  n2;

    if (buffer && (num < 100)) {
      if (num < 20) {
        strcpy(buffer, G_units[num]);
      } else {
        // Get tens
        n1 = num / 10;
        strcpy(buffer, G_tens[n1]);
        n2 = num - n1 * 10;
        if (n2) {
          strcat(buffer, "-");
          strcat(buffer, G_units[n2]);
        }
      }
    }
}

static void spell_out_year(int year, char *buffer, char cap) {
    // Only English is supported.
    //
    // Cap: 0 = lower, 1 = upper, 2 = capitalize initial
    int  y1;
    int  y2;
    int  len;
    char tmpbuf[FORMAT_LEN];

    // From
    // http://babelhut.com/languages/english/how-to-read-years-in-english/
    //
    // Algorithm for Reading Years
    //
    //  If there there are no thousands’ or hundreds’ digits, read
    //  the number as-is. Examples:
    //     54 – “fifty-four”
    //     99 – “ninety-nine”
    //      0 – “zero”
    //      8 – “eight”
    //  If there is a thousands’ digit but the hundreds’ digit is zero,
    //  you can read the number as “n thousand and x”. If the last two
    //  digits are zero, you leave off the “and x” part. Examples:
    //   1054 – “one thousand and fifty-four”
    //   2007 – “two thousand and seven”
    //   1000 – “one thousand”
    //   2000 – “two thousand”
    //  If the hundreds’ digit is non-zero, you can read the number as
    //  “n hundred and x”. If the last two digits are zero, you leave off
    //  the “and x” part. Examples:
    //    433 – “four hundred and thirty-three”
    //   1492 – “fourteen hundred and ninety-two” (who sailed the ocean blue?)
    //   1200 – “twelve hundred”
    //    600 – “six hundred”
    //  The above rule produces some formal and old-fashioned names. Where
    //  it exists, it is acceptable to omit “hundred and”. If you do, and
    //  the tens’ digit is zero, you must read that zero as “oh”. Examples:
    //    432 – “four thirty-two”
    //   1492 – “fourteen ninety-two”
    //   1908 – “nineteen oh eight”
    //   1106 – “eleven oh six”
    //  Finally, though uncommon it is possible to read the years in rule #2
    //  using the systems for rules #3 and #4. Examples:
    //   1054 – “ten hundred and fifty-four” (if this sounds wrong to you,
    //          imagine you are watching a documentary on the history channel
    //          and the stiff narrator begins: “In the year ten hundred and
    //          fifty-four, Pope Leo IX died.”)
    //   1054 – “ten fifty-four”
    //   3026 – “thirty twenty-six”
    //   2007 – “twenty oh seven” (if this sounds wrong to you, imagine
    //          you live in 1972 and you are reading a science fiction
    //          story that starts: “In the year twenty oh seven, the world
    //          was overrun by blood-thirsty robots.”)
    //
    //  Rules adopted slightly different from above
    //
    if (year < 0) {
      year *= -1;
    }
    if (year < 100) {
      spell_out_2digits(year, tmpbuf);
      strcpy(tmpbuf, G_units[year]);
    } else {
      y1 = year / 100;
      y2 = year - y1 * 100;
      if ((y1 < 10) || (y2 < 20)) {
        if ((y1 % 10) == 0) {
          // Hundreds' digit is zero. Thousands' digits necessarily non zero.
          y1 /= 10;
          if (y1 == 1) {
            strcpy(tmpbuf, "thousand");
          } else {
            sprintf(tmpbuf, "%s thousand", G_units[y1]);
          }
        } else {
          spell_out_2digits(y1, tmpbuf);
          strcat(tmpbuf, " hundred");
        }
        if (y2 > 0) {
          strcat(tmpbuf, " and ");
          strcat(tmpbuf, G_units[y2]);
        }
      } else {
        // Just spell out y1 and y2 separately
        spell_out_2digits(y1, tmpbuf);
        strcat(tmpbuf, " ");
        len = strlen(tmpbuf);
        spell_out_2digits(y2, &(tmpbuf[len]));
      }
    }
    capitalize(tmpbuf, buffer, cap);
}

static void spell_out_day(int day, char *buffer, char cap) {
    // Only English is supported.
    char tmpbuf[FORMAT_LEN];
    int  t;
    int  u;

    if ((day > 0) && (day <= 31)) {
      if (day < 20) {
        strcpy(tmpbuf, G_units[day]);
      } else {
        t = day / 10;
        u = day - 10 * t;
        strcpy(tmpbuf, G_tens[t]);
        if (u) {
          strcat(tmpbuf, "-");
          strcat(tmpbuf, G_units[u]);
        }
      }
      capitalize(tmpbuf, buffer, cap);
    }
}

static void spell_out_day_ord(int day, char *buffer, char cap) {
    // Only English is supported.
    char tmpbuf[FORMAT_LEN];
    int  t;
    int  u;

    if ((day > 0) && (day <= 31)) {
      if (day < 20) {
        strcpy(tmpbuf, G_unitsth[day]);
      } else {
        t = day / 10;
        u = day - 10 * t;
        if (u) {
          strcpy(tmpbuf, G_tens[t]);
          strcat(tmpbuf, "-");
          strcat(tmpbuf, G_unitsth[u]);
        } else {
          strcpy(tmpbuf, G_tensth[t]);
        }
      }
      capitalize(tmpbuf, buffer, cap);
    }
}

// For indicators
#define IND_DOTS         8
#define IND_FIRST_UPPER  4
#define IND_SECOND_UPPER 2
#define IND_SET          1

// For number rendering (spelling / ordinal)
#define NUM_SPELL        1
#define NUM_ORD          2

static char *pg_format_date(sqlite3_context *context,
                            KSU_TIME_T       t,
                            const char      *fmt,
                            char            *formatted) {
    char       *f;
    char       *p;
    char       *suf;   // Suffix
    int         code; 
    int         code_suf; 
    long        val;
    short       rendering = 0;
    char        buf[FORMATTED_LEN];
    char        capbuf[FORMATTED_LEN]; // For capitalization
    int         len;
    int         what;
    int         i = 0;
    int         k;
    int         pad;
    char        fm = 0;  // flag for variable model
    char        am = 0;  // flag for am/pm
    char        bc = 0;  // flag for bc/ad
    char        cap;     // flag for capitalization
                         // 0 = lower, 1 = upper, 2 = capitalize initial
    char        processed;  // flag
    char        str;
    int         hour = 25;    // Must be memorized for am/pm indicators
    int         year = 0;    // Must be memorized for bc/ad indicators

    if ((f = (char *)fmt) != NULL) {
      while (*f) {
        // Punctuation and quoted text output as is
        if (*f == '"') {
          f++; // Skip double quote
          while (*f && (*f != '"')) {
            formatted[i++] = *f++;
          }
          if (*f != '"') {
            ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
            return NULL;
          }
          len = 1;
        } else if (ispunct(*f) || isspace(*f)) {
          formatted[i++] = *f;
          len = 1;
        } else {
          code = pgtimfmt_best_match(f);
          if ((code != PGTIMFMT_AMBIGUOUS)
              && (code != PGTIMFMT_NOT_FOUND)) {
            len = strlen(pgtimfmt_keyword(code));
          } else {
            ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
            return NULL;
          }
          str = 0;
          switch(code) {
            // Variable "what" is set to what should be extracted
            // from the date. If nothing has to be extracted,
            // 'what' is set to KSU_DATE_PART_NOT_FOUND.
            case PGTIMFMT_YYYY:  // 4-digit year
            case PGTIMFMT_Y_YYY: // Same as before but as Y,YYY (with a comma)
            case PGTIMFMT_YYY:   // Last 3, 2, or 1 digit(s) of year.
            case PGTIMFMT_YY:
            case PGTIMFMT_Y:
                 what = KSU_DATE_PART_YEAR;
                 break;
            case PGTIMFMT_WW: // Week of year (1-53) where week 1 starts on
                            // the first day of the year and continues to
                            // the seventh day of the year.
                 what = KSU_DATE_PART_YEAR;
                 break;
            case PGTIMFMT_W:  // Week of month (1-5) where week 1 starts on
                            // the first day of the month and ends on the
                            // seventh.
                 what = KSU_DATE_PART_DAY;
                 break;
            case PGTIMFMT_MS:
            case PGTIMFMT_US:
                 // Milli/Microseconds - ignore
            case PGTIMFMT_TZ:
            case PGTIMFMT_OF:
                 // Timezone stuff - ignore
                 what = KSU_DATE_PART_NOT_FOUND;
                 break;
            case PGTIMFMT_SSSS: // Seconds past midnight (0-86399).
                 what = KSU_DATE_PART_SECONDS_IN_DAY;
                 break;
            case PGTIMFMT_SS:    // Second (0-59).
                 what = KSU_DATE_PART_SECOND;
                 break;
            case PGTIMFMT_CC: // Century.
                              // If the last 2 digits of a 4-digit year are
                              // between 01 and 99 (inclusive), then
                              // the century is one greater than the first
                              // 2 digits of that year.
                              // If the last 2 digits of a 4-digit year
                              // are 00, then the century is the same as the
                              // first 2 digits of that year.
                              // For example, 2002 returns 21; 2000 returns
                              // 20.
                 what = KSU_DATE_PART_CENTURY;
                 break;
            case PGTIMFMT_Q:     // Quarter of year (1, 2, 3, 4;
                               // January - March = 1).
                 what = KSU_DATE_PART_QUARTER;
                 break;
            case PGTIMFMT_PM:
            case PGTIMFMT_P_M_:
            case PGTIMFMT_AM:
            case PGTIMFMT_A_M_:
                 // Use a xx or xxxx place holder
                 for (k = 0; k < len; k++) {
                   formatted[i++] = 'x';
                 }
                 am = IND_SET | (len == 4 ? IND_DOTS : 0)
                              | (isupper(*f) ? IND_FIRST_UPPER : 0)
                              | (isupper(*(f + (len == 4 ? 2: 1))) ?
                                          IND_SECOND_UPPER : 0);
                 what = KSU_DATE_PART_NOT_FOUND;
                 break;
            case PGTIMFMT_J:     // Julian day; the number of days since
                               // January 1, 4712 BC.
                 what = KSU_DATE_PART_JULIAN_NUM;
                 break;
            case PGTIMFMT_RM:    // Roman numeral month (I-XII; January = I).
            case PGTIMFMT_MM:    // Month (01-12; January = 01).
                 what = KSU_DATE_PART_MONTH;
                 break;
            case PGTIMFMT_MONTH: // Name of month, padded with blanks to display
                               // width of the widest name of month in the date
                               // language used for this element.
                 what = KSU_DATE_PART_MONTHNAME;
                 str = 1;
                 break;
            case PGTIMFMT_MON:   // Abbreviated name of month.
                 what = KSU_DATE_PART_MONTHABBREV;
                 str = 1;
                 break;
            case PGTIMFMT_IYYY:  // Last 4, 3, 2, or 1 digit(s) of ISO year.
            case PGTIMFMT_IYY:
            case PGTIMFMT_IY:
            case PGTIMFMT_I:
                 what = KSU_DATE_PART_ISOYEAR;
                 break;
            case PGTIMFMT_IW:    // Week of year (1-52 or 1-53) based on the
                               // ISO standard.
                 what = KSU_DATE_PART_ISOWEEK;
                 break;
            case PGTIMFMT_HH24:  // Hour of day (0-23).
            case PGTIMFMT_HH12:  // Hour of day (1-12).
            case PGTIMFMT_HH:    // Same as HH12
                 what = KSU_DATE_PART_HOUR;
                 break;
            case PGTIMFMT_MI:    // Minute (0-59).
                 what = KSU_DATE_PART_MINUTE;
                 break;
            case PGTIMFMT_FX:
                 fm = 0;
                 what = KSU_DATE_PART_NOT_FOUND;
                 break;
            case PGTIMFMT_FM:
                 fm = 1;
                 what = KSU_DATE_PART_NOT_FOUND;
                 break;
            case PGTIMFMT_DY:   // Abbreviated name of day.
                 what = KSU_DATE_PART_WEEKDAYABBREV;
                 str = 1;
                 break;
            case PGTIMFMT_DDD:  // Day of year (1-366).
                 what = KSU_DATE_PART_DAYOFYEAR;
                 break;
            case PGTIMFMT_DD:   // Day of month (1-31).
                 what = KSU_DATE_PART_DAY;
                 break;
            case PGTIMFMT_DAY:  // Name of day, padded with blanks to display
                              // width of the widest name of day in the date
                              // language used for this element.
                 what = KSU_DATE_PART_WEEKDAYNAME;
                 str = 1;
                 break;
            case PGTIMFMT_D:    // Day of week (1-7), 1 = Monday
                 what = KSU_DATE_PART_ISOWEEKDAY;
                 break;
            // BC/AD indicator
            case PGTIMFMT_BC:
            case PGTIMFMT_B_C_:
            case PGTIMFMT_AD:
            case PGTIMFMT_A_D_:
                 // Use a zz or zzzz place holder
                 for (k = 0; k < len; k++) {
                   formatted[i++] = 'z';
                 }
                 bc = IND_SET | (len == 4 ? IND_DOTS : 0)
                              | (isupper(*f) ? IND_FIRST_UPPER : 0)
                              | (isupper(*(f + (len == 4 ? 2: 1))) ?
                                          IND_SECOND_UPPER : 0);
                 what = KSU_DATE_PART_NOT_FOUND;
                 break;
            default:
                 // REALLY not found or suffix (processed elsewhere)
                 // Generate an error
                 ksu_err_msg(context, KSU_ERR_INV_FORMAT, fmt, "to_char");
                 return NULL;
          }
          // Check whether there is a suffix
          suf = f + len;
          code_suf = pgtimfmt_best_match(suf);
          rendering = 0;
          switch (code_suf) {
            // Suffixes - ordinal and spelling out
            case PGTIMFMT_TH:
                 rendering = NUM_ORD;
                 break;
            case PGTIMFMT_SP:
                 rendering = NUM_SPELL;
                 break;
            default:
                 // Ignore for now
                 break;
          }
          if (what != KSU_DATE_PART_NOT_FOUND) {
            if (str) {
              if (ksu_extract_str(t, what, buf, FORMATTED_LEN) != 1) {
                buf[0] = '\0';
              }
              // String results won't be transformed, except for
              // capitalization and padding
              if (islower(*f)) {
                cap = 0;
              } else {
                if (islower(*(f+1))) {
                  cap = 2;
                } else {
                  cap = 1;
                }
              }
              capitalize(buf, capbuf, cap);
              if (!fm) {
                switch(code) {
                  case PGTIMFMT_MONTH: 
                       pad = ksu_maxmonth();
                       break;
                  case PGTIMFMT_MON: 
                       pad = ksu_maxmon();
                       break;
                  case PGTIMFMT_DAY: 
                       pad = ksu_maxday();
                       break;
                  case PGTIMFMT_DY: 
                       pad = ksu_maxdy();
                       break;
                  default :
                       pad = 0; 
                       break;
                }
              } else {
                pad = 0;
              }
              (void)strncpy(&(formatted[i]), capbuf, FORMATTED_LEN - i);
              i = strlen(formatted);
              if (pad) {
                for (k = (int)strlen(capbuf); k < pad; k++) {
                  formatted[i++] = ' ';
                }
                formatted[i] = '\0';
              }
            } else {
              if (ksu_extract_int(t, what, &val) != 1) {
                val = 0;
              }
              // Numerical results may be widely transformed
              processed = 0;
              if (rendering & NUM_SPELL) {
                if (islower(*f)) {
                  cap = 0;
                } else {
                  if (islower(*(f+1))) {
                    cap = 2;
                  } else {
                    cap = 1;
                  }
                }
                if (code == PGTIMFMT_DD) {  // Day of month (1-31).
                   if (rendering & NUM_ORD) {
                     spell_out_day_ord((int)val, buf, cap);
                   } else {
                     spell_out_day((int)val, buf, cap);
                   }
                   (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                   processed = 1;
                } else if (code == PGTIMFMT_YYYY) { // Spell year
                   year = (int)val;
                   if (val < 0) {
                     val *= -1;
                   }
                   p = f;
                   spell_out_year(year, buf, cap);
                   (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                   processed = 1;
                }
              }
              if (!processed) {
                switch(code) {
                  case PGTIMFMT_YYYY:  // 4-digit year
                  case PGTIMFMT_Y_YYY: // Same as before but as Y,YYY (with a
                                     // comma)
                  case PGTIMFMT_YYY:   // Last 3, 2, or 1 digit(s) of year.
                  case PGTIMFMT_YY:
                  case PGTIMFMT_Y:
                       year = (int)val;
                       if (val < 0) {
                         val *= -1;
                       }
                       if (code == PGTIMFMT_Y_YYY) {
                         if (!fm) {
                           sprintf(buf, "'%04d", (int)val);
                         } else {
                           sprintf(buf, "'%d", (int)val);
                         }
                       } else {
                         if (!fm) {
                           sprintf(buf, "%04d", (int)val);
                         } else {
                           sprintf(buf, "%d", (int)val);
                         }
                       }
                       k = 0;
                       if (len < 4) {
                         if (!fm) {
                           for (k = 0; k < strlen(buf) - len; k++) {
                             buf[k] = ' ';
                           }
                           k = 0;
                         } else {
                           k = strlen(buf) - len;
                         }
                       }
                       (void)strncpy(&(formatted[i]),
                                     &(buf[k]),
                                     FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_WW: // Week of year (1-53)
                       if (!fm) {
                         sprintf(buf, "%02ld", val);
                       } else {
                         sprintf(buf, "%ld", val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_W:  // Week of month (1-5)
                       sprintf(buf, "%ld", val);
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_SSSS: // Seconds past midnight (0-86399).
                       if (!fm) {
                         sprintf(buf, "%05ld", val);
                       } else {
                         sprintf(buf, "%ld", val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_SS:    // Second (0-59).
                       sprintf(buf, "%02ld", val);
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_CC:
                       year = (int)val*100; // In case BC/AD would be required
                       if (!fm) {
                         sprintf(buf, "%02d", (int)val);
                       } else {
                         sprintf(buf, "%d", (int)val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_Q:     // Quarter of year (1, 2, 3, 4;
                                     // January - March = 1).
                       sprintf(buf, "%ld", val);
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_J:     // Julian day; the number of days since
                                     // January 1, 4712 BC.
                       if (!fm) {
                         sprintf(buf, "%ld", val);
                       } else {
                         sprintf(buf, "%07ld", val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_RM:    // Roman numeral month
                       // Last parameter = ltrim
                       if (roman((int)val, buf, (char)isupper(*f), (char)1)){
                         (void)strncpy(&(formatted[i]),
                                       buf, FORMATTED_LEN - i);
                       }
                       if (!fm) {
                         i = strlen(formatted);
                         for (k = strlen(buf); k < 4; k++) {
                           formatted[i++] = ' ';
                         }
                         formatted[i] = '\0';
                       }
                       break;
                  case PGTIMFMT_MM:    // Month (01-12; January = 01).
                       if (!fm) {
                         sprintf(buf, "%02ld", val);
                       } else {
                         sprintf(buf, "%ld", val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_IYYY:  // Last n digit(s) of ISO year.
                  case PGTIMFMT_IYY:
                  case PGTIMFMT_IY:
                  case PGTIMFMT_I:
                       year = (int)val;
                       if (val < 0) {
                         val *= -1;
                       }
                       sprintf(buf, "%04d", (int)val);
                       k = 0;
                       if (len < 4) {
                         if (!fm) {
                           for (k = 0; k < strlen(buf) - len; k++) {
                             buf[k] = ' ';
                           }
                           k = 0;
                         } else {
                           k = strlen(buf) - len;
                         }
                       }
                       (void)strncpy(&(formatted[i]),
                                     &(buf[k]),
                                     FORMATTED_LEN - i);
                       what = KSU_DATE_PART_ISOYEAR;
                       break;
                  case PGTIMFMT_IW:    // Week of year (1-52 or 1-53)
                       if (!fm) {
                         sprintf(buf, "%02ld", val);
                       } else {
                         sprintf(buf, "%ld", val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_HH24:  // Hour of day (0-23).
                  case PGTIMFMT_HH12:  // Hour of day (1-12).
                  case PGTIMFMT_HH:    // Same as HH12
                       hour = (int)val;
                       if (code != PGTIMFMT_HH24) {
                         if (val >= 13) {
                           val -= 12;
                         }
                       }
                       sprintf(buf, "%02ld", val);
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_MI:    // Minute (0-59).
                       sprintf(buf, "%02ld", val);
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_DDD:  // Day of year (1-366).
                       if (!fm) {
                         sprintf(buf, "%03ld", val);
                       } else {
                         sprintf(buf, "%ld", val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_DD:   // Day of month (1-31).
                       if (!fm) {
                         sprintf(buf, "%02ld", val);
                       } else {
                         sprintf(buf, "%ld", val);
                       }
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  case PGTIMFMT_D:    // Day of week (1-7).
                       sprintf(buf, "%ld", val);
                       (void)strncpy(&(formatted[i]), buf, FORMATTED_LEN - i);
                       break;
                  default:
                       // REALLY not found or suffix (processed elsewhere)
                       // Generate an error
                       ksu_err_msg(context, KSU_ERR_INV_FORMAT,
                                            fmt, "to_char");
                       return NULL;
                }
                if (rendering & NUM_ORD) {
                  i = strlen(formatted);
                  if ((val <= 10) || (val >= 20)) {
                    switch (val % 10) {
                      case 1:
                          strcpy(buf, "st");
                          break;
                      case 2:
                          strcpy(buf, "nd");
                          break;
                      case 3:
                          strcpy(buf, "rd");
                          break;
                      default:
                          strcpy(buf, "th");
                          break;
                    }
                 } else {
                    strcpy(buf, "th");
                  }
                  if (islower(*suf)) {
                    cap = 0;
                  } else {
                    if (islower(*(suf+1))) {
                      cap = 2;
                    } else {
                      cap = 1;
                    }
                  }
                  capitalize(buf, capbuf, cap);
                  (void)strncpy(&(formatted[i]), capbuf, FORMATTED_LEN - i);
                }
              }
              i = strlen(formatted);
            }
          }
          if (rendering) {
            // Be ready to skip rendering info
            f += strlen(pgtimfmt_keyword(code_suf));
          }
        }
        f += len;
      }
      formatted[i] = '\0';
    }
    // Look for am/pm (xx) or bc/ad (zz) placeholders
    if (am && ((p = strstr(formatted, "xx")) != NULL)) {
      if (hour == 25) {
         // We don't have it yet!
         if (ksu_extract_int(t, KSU_DATE_PART_HOUR, &val) != 1) {
            val = 0;
         }
         hour = (int)val;
      }
      k = 1;  // Position of second letter
      if (am & IND_DOTS) {
        p[1] = '.';
        p[3] = '.';
        k = 2;
      }
      if (hour >= 12) { // pm
        p[0] = (am & IND_FIRST_UPPER ? 'P' : 'p');
        p[k] = (am & IND_SECOND_UPPER ? 'M' : 'm');
      } else { // am
        p[0] = (am & IND_FIRST_UPPER ? 'A' : 'a');
        p[k] = (am & IND_SECOND_UPPER ? 'M' : 'm');
      }
    }
    if (bc && ((p = strstr(formatted, "zz")) != NULL)) {
      if (year == 0) {
         // We don't have it yet!
         if (ksu_extract_int(t, KSU_DATE_PART_YEAR, &val) != 1) {
            val = 0;
         }
         year = (int)val;
      }
      k = 1;  // Position of second letter
      if (bc & IND_DOTS) {
        p[1] = '.';
        p[3] = '.';
        k = 2;
      }
      if (year < 0) { // bc
        p[0] = (bc & IND_FIRST_UPPER ? 'B' : 'b');
        p[k] = (bc & IND_SECOND_UPPER ? 'C' : 'c');
      } else { // ad
        p[0] = (bc & IND_FIRST_UPPER ? 'A' : 'a');
        p[k] = (bc & IND_SECOND_UPPER ? 'D' : 'd');
      }
    }
    return formatted;
}

extern void pg_to_char(sqlite3_context *context,
                       int              argc,
                       sqlite3_value  **argv) {

         char       num = 0;
         char       any_text = 0;
   const char      *fmt;
         char      *rslt;
         char       formatted[FORMAT_LEN];
         int        typ;
         KSU_TIME_T t;

   if (ksu_prm_ok(context, argc, argv, "to_char",
                  KSU_PRM_TEXT, KSU_PRM_TEXT)) {
     ksu_i18n();
     // Can be called for either numbers or dates
     typ = sqlite3_value_type(argv[0]);
     switch (typ) {
         case SQLITE_INTEGER:
         case SQLITE_FLOAT:
              num = 1;
              break;
         case SQLITE_TEXT:
              // Check that it looks like a date
              if (!ksu_is_datetime((const char *)sqlite3_value_text(argv[0]),
                                   &t, (char)0)) {
                any_text = 1; 
              }
              break;
         case SQLITE_NULL:
              sqlite3_result_null(context);
              return;
         default: 
              ksu_err_msg(context, KSU_ERR_N_INV_DATATYPE, 1, "to_char");
              return;
     }
     fmt = (const char *)sqlite3_value_text(argv[1]);
     if (num) {
       rslt = pg_format_number(context, argv[0], fmt, formatted);
     } else {
       rslt = pg_format_date(context, t, fmt, formatted);
     }
     if (!rslt) {
       return;  // Error message set in pg_format_xxxx
     }
     sqlite3_result_text(context, formatted, -1, SQLITE_TRANSIENT);
   }
}
