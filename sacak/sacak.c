/* simple driver to test sacak
   
   support 32/64 bit SA/LCP array (for input larger than 4GB) with different executables
   support 8 bit input (not the full range 0..255) because we need a distinct eof
   support 16 bit input stored in an int_t text array (so the full range 0..65535 is supported)
   support 32 bit input stored in an int_t text array: the 32 bit version support the 
   range 0..2^31-2, while the 64 bit version support the full range 0..2^31-1 
   
   TODO:
     in saca_int the text is stored in a signed array, is that necessary? 
     add support for 16 bit input stored in a uint16_t text array to save space
     add suport for 32 bit input stored in a uint32_t text array when using 64 bit SA/LCP to save space
   */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include <assert.h>
#include "sacak-lcp.h"


typedef unsigned char uchar_t;

#define DBL_CLK_TCK ((double) sysconf(_SC_CLK_TCK)) // clocks x secs 
int Verbose=0;


/* **********************************************************
   open filename and write p[0] .. p[n-1] using 32 bit
   ********************************************************** */
void write_sa(char *filename, uint_t *p, uint_t n)
{
  FILE *sa;

  if(Verbose)
    fprintf(stderr,"Writing sa to file %s\n",filename);
  if((sa=fopen(filename,"wb"))==NULL)  perror(filename);
  size_t c = fwrite(p,sizeof(*p),(size_t)n,sa);
  if(c!=(size_t)n) {
    perror("Error writing the sa file");
    exit(1);
  }  fclose(sa);
}


void write_lcp(char *filename, int_t *p, uint_t n)
{
  FILE *lcp;

  if(Verbose)
    fprintf(stderr,"Writing lcp to file %s\n",filename);
  if((lcp=fopen(filename,"w"))==NULL) perror(filename);
  fwrite(p,sizeof(*p),(size_t)n,lcp);
  if(ferror(lcp)) {
    perror("Error writing the lcp file");
    exit(1);
  }
  fclose(lcp);
}

// read and compact the alphabet, return alphabet size
static uint_t  read_input_bytes(FILE *f, uint_t n, uint8_t *x, const char *fnam)
{
  rewind(f);
  if (fread(x, 1, (size_t)n, f) != (size_t)n) {
    perror(fnam);
    exit(1);
  }
  // check which symbols appear in the input and remap them to a compact alphabet
  unsigned char q[UINT8_MAX + 1] = {0}; // track chars 
  for (uint_t i=0; i < n; ++i) q[x[i]] = 1;
  uint_t kk = 0;
  for (int i = 0; i <= UINT8_MAX; ++i)
    if (q[i]) q[i] = kk++; // assign new compacted symbol values to the chars that appear in the input
  if(kk==UINT8_MAX + 1) {
    fprintf(stderr, "%s: input file contains all symbols in [0,255], not allowed in sacak-uint8\n", fnam);
    exit(1);
  }
  assert(q[UINT8_MAX]<UINT8_MAX);
  for (uint_t i=0; i < n; ++i)
    x[i] = q[x[i]] + 1;
  return kk; 
}


// read and remap alphabet to 1..maxv+1 return new alphabet size
static uint_t read_input_uint16(FILE *f, uint_t n, int_t *x, uint16_t *tmp16, const char *fnam)
{
  rewind(f);
  if (fread(tmp16, sizeof(uint16_t), (size_t)n, f) != (size_t)n) {
    perror(fnam);
    exit(1);
  }
  // compute min and max values
  uint16_t minv = tmp16[0], maxv = tmp16[0];
  for (uint_t ii = 1; ii < n; ii++) {
    if (tmp16[ii] < minv) minv = tmp16[ii];
    if (tmp16[ii] > maxv) maxv = tmp16[ii];
  }
  // copy and remap text 
  for (uint_t ii = 0; ii < n; ii++) {
    x[ii] = ((int_t) tmp16[ii]) - minv + 1; // remap to 1..maxv-minv+1
    assert(x[ii] > 0 && x[ii] <= (int_t)(maxv - minv) + 1);
  }
  return (uint_t)(maxv - minv + 2); 
}

