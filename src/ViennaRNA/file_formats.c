/*
    file_formats.c

    Various functions dealing with file formats for RNA sequences, structures, and alignments

    (c) 2014 Ronny Lorenz

    Vienna RNA package
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/utils.h"
#include "ViennaRNA/constraints.h"
#if WITH_JSON_SUPPORT
# include <json/json.h>
#endif
#include "ViennaRNA/file_formats.h"

/*
#################################
# PRIVATE VARIABLES             #
#################################
*/

PRIVATE char          *inbuf  = NULL;
PRIVATE char          *inbuf2 = NULL;
PRIVATE unsigned int  typebuf = 0;

/*
#################################
# PRIVATE FUNCTION DECLARATIONS #
#################################
*/

PRIVATE void
find_helices(short *pt, int i, int j, FILE *file);

PRIVATE unsigned int
read_multiple_input_lines(char **string, FILE *file, unsigned int option);

PRIVATE void
elim_trailing_ws(char *string);

/*
#################################
# BEGIN OF FUNCTION DEFINITIONS #
#################################
*/

PRIVATE void
elim_trailing_ws(char *string){    /* eliminate whitespaces at the end of a character string */

  int i, l = strlen(string);

  for(i = l-1; i >= 0; i--){
    if      (string[i] == ' ')  continue;
    else if (string[i] == '\t') continue;
    else                        break;
  }
  string[(i >= 0) ? (i+1) : 0] = '\0';
}

PRIVATE void
find_helices(short *pt, int i, int j, FILE *file){

  FILE *out = (file) ? file : stdout;
  int h_start, h_length, h_end;

  h_start = h_length = h_end = 0;

  for(;i < j; i++){
    if(i > pt[i]) continue;
    h_start = i;
    h_end   = pt[i];
    h_length = 1;
    while(pt[i+1] == (pt[i]-1)){
      h_length++;
      i++;
    }
    if(i < h_end){
      find_helices(pt, i+1, h_end, file);
    }
    if(h_length > 1){
      fprintf(out, "%d %d %d\n", h_start, h_end, h_length);
    }
    i = pt[h_start] - 1;
  }
}

PUBLIC void
vrna_structure_print_helix_list(const char *db,
                                FILE *file){

  short *pt = vrna_pt_get(db);

  find_helices(pt, 1, pt[0], file);
  free(pt);
}

PUBLIC void
vrna_structure_print_ct(const char *seq,
                        const char *db,
                        float energy,
                        const char *identifier,
                        FILE *file){

  int i, power_d;
  FILE *out = (file) ? file : stdout;

  if(strlen(seq) != strlen(db))
    vrna_message_error("vrna_structure_print_ct: sequence and structure have unequal length!");

  short *pt = vrna_pt_get(db);

  for(power_d=0;pow(10,power_d) <= (int)strlen(seq);power_d++);

  /*
    Connect table file format looks like this:

    300  ENERGY = 7.0  example
      1 G       0    2   22    1
      2 G       1    3   21    2

    where the headerline is followed by 6 columns with:
    1. Base number: index n
    2. Base (A, C, G, T, U, X)
    3. Index n-1  (0 if first nucleotide)
    4. Index n+1  (0 if last nucleotide)
    5. Number of the base to which n is paired. No pairing is indicated by 0 (zero).
    6. Natural numbering.
  */

  /* print header */
  fprintf(out, "%d  ENERGY = %6.2f", (int)strlen(seq), energy);
  if(identifier)
    fprintf(out, "  %s\n", identifier);

  /* print structure information except for last line */
  /* TODO: modify the structure information for cofold */
  for(i = 0; i < strlen(seq) - 1; i++){
    fprintf(out, "%*d %c %*d %*d %*d %*d\n",
                  power_d, i+1,           /* nucleotide index */
                  (char)toupper(seq[i]),  /* nucleotide char */
                  power_d, i,             /* nucleotide predecessor index */
                  power_d, i+2,           /* nucleotide successor index */
                  power_d, pt[i+1],       /* pairing partner index */
                  power_d, i+1);          /* nucleotide natural numbering */
  }
  /* print last line */
  fprintf(out, "%*d %c %*d %*d %*d %*d\n",
                power_d, i+1,
                (char)toupper(seq[i]),
                power_d, i,
                power_d, 0,
                power_d, pt[i+1],
                power_d, i+1);

  /* clean up */
  free(pt);
  fflush(out);
}