// read and remap alphabet to 1..maxv+1, return new alphabet size 
static uint_t read_input_int32(FILE *f, uint_t n, int_t *x, int32_t *tmp32, const char *fnam)
{
  rewind(f);
  if (fread(tmp32, sizeof(uint32_t), (size_t)n, f) != (size_t)n) {
    perror(fnam);
    exit(1);
  }
  // compute min and max values
  int32_t minv = tmp32[0], maxv = tmp32[0];
  for (uint_t ii = 1; ii < n; ii++) {
    if (tmp32[ii] < minv) minv = tmp32[ii];
    if (tmp32[ii] > maxv) maxv = tmp32[ii];
  }
  if(minv < 0) {
    fprintf(stderr, "%s: input file contains a value larger than INT32_MAX\n", fnam);
    exit(1);
  }
  uint_t range = (uint_t)(maxv - minv) + 1;
  if(range > (uint_t)I_MAX) {
    fprintf(stderr, "%s: input file contains a range of symbols larger than INT32_MAX, use -DM64\n", fnam);
    // note: it is bad that -D64 forces the input to be stored in an unint64_t array but here we take advantage of this
    exit(1);
  }
  // copy and remap text 
  for (uint_t ii = 0; ii < n; ii++) {
    x[ii] = ((int_t) tmp32[ii]) - minv + 1; // remap to 1..maxv-minv+1
    assert(x[ii] > 0 && x[ii] <= range);
  }
  return range+1; 
}