PUBLIC void
vrna_structure_print_bpseq( const char *seq,
                            const char *db,
                            FILE *file){

  int i;
  FILE *out = (file) ? file : stdout;

  if(strlen(seq) != strlen(db))
    vrna_message_error("vrna_structure_print_bpseq: sequence and structure have unequal length!");

  short *pt = vrna_pt_get(db);

  for(i = 1; i <= pt[0]; i++){
    fprintf(out, "%d %c %d\n", i, (char)toupper(seq[i-1]), pt[i]);
  }

  /* clean up */
  free(pt);
  fflush(out);
}

#if WITH_JSON_SUPPORT

PUBLIC void
vrna_structure_print_json(  const char *seq,
                            const char *db,
                            double energy,
                            const char *identifier,
                            FILE *file){

  FILE *out = (file) ? file : stdout;

  JsonNode *data  = json_mkobject();
  JsonNode *value;

  if(identifier){
    value = json_mkstring(identifier);
    json_append_member(data, "id", value);
  }

  value = json_mkstring(seq);
  json_append_member(data, "sequence", value);

  value = json_mknumber(energy);
  json_append_member(data, "mfe", value);

  value = json_mkstring(db);
  json_append_member(data, "structure", value);

  
  fprintf(out, "%s\n", json_stringify(data, "\t"));

  fflush(out);
}

#endif

PRIVATE  unsigned int
read_multiple_input_lines(char **string,
                          FILE *file,
                          unsigned int option){

  char  *line;
  int   i, l;
  int   state = 0;
  int   str_length = 0;
  FILE  *in = (file) ? file : stdin;

  line = (inbuf2) ? inbuf2 : get_line(in);
  inbuf2 = NULL;
  do{

    /*
    * read lines until informative data appears or
    * report an error if anything goes wrong
    */
    if(!line) return VRNA_INPUT_ERROR;

    l = (int)strlen(line);

    /* eliminate whitespaces at the end of the line read */
    if(!(option & VRNA_INPUT_NO_TRUNCATION))
      elim_trailing_ws(line);

    l           = (int)strlen(line);
    str_length  = (*string) ? (int) strlen(*string) : 0;

    switch(*line){
      case  '@':    /* user abort */
                    if(state) inbuf2 = line;
                    else      free(line);
                    return (state==2) ? VRNA_INPUT_CONSTRAINT : (state==1) ? VRNA_INPUT_SEQUENCE : VRNA_INPUT_QUIT;

      case  '\0':   /* empty line */
                    if(option & VRNA_INPUT_NOSKIP_BLANK_LINES){
                      if(state) inbuf2 = line;
                      else      free(line);
                      return (state==2) ? VRNA_INPUT_CONSTRAINT : (state==1) ? VRNA_INPUT_SEQUENCE : VRNA_INPUT_BLANK_LINE;
                    }
                    break;

      case  '#': case  '%': case  ';': case  '/': case  '*': case ' ':
                    /* comments */
                    if(option & VRNA_INPUT_NOSKIP_COMMENTS){
                      if(state) inbuf2   = line;
                      else      *string = line;
                      return (state == 2) ? VRNA_INPUT_CONSTRAINT : (state==1) ? VRNA_INPUT_SEQUENCE : VRNA_INPUT_COMMENT;
                    }
                    break;

      case  '>':    /* fasta header */
                    if(state) inbuf2   = line;
                    else      *string = line;
                    return (state==2) ? VRNA_INPUT_CONSTRAINT : (state==1) ? VRNA_INPUT_SEQUENCE : VRNA_INPUT_FASTA_HEADER;

      case  'x': case 'e': case 'l': case '&':   /* seems to be a constraint or line starting with second sequence for dimer calculations */
                    i = 1;
                    /* lets see if this assumption holds for the complete line */
                    while((line[i] == 'x') || (line[i] == 'e') || (line[i] == 'l')) i++;
                    /* lines solely consisting of 'x's, 'e's or 'l's will be considered as structure constraint */
                    
                    if(
                            ((line[i]>64) && (line[i]<91))  /* A-Z */
                        ||  ((line[i]>96) && (line[i]<123)) /* a-z */
                      ){
                      if(option & VRNA_INPUT_FASTA_HEADER){
                        /* are we in structure mode? Then we remember this line for the next round */
                        if(state == 2){ inbuf2 = line; return VRNA_INPUT_CONSTRAINT;}
                        else{
                          *string = (char *)vrna_realloc(*string, sizeof(char) * (str_length + l + 1));
                          strcpy(*string + str_length, line);
                          state = 1;
                        }
                        break;
                      }
                      /* otherwise return line read */
                      else{ *string = line; return VRNA_INPUT_SEQUENCE;}
                    }
                    /* mmmh? it really seems to be a constraint */
                    /* fallthrough */
      case  '<': case  '.': case  '|': case  '(': case ')': case '[': case ']': case '{': case '}': case ',': case '+':
                    /* seems to be a structure or a constraint */
                    /* either we concatenate this line to one that we read previously */
                    if(option & VRNA_INPUT_FASTA_HEADER){
                      if(state == 1){
                        inbuf2 = line;
                        return VRNA_INPUT_SEQUENCE;
                      }
                      else{
                        *string = (char *)vrna_realloc(*string, sizeof(char) * (str_length + l + 1));
                        strcpy(*string + str_length, line);
                        state = 2;
                      }
                    }
                    /* or we return it as it is */
                    else{
                      *string = line;
                      return VRNA_INPUT_CONSTRAINT;
                    }
                    break;
      default:      if(option & VRNA_INPUT_FASTA_HEADER){
                      /* are we already in sequence mode? */
                      if(state == 2){
                        inbuf2 = line;
                        return VRNA_INPUT_CONSTRAINT;
                      }
                      else{
                        *string = (char *)vrna_realloc(*string, sizeof(char) * (str_length + l + 1));
                        strcpy(*string + str_length, line);
                        state = 1;
                      }
                    }
                    /* otherwise return line read */
                    else{
                      *string = line;
                      return VRNA_INPUT_SEQUENCE;
                    }
    }
    free(line);
    line = get_line(in);
  }while(line);

  return (state==2) ? VRNA_INPUT_CONSTRAINT : (state==1) ? VRNA_INPUT_SEQUENCE : VRNA_INPUT_ERROR;
}

PUBLIC  unsigned int
vrna_read_fasta_record( char **header,
                        char **sequence,
                        char ***rest,
                        FILE *file,
                        unsigned int options){

  unsigned int  input_type, return_type, tmp_type;
  int           rest_count;
  char          *input_string;

  rest_count    = 0;
  return_type   = tmp_type = 0;
  input_string  = *header = *sequence = NULL;
  *rest         = (char **)vrna_alloc(sizeof(char *));

  /* remove unnecessary option flags from options variable... */
  options &= ~VRNA_INPUT_FASTA_HEADER;

  /* read first input or last buffered input */
  if(typebuf){
    input_type    = typebuf;
    input_string  = inbuf;
    typebuf       = 0;
    inbuf         = NULL;
  }
  else input_type  = read_multiple_input_lines(&input_string, file, options);

  if(input_type & (VRNA_INPUT_QUIT | VRNA_INPUT_ERROR)) return input_type;

  /* skip everything until we read either a fasta header or a sequence */
  while(input_type & (VRNA_INPUT_MISC | VRNA_INPUT_CONSTRAINT | VRNA_INPUT_BLANK_LINE)){
    free(input_string); input_string = NULL;
    input_type    = read_multiple_input_lines(&input_string, file, options);
    if(input_type & (VRNA_INPUT_QUIT | VRNA_INPUT_ERROR)) return input_type;
  }

  if(input_type & VRNA_INPUT_FASTA_HEADER){
    return_type  |= VRNA_INPUT_FASTA_HEADER; /* remember that we've read a fasta header */
    *header       = input_string;
    input_string  = NULL;
    /* get next data-block with fasta support if not explicitely forbidden by VRNA_INPUT_NO_SPAN */
    input_type  = read_multiple_input_lines(
                    &input_string,
                    file,
                    ((options & VRNA_INPUT_NO_SPAN) ? 0 : VRNA_INPUT_FASTA_HEADER) | options
                  );
    if(input_type & (VRNA_INPUT_QUIT | VRNA_INPUT_ERROR)) return (return_type | input_type);
  }

  if(input_type & VRNA_INPUT_SEQUENCE){
    return_type  |= VRNA_INPUT_SEQUENCE; /* remember that we've read a sequence */
    *sequence     = input_string;
    input_string  = NULL;
  } else vrna_message_error("sequence input missing");

  /* read the rest until we find user abort, EOF, new sequence or new fasta header */
  if(!(options & VRNA_INPUT_NO_REST)){
    options |= VRNA_INPUT_NOSKIP_COMMENTS; /* allow commetns to appear in rest output */
    tmp_type = VRNA_INPUT_QUIT | VRNA_INPUT_ERROR | VRNA_INPUT_SEQUENCE | VRNA_INPUT_FASTA_HEADER;
    if(options & VRNA_INPUT_NOSKIP_BLANK_LINES) tmp_type |= VRNA_INPUT_BLANK_LINE;
    while(!((input_type = read_multiple_input_lines(&input_string, file, options)) & tmp_type)){
      *rest = vrna_realloc(*rest, sizeof(char **)*(++rest_count + 1));
      (*rest)[rest_count-1] = input_string;
      input_string = NULL;
    }
    /*
    if(input_type & (VRNA_INPUT_QUIT | VRNA_INPUT_ERROR)) return input_type;
    */

    /*  finished reading everything...
    *   we now put the last line into the buffer if necessary
    *   since it should belong to the next record
    */
    inbuf = input_string;
    typebuf = input_type;
  }
  (*rest)[rest_count] = NULL;
  return (return_type);
}