int main(int argc, char *argv[])
{
  void *x;
  uint_t  *p, n;
  int_t *lcp= NULL;
  int c, e;
  int input_is_16bit = 0;
  int input_is_int = 0;
   clock_t end_time,start_time;
   struct tms en, st;   
   double tot_time;
   extern char *optarg;
   extern int optind, optopt;
   char *fnam;
   FILE *f;

  /* ------------ set default values ------------- */
  char *sa_filename = NULL;
  char *lcp_filename = NULL;
  int compute_avg_lcp = 0;

  /* ------------- read options from command line ----------- */
  while ((c=getopt(argc, argv, "vw:W:axi")) != -1) {
    switch (c) 
      {
      case 'w':
        sa_filename = optarg; break;
      case 'W':
        lcp_filename = optarg; break;
      case 'x':
        input_is_16bit = 1; break;
      case 'i':
        input_is_int = 1; break;
      case 'v':
        Verbose++; break;
      case 'a':
        compute_avg_lcp = 1; break;
      case '?':
        fprintf(stderr,"Unknown option: %c -main-\n", optopt);
        exit(1);
      }
  }
  if(optind<argc)
    fnam=argv[optind];
  else {
    fprintf(stderr, "Usage:\n\t%s [-w safile][-W lcpfile][-x][-i][-v][-a] file\n\n",argv[0]);
    fprintf(stderr,"\t-w safile   write sa to safile\n");    
    fprintf(stderr,"\t-W lcpfile  write lcp to lcpfile\n");
    fprintf(stderr,"\t-x          read input as sequence of uint16_t values\n");
    fprintf(stderr,"\t-i          read input as sequence of positive int32_t values\n");
    fprintf(stderr,"\t-a          compute and print average LCP value\n"); 
    fprintf(stderr,"\t-v          produces a verbose output\n\n");
    return 0;
  }
  if(Verbose) {
    fprintf(stderr,"Command line: ");
    for(c=0;c<argc;c++)
      fprintf(stderr,"%s ",argv[c]);
    fprintf(stderr,"\n");
  }
  if(input_is_16bit && input_is_int) {
    fprintf(stderr,"Error: cannot use both -x and -i options together\n");
    return 1;
  }
  if(Verbose>1) 
    fprintf(stderr,"Alphabet type: %s\n", input_is_int ? "int32" : (input_is_16bit ? "uint16" : "uint8"));

  // read size and adjust it 
   if (! (f=fopen(fnam, "rb"))) {
      perror(fnam);
      return 1;
   }
   if (fseeko(f, 0L, SEEK_END)) {
      perror(fnam);
      return 1;
   }
   n=ftello(f);
   if(input_is_16bit==1) {
      if(n%2!=0) { fprintf(stderr, "%s: file size not a multiple of 2 (uint16 mode)\n", fnam); return 1; }
      n /= 2;
   } else if(input_is_int==2) {
      if(n%4!=0) { fprintf(stderr, "%s: file size not a multiple of 4 (int32 mode)\n", fnam); return 1; }
      n /= 4;
   }
   if (n==0) {
      fprintf(stderr, "%s: file empty\n", fnam);
      return 0;
   }
   // allocation 
   p=malloc((size_t) (n+1)*sizeof *p);
   if(input_is_16bit || input_is_int) {
     x=malloc((size_t) (n+1)*sizeof(int_t));
   } else {
     x=malloc((size_t) (n+1)*sizeof(uchar_t));
   }
   if (!p || !x) {
      fprintf(stderr, "malloc failed\n");
      fclose(f); return 1;
   }

  /* handle different input formats */
  uint_t alpha_size = 0; // only used for sacak_int and sacak_lcp_int
  if (input_is_16bit) 
    alpha_size = read_input_uint16(f, n, (int_t *)x, (uint16_t *) p, fnam);
  else if (input_is_int)
    alpha_size = read_input_int32(f, n, (int_t *)x, (int32_t *) p, fnam); 
  else 
    alpha_size = read_input_bytes(f,n, (uint8_t *) x, fnam);
  fclose(f);
  if(Verbose) fprintf(stderr,"Alphabet size: %lld\n", (long long) alpha_size);
   
  /* ---------  start measuring time ------------- */
  start_time = times(&st);
  if(lcp_filename!=NULL || compute_avg_lcp) {
    lcp=malloc((size_t) (n+1)*sizeof *lcp);
    if(!lcp) {
      fprintf(stderr, "malloc failed\n");
      free(x); free(p);
      return 1;
    }
  }
  else lcp=NULL; // no lcp requested 
    
  if (input_is_int || input_is_16bit) {
    int_t *s = (int_t *) x;
    s[n]=0; // sentinel
    if(lcp) e = sacak_lcp_int(s, p, lcp, n+1, alpha_size);
    else    e = sacak_int(s, p, n+1, alpha_size);  
  } 
  else {
    unsigned char *s = (unsigned char *) x;
    s[n]=0; // sentinel
    if(lcp)  e = sacak_lcp(s, p, lcp, n+1);
    else     e = sacak(s, p, n+1);
  }

  if(e<0) {
    fprintf(stderr,"Error: sacak returned %d\n", e);
    return 1;
  }
  end_time = times(&en);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  if(Verbose>1) 
    fprintf(stderr,"Elapsed time: %f seconds %s \n", tot_time, lcp==NULL ? " (SA)" : " (SA+LCP)");
  
  // --------------- write sa to a file 
  if(sa_filename!=NULL) 
    write_sa(sa_filename,p+1,n); // discard first position since it is the suffix starting at the end of the string

  // --------------- write lcp to a file 
  if(lcp!=NULL) {
    if(lcp_filename!=NULL)
      write_lcp(lcp_filename, lcp+1, n);

    if(compute_avg_lcp) {
      long long psum = 0;
      double sum = 0;
      for(uint_t i = 1; i <= n; i++) {
        psum += lcp[i];
        if(psum > (1L << 30)) {sum += psum; psum=0;} // avoid overflow
      }
      sum += psum;
      printf("Average LCP: %f\n", sum / n);
    }
    free(lcp); 
  }
  // free memory
  free(x); free(p);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  printf("Elapsed time: %f seconds.\n", tot_time);
  return 0;
}