PUBLIC char *
vrna_extract_record_rest_structure( const char **lines,
                                    unsigned int length,
                                    unsigned int options){

  char *structure = NULL;
  int r, i, l, cl, stop;
  char *c;

  if(lines){
    for(r = i = stop = 0; lines[i]; i++){
      l   = (int)strlen(lines[i]);
      c   = (char *) vrna_alloc(sizeof(char) * (l+1));
      (void) sscanf(lines[i], "%s", c);
      cl  = (int)strlen(c);

      /* line commented out ? */
      if((*c == '#') || (*c == '%') || (*c == ';') || (*c == '/') || (*c == '*' || (*c == '\0'))){
        /* skip leading comments only, i.e. do not allow comments inside the constraint */
        if(!r)  continue;
        else    break;
      }

      /* append the structure part to the output */
      r += cl+1;
      structure = (char *)vrna_realloc(structure, r*sizeof(char));
      strcat(structure, c);
      free(c);
      /* stop if the assumed structure length has been reached */
      if((length > 0) && (r-1 == length)) break;
      /* stop if not allowed to read from multiple lines */
      if(!(options & VRNA_OPTION_MULTILINE)) break;
    }
  }
  return structure;
}

PUBLIC void
vrna_extract_record_rest_constraint(char **cstruc,
                                    const char **lines,
                                    unsigned int option){

  int r, i, l, cl, stop;
  char *c, *ptr;
  if(lines){
    if(option & VRNA_CONSTRAINT_ALL)
      option |=   VRNA_CONSTRAINT_PIPE
                | VRNA_CONSTRAINT_ANG_BRACK
                | VRNA_CONSTRAINT_RND_BRACK
                | VRNA_CONSTRAINT_X
                | VRNA_CONSTRAINT_INTRAMOLECULAR
                | VRNA_CONSTRAINT_INTERMOLECULAR;

    for(r=i=stop=0;lines[i];i++){
      l   = (int)strlen(lines[i]);
      c   = (char *) vrna_alloc(sizeof(char) * (l+1));
      (void) sscanf(lines[i], "%s", c);
      cl  = (int)strlen(c);
      /* line commented out ? */
      if((*c == '#') || (*c == '%') || (*c == ';') || (*c == '/') || (*c == '*' || (*c == '\0'))){
        /* skip leading comments only, i.e. do not allow comments inside the constraint */
        if(!r)  continue;
        else    break;
      }

      /* check current line for actual constraining structure */
      for(ptr = c;*c;c++){
        switch(*c){
          case '|':   if(!(option & VRNA_CONSTRAINT_PIPE)){
                        vrna_message_warning("constraints of type '|' not allowed");
                        *c = '.';
                      }
                      break;
          case '<':   
          case '>':   if(!(option & VRNA_CONSTRAINT_ANG_BRACK)){
                        vrna_message_warning("constraints of type '<' or '>' not allowed");
                        *c = '.';
                      }
                      break;
          case '(':
          case ')':   if(!(option & VRNA_CONSTRAINT_RND_BRACK)){
                        vrna_message_warning("constraints of type '(' or ')' not allowed");
                        *c = '.';
                      }
                      break;
          case 'x':   if(!(option & VRNA_CONSTRAINT_X)){
                        vrna_message_warning("constraints of type 'x' not allowed");
                        *c = '.';
                      }
                      break;
          case 'e':   if(!(option & VRNA_CONSTRAINT_INTERMOLECULAR)){
                        vrna_message_warning("constraints of type 'e' not allowed");
                        *c = '.';
                      }
                      break;
          case 'l':   if(!(option & VRNA_CONSTRAINT_INTRAMOLECULAR)){
                        vrna_message_warning("constraints of type 'l' not allowed");
                        *c = '.';
                      }
                      break;  /*only intramolecular basepairing */
          case '.':   break;
          case '&':   break; /* ignore concatenation char */
          default:    vrna_message_warning("unrecognized character in constraint structure");
                      break;
        }
      }

      r += cl+1;
      *cstruc = (char *)vrna_realloc(*cstruc, r*sizeof(char));
      strcat(*cstruc, ptr);
      free(ptr);
      /* stop if not in fasta mode or multiple words on line */
      if(!(option & VRNA_CONSTRAINT_MULTILINE) || (cl != l)) break;
    }
  }
}


PUBLIC int
vrna_read_SHAPE_file( const char *file_name,
                      int length,
                      double default_value,
                      char *sequence,
                      double *values){

  FILE *fp;
  char *line;
  int i;
  int count = 0;

  if(!file_name)
    return 0;

  if(!(fp = fopen(file_name, "r"))){
    vrna_message_warning("SHAPE data file could not be opened");
    return 0;
  }

  for (i = 0; i < length; ++i)
  {
    sequence[i] = 'N';
    values[i + 1] = default_value;
  }

  sequence[length] = '\0';

  while((line=get_line(fp))){
    int position;
    unsigned char nucleotide = 'N';
    double reactivity = default_value;
    char *second_entry = 0;
    char *third_entry = 0;
    char *c;

    if(sscanf(line, "%d", &position) != 1)
    {
      free(line);
      continue;
    }

    if(position <= 0 || position > length)
    {
      vrna_message_warning("Provided SHAPE data outside of sequence scope");
      fclose(fp);
      free(line);
      return 0;
    }

    for(c = line + 1; *c; ++c){
      if(isspace(*(c-1)) && !isspace(*c)) {
        if(!second_entry){
          second_entry = c;
        }else{
          third_entry = c;
          break;
        }
      }
    }

    if(second_entry){
      if(third_entry){
        sscanf(second_entry, "%c", &nucleotide);
        sscanf(third_entry, "%lf", &reactivity);
      }else if(sscanf(second_entry, "%lf", &reactivity) != 1)
        sscanf(second_entry, "%c", &nucleotide);
    }

    sequence[position-1] = nucleotide;
    values[position] = reactivity;
    ++count;

    free(line);
  }

  fclose(fp);

  if(!count)
  {
      vrna_message_warning("SHAPE data file is empty");
      return 0;
  }

  return 1;
}

PRIVATE int
parse_constraints_line( const char *line,
                        char command,
                        int *i,
                        int *j,
                        int *k,
                        int *l,
                        char *loop,
                        char *orientation,
                        float *e){

  int ret = 0;
  int range_mode = 0;
  int pos = 0;
  int max_entries = 5;
  int entries_seen = 0;
  int pp;
  char buf[256], c;

  switch(command){
    case 'F':   /* fall through */
    case 'P':   max_entries = 5;
                break;
    case 'W':   /* fall through */
    case 'U':   max_entries = 3;
                break;
    case 'B':   max_entries = 4;
                break;
    case '#': case ';': case '%': case '/': case ' ':
                ret = 2;  /* comment */
                break;
    default:    ret = 1;  /* error */
                break;
  }

  /* now lets scan the entire line for content */
  while(!ret && (entries_seen < max_entries) && (sscanf(line+pos,"%15s%n", buf, &pp) == 1)){
    pos += pp;
    switch(entries_seen){
      case 0: /* must be i, or range */
              if(sscanf(buf, "%d-%d%n", i, j, &pp) == 2){
                if(pp == strlen(buf)){
                  range_mode = 1;
                  --max_entries; /* no orientation allowed now */
                  break;
                }
              } else if(sscanf(buf, "%d%n", i, &pp) == 1){
                if(pp == strlen(buf)){
                  break;
                }
              }
              ret = 1;
              break;
      case 1: /* must be j, or range */
              if(sscanf(buf, "%d-%d%n", k, l, &pp) == 2){
                if(pp == strlen(buf)){
                  if(!range_mode)
                    --max_entries; /* no orientation allowed now */
                  range_mode = 1;
                  break;
                }
              } else if(range_mode){
                if(sscanf(buf, "%d%n", l, &pp) == 1){
                  if(pp == strlen(buf))
                    break;
                }
              } else if(sscanf(buf, "%d%n", j, &pp) == 1){
                if(pp == strlen(buf))
                  break;
              }
              ret = 1;
              break;
      case 2: /* skip if in range_mode */
              if(!range_mode){
                /* must be k */
                if(sscanf(buf, "%d%n", k, &pp) == 1){
                  if(pp == strlen(buf))
                    break;
                }
                ret = 1;
                break;
              } else {
                --max_entries;
                /* fall through */
              }
      case 3: /*  must be loop type, or orientation */
              if(sscanf(buf, "%c%n", &c, &pp) == 1){
                if(pp == strlen(buf)){
                  if((!range_mode) && ((c == 'U') || (c == 'D'))){
                    *orientation = c;
                    ++entries_seen;
                  } else {
                   *loop = c;
                  }
                  break;
                }
              }
              ret = 1;
              break;
      case 4: /* must be orientation */
              if(!(sscanf(buf, "%c", orientation) == 1)){
                ret = 1;
              }
              break;
    }
    ++entries_seen;
  }

  return ret;
}

PUBLIC  plist *
vrna_read_constraints_file( const char *filename,
                            unsigned int length,
                            unsigned int options){

  FILE  *fp;
  int   line_number, constraint_number, constraint_number_guess;
  char  *line;
  plist *constraints;

  if(!(fp = fopen(filename, "r"))){
    vrna_message_warning("Hard Constraints File could not be opened!");
    return NULL;
  }

  line_number             = 0;
  constraint_number       = 0;
  constraint_number_guess = 10;
  constraints             = (plist *)vrna_alloc(sizeof(plist) * constraint_number_guess);

  while((line=get_line(fp))){

    int i, j, k, l, cnt1, cnt2, cnt3, cnt4, error;
    float e;
    char command, looptype, orientation;

    line_number++; /* increase line number */

    if(sscanf(line, "%c", &command) != 1){
      free(line);
      continue;
    }

    i = j = k = l = -1;
    looptype    = 'A';  /* default to all loop types */
    orientation = '\0'; /* no orientation */
    e = (float)INF;

    error = parse_constraints_line(line + 1, command, &i, &j, &k, &l, &looptype, &orientation, &e);

    if(error == 1){
      fprintf(stderr, "WARNING: Unrecognized constraint command line in input file %s, line %d\n", filename, line_number);
    } else if(error == 0){
      /* do something with the constraint we've just read */
      int type;

      switch(looptype){
        case 'E':   type = (int)VRNA_HC_CONTEXT_EXT_LOOP;
                    break;
        case 'H':   type = (int)VRNA_HC_CONTEXT_HP_LOOP;
                    break;
        case 'I':   type = (int)VRNA_HC_CONTEXT_INT_LOOP;
                    break;
        case 'i':   type = (int)VRNA_HC_CONTEXT_INT_LOOP_ENC;
                    break;
        case 'M':   type = (int)VRNA_HC_CONTEXT_MB_LOOP;
                    break;
        case 'm':   type = (int)VRNA_HC_CONTEXT_MB_LOOP_ENC;
                    break;
        default:    type = (int)VRNA_HC_CONTEXT_ALL_LOOPS;
                    break;
      }
      if(command == 'P'){ /* prohibit */
        type = ~type;
        type &= (int)VRNA_HC_CONTEXT_ALL_LOOPS;
      } else if(command = 'F'){ /* enforce */
        type |= VRNA_HC_CONTEXT_ENFORCE;
      } else if((command == 'U') || (command == 'B')){ /* soft constraints are always applied for all loops */
        type = (int)VRNA_HC_CONTEXT_ALL_LOOPS;
      } else { /* remove conflicts only */
        /*do nothing */
      }

      if(l != -1){  /* we must have read a range */
        if(k != -1){ /* was k-l range */
          if(j != -1){  /* got i-j range too?! */
            if(i != -1){
              /* ok, range i-j is probibited to pair with range k-l */
              if((i < j) && (i < k) && (k < l)){
                for(cnt1 = i; cnt1 <= j; cnt1++)
                  for(cnt2 = k; cnt2 <= l; cnt2++){
                    constraints[constraint_number].i = cnt1;
                    constraints[constraint_number].j = cnt2;
                    constraints[constraint_number].p = 0.;
                    constraints[constraint_number++].type = type;
                    if(constraint_number == constraint_number_guess){
                      constraint_number_guess *= 2;
                      constraints = (plist *)vrna_realloc(constraints, sizeof(plist) * constraint_number_guess);
                    }
                  }
              } else {
                fprintf(stderr, "WARNING: Constraint command has wrong intervals in input file %s, line %d\n", filename, line_number);
              }
            } else {
              /* this is an input error */
            }
          } else if(i != -1){
            /* i must not pair with k-l range */
          } else {
            /* this is an input error */
          }
        } else { /* must have been i-j range with single target l */
          if(j != -1){  /* should be range i-j */
            if(i != -1){
              /* range i-j must not pair with l */
            } else {
              /* this is an input error */
            }
          } else {
            /* this is an input error */
          }
        }
      } else { /* no range k-l, so i, j, and k must not be -1 */
        if((i != -1) && (j != -1) && (k != -1)){
          /* do something here */
        } else {
          /* this is an input error */
        }
      }
#if DEBUG
      printf("Constraint: %c %d %d %d %d %c %c\n", command, i, j, k, l, looptype, orientation);
#endif
    }

    free(line);
  }

  fclose(fp);

  /* resize plist to actual size */
  constraints = (plist *)vrna_realloc(constraints, sizeof(plist) * (constraint_number + 1));

  constraints[constraint_number].i    = 0;
  constraints[constraint_number].j    = 0;
  constraints[constraint_number].p    = 0.;
  constraints[constraint_number].type = 0;

  if(constraint_number == 0){
    vrna_message_warning("Constraints file does not contain any constraints");
  }

  return constraints;
}

#ifdef  VRNA_BACKWARD_COMPAT

/*###########################################*/
/*# deprecated functions below              #*/
/*###########################################*/

PUBLIC unsigned int
get_multi_input_line( char **string,
                      unsigned int option){

  return read_multiple_input_lines(string, NULL, option);
}

PUBLIC unsigned int
read_record(char **header,
            char **sequence,
            char  ***rest,
            unsigned int options){

  return vrna_read_fasta_record(header, sequence, rest, NULL, options);
}

PUBLIC char *
extract_record_rest_structure(const char **lines,
                              unsigned int length,
                              unsigned int options){

  return vrna_extract_record_rest_structure(lines, length, options);
}


#endif
