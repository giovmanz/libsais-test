/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   
   Test suffix sorting and LCP building with libsais
      
   Reads a sequence of unsigned bytes/short/int from a file
   and compute SA and LCP array using the sais+plcp (from libsais) algorithm  

   Note: this implementationcan work with 32 bits for inputs of size ar most 2**31-1, 
   for larger files we need to switch to 64 bits (for both SA and LCP)
   Space usage 
     8bit input: T + SA + LCP: 9n bytes 32bit, 17n 64bit
    16bit input: T + SA + LCP: 10n bytes 32bit, 18n 64bit 
    32bit input: T + SA + LCP: 12n bytes 32bit
    The 64 bit version store the text in a 64bit array so space usage is 24n
    The 64 bit version does not support the computation of the LCP array for 32bit input
    Still need to explore the effect of the alphabet size on the space usage
   Runinng time: truly linear 
   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
#define _GNU_SOURCE
#define LIBSAIS_OPENMP
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <sys/times.h>
#include "include/libsais.h"
#include "include/libsais64.h"
#include "include/libsais16.h"
#include "include/libsais16x64.h"

// constant to determine when the sa extra space is significant 
// and should returned to the system via realloc (see lcp computation)
// significant here means > n/SA_EXTRA_SPACE_FACTOR 
#define SA_EXTRA_SPACE_FACTOR 8

typedef uint8_t  uchar_t;

#ifdef USE_INT64
typedef int64_t  idx_t;
#else
typedef int32_t  idx_t;
#endif

#define DBL_CLK_TCK ((double) sysconf(_SC_CLK_TCK)) // clocks x secs 
int Verbose=0;

static void quit(const char *msg) {
  perror(msg);
  exit(1);
}

/* **********************************************************
   open filename and write p[0] .. p[n-1] using 32 bit
   ********************************************************** */
void write_sa(char *filename, idx_t *p, idx_t n)
{
  FILE *sa;

  if(Verbose)
    fprintf(stderr,"Writing sa to file %s\n",filename);
  if((sa=fopen(filename,"wb"))==NULL)  perror(filename);
  size_t c = fwrite(p,sizeof(*p),(size_t)n,sa);
  if(c!=(size_t)n) quit("Error writing the sa file");
  fclose(sa);
}


void write_lcp(char *filename, idx_t *p, idx_t n)
{
  FILE *lcp;

  if(Verbose)
    fprintf(stderr,"Writing lcp to file %s\n",filename);
  if((lcp=fopen(filename,"w"))==NULL) perror(filename);
  fwrite(p,sizeof(*p),(size_t)n,lcp);
  if(ferror(lcp)) {
    quit("Error writing the lcp file");
  }
  fclose(lcp);
}

// read and remap alphabet to 1..maxv+1, return new alphabet size 
static idx_t read_input_uint32(FILE *f, idx_t n, void *xv, const char *fnam)
{
  
  #ifdef USE_INT64
  uint32_t *tmp32 = ((uint32_t *) xv) + n; // tmp32 is the second half of xv 
  #else
  uint32_t *tmp32 = (uint32_t *) xv;       // tmp32 is xv
  #endif 
  
  // read on the temp buffer
  if (fread(tmp32, sizeof(uint32_t), (size_t)n, f) != (size_t)n)
    quit(fnam);
  // compute min and max values
  uint32_t minv = tmp32[0], maxv = tmp32[0];
  for (idx_t ii = 1; ii < n; ii++) {
    if (tmp32[ii] < minv) minv = tmp32[ii];
    if (tmp32[ii] > maxv) maxv = tmp32[ii];
  }
  idx_t alpha_size= (idx_t) maxv - minv + 1; // alphabet remapped to 0 maxv-minv
  if(maxv > INT32_MAX) 
    fprintf(stderr, "%s WARNING: input file contains a value larger than INT32_MAX\n", fnam);

  #ifndef USE_INT64
  if(alpha_size > (int64_t) INT32_MAX + 1) {
    fprintf(stderr, "%s: input file contains an alphabet larger than INT32_MAX+1, use 64 bit version\n", fnam);
    // note: it is bad that libsais64_long forces the input to be stored in an int64_t array but we take advantage of this to handle maxv
    exit(1);
  }
  #endif
  // copy and remap text
  #ifdef USE_INT64
  int64_t *x = (int64_t *) xv;
  #else 
  int32_t *x = (int32_t *) xv;
  #endif
  for (idx_t ii = 0; ii < n; ii++) {
    x[ii] = tmp32[ii] - minv; // remap to 0..maxv-minv
    assert(x[ii] < alpha_size && x[ii]>=0);
  }
  // compute alphabet size = maxv-minv+1, overflow is not possible since for 32 bit we checked that maxv-minv < INT32_MAX
  return alpha_size;
}


int main(int argc, char *argv[])
{
  void *x;
  idx_t  *p;
  int c;
  clock_t end_time,start_time;
  struct tms en, st;   
  double tot_time;
  extern char *optarg;
  extern int optind, optopt;
  char *fnam;
  FILE *f;
  int input_is_16bit = 0, input_is_int32 = 0, num_threads = 0;
  int sa_extra_space = 0;
  int alpha_multiplier =0; // multiplier (x alpha size) to define sa_extra space  

  /* ------------ set default values ------------- */
  char *sa_filename = NULL;
  char *lcp_filename = NULL;
  int compute_avg_lcp = 0;

  /* ------------- read options from command line ----------- */
  while ((c=getopt(argc, argv, "vw:W:axit:M:")) != -1) {
    switch (c)
      {
      case 'w':
        sa_filename = optarg; break;
      case 'W':
        lcp_filename = optarg; break;
      case 'x':
        input_is_16bit = 1; break;
      case 'i':
        input_is_int32 = 1; break;
      case 't':
        num_threads = atoi(optarg); break;
      case 'v':
        Verbose++; break;
      case 'a':
        compute_avg_lcp = 1; break;
      case 'M':
        alpha_multiplier = atoi(optarg); break;
      case '?':
        fprintf(stderr,"Unknown option: %c -main-\n", optopt);
        exit(1);
      }
  }
  if(input_is_16bit && input_is_int32) {
    fprintf(stderr, "Error: -x and -i are mutually exclusive\n");
    exit(1);
  }
  if(optind<argc)
    fnam=argv[optind];
  else {
    fprintf(stderr, "Usage:\n\t%s [-w safile][-W lcpfile][-x|-i][-v][-a] file\n\n",argv[0]);
    fprintf(stderr,"\t-w safile   write sa to safile\n");
    fprintf(stderr,"\t-W lcpfile  write lcp to lcpfile\n");
    fprintf(stderr,"\t-t threads  # helper threads [def. don't use omp functions]\n");
    fprintf(stderr,"\t-M alphamul alphabet multiplier for extra sa space\n");    
    fprintf(stderr,"\t-x          read input as sequence of uint16_t values\n");
    fprintf(stderr,"\t-i          read input as sequence of int32_t values (integer alphabet)\n");
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
  if(Verbose>1)
    fprintf(stderr,"Alphabet type: %s\n", (input_is_16bit ? "uint16" : (input_is_int32 ? "int32" : "uint8")));

  // read size and adjust it  
  if (! (f=fopen(fnam, "rb"))) quit(fnam);
  if (fseeko(f, 0L, SEEK_END)) quit(fnam);
  off_t n=ftello(f);
  if(input_is_16bit) {
    if(n%2!=0) { fprintf(stderr, "%s: file size not a multiple of 2 (uint16 mode)\n", fnam); return 1; }
    n /= 2;
  } else if(input_is_int32) {
    if(n%4!=0) { fprintf(stderr, "%s: file size not a multiple of 4 (int32 mode)\n", fnam); return 1; }
    n /= 4;
  }
  if (n==0) {
    fprintf(stderr, "%s: file empty\n", fnam);
    return 0;
  }
  if(Verbose>1) fprintf(stderr,"Input size: %lld\n", (long long) n);
  #ifndef USE_INT64
  if(n > INT32_MAX) {
    fprintf(stderr, "%s: input file too large for 32 bit version, use 64 bit version\n", fnam);
    return 1;
  }
  #endif

  // allocate text: n symbols of different kinds
  if(input_is_16bit) 
   x=malloc((size_t) n*sizeof(uint16_t));
  else if(input_is_int32) 
   x=malloc((size_t) n*sizeof(idx_t)); // wasted space if USE_INT64  but we need to widen the input for libsais64_long()
  else 
   x=malloc((size_t) n*sizeof(uint8_t));
  if (! x) quit("malloc failed");

  // read text 
  rewind(f);
  idx_t alpha_size = 0;  // alphabet size: exact for integer input, 2^8 or 2^16 for 8/16 bit
  if(input_is_16bit) { // uint16 input 
   if(fread(x, sizeof(uint16_t), (size_t)n, f)!=(size_t)n)
     quit("error reading input file (16 bit)");
   alpha_size = 65536;
  }
  else if(input_is_int32) {
    // read input and get alpha size 
    alpha_size = read_input_uint32(f, n, x, fnam); 
    if(Verbose>1) fprintf(stderr,"Alphabet size: %lld\n", (long long) alpha_size);
  }
  else { // uint8 input
   if(fread(x, sizeof(uint8_t), (size_t)n, f)!=(size_t)n) 
      quit("error reading input file (8 bit)");
   alpha_size = 256;
  }
  // compute extra space and allocate SA
  long extra_space_l = (long) alpha_multiplier *alpha_size; 
  sa_extra_space =  (extra_space_l > INT32_MAX) ? INT32_MAX : (int32_t) extra_space_l;
  if(Verbose>1) fprintf(stderr,"SA extra space: %d\n", sa_extra_space);
  p=malloc(((size_t) n + sa_extra_space) *sizeof *p);
  if (! p) quit("malloc failed");
  
  /* ---------  start measuring time ------------- */
  start_time = times(&st);
  int32_t e;
  if (input_is_int32) {
    #ifdef USE_INT64
    if(num_threads==0) e = (int32_t) libsais64_long((idx_t *)x, p, n, alpha_size, sa_extra_space);
    else e = (int32_t) libsais64_long_omp((idx_t *) x, p, n, alpha_size, sa_extra_space, num_threads);
    #else
    if(num_threads==0) e = libsais_int((int32_t *)x, p, n, alpha_size, sa_extra_space);
    else e = libsais_int_omp((int32_t *)x, p, n, alpha_size, sa_extra_space, num_threads);
    #endif
  } else if (input_is_16bit) {
    #ifdef USE_INT64
    if(num_threads==0) e = libsais16x64((const uint16_t *)x, p, n, sa_extra_space, NULL);
    else e = libsais16x64_omp((const uint16_t *)x, p, n, sa_extra_space, NULL, num_threads);
    #else
    if(num_threads==0) e = libsais16((const uint16_t *)x, p, n, sa_extra_space, NULL);
    else e = libsais16_omp((const uint16_t *)x, p, n, sa_extra_space, NULL, num_threads);
    #endif
  } else { // 8bit input
    #ifdef USE_INT64
    if(num_threads==0) e = libsais64((const uint8_t *)x, p, n, sa_extra_space, NULL);
    else e = libsais64_omp((const uint8_t *)x, p, n, sa_extra_space, NULL,num_threads);
    #else
    if(num_threads==0)   e = libsais((const uint8_t *)x, p, n, sa_extra_space, NULL);
    else e = libsais_omp((const uint8_t *)x, p, n, sa_extra_space, NULL,num_threads);
    #endif
  }
  if(e<0) {
    fprintf(stderr,"Error: suffix array construction returned %d\n", e);
    free(x); free(p);
    return 1;
  }
  end_time = times(&en);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  if(Verbose) fprintf(stderr,"Elapsed time: %f seconds (SA)\n", tot_time);
  
  // --------------- write sa to a file 
  if(sa_filename!=NULL) 
    write_sa(sa_filename,p,n); // discard first position since it is the suffix starting at the end of the string

  // --------------- compute lcp and write it to a file or compute average
  if(lcp_filename!=NULL || compute_avg_lcp) {
    if(sa_extra_space>n/SA_EXTRA_SPACE_FACTOR) {  // recover extra space if it was significant as defined by SA_EXTRA_SPACE_FACTOR   
      p = realloc(p, (size_t) n*sizeof *p);       // p was the last allocated array, realloc should be cheap
      if (! p) quit("realloc failed");
    }
    clock_t lcp_time = times(&st); 
    idx_t *plcp=malloc((n+1)*sizeof *plcp);
    if(!plcp) {
      fprintf(stderr, "malloc failed\n");
      return 1;
    }
    if(Verbose>1) fprintf(stderr,"Computing lcp array\n");
    if (input_is_int32) {
      #ifdef USE_INT64
      fprintf(stderr, "Error: LCP computation for int32 input is not supported when 64-bit "
                       "indices are required (n > 2^31-1) - libsais64.h has no plcp function "
                       "for integer-alphabet input.\n");
      free(x); free(p); free(plcp);
      return 1;
      #else
      if(num_threads==0) e = libsais_plcp_int((const int32_t *)x, p, plcp, n);
      else e = libsais_plcp_int_omp((const int32_t *)x, p, plcp, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais_plcp_int?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      if(num_threads==0) e = libsais_lcp(plcp, p, p, n);
      else e = libsais_lcp_omp(plcp, p, p, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais_lcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #endif
    }
    else if (input_is_16bit) {
      #ifdef USE_INT64
      if(num_threads==0) e = libsais16x64_plcp((const uint16_t *)x, p, plcp, n);
      else e = libsais16x64_plcp_omp((const uint16_t *)x, p, plcp, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais16x64_plcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      if(num_threads==0) e = libsais16x64_lcp(plcp, p, p, n);
      else e = libsais16x64_lcp_omp(plcp, p, p, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais16x64_lcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #else
      if(num_threads==0) e = libsais16_plcp((const uint16_t *)x, p, plcp, n);
      else e = libsais16_plcp_omp((const uint16_t *)x, p, plcp, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais16_plcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      if(num_threads==0) e = libsais16_lcp(plcp, p, p, n);
      else e = libsais16_lcp_omp(plcp, p, p, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais16_lcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #endif
    } 
    else { // input is 8 bit
      #ifdef USE_INT64
      if(num_threads==0) e = libsais64_plcp((const uint8_t *)x, p, plcp, n);
      else e = libsais64_plcp_omp((const uint8_t *)x, p, plcp, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais64_plcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      if(num_threads==0) e = libsais64_lcp(plcp, p, p, n);
      else e = libsais64_lcp_omp(plcp, p, p, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais64_lcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #else
      if(num_threads==0) e = libsais_plcp((const uint8_t *)x, p, plcp, n);
      else e = libsais_plcp_omp((const uint8_t *)x, p, plcp, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais_plcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      if(num_threads==0) e = libsais_lcp(plcp, p, p, n);
      else e = libsais_lcp_omp(plcp, p, p, n, num_threads);
      if(e<0) {fprintf(stderr,"Error: libsais_lcp?omp? returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #endif
    }
    end_time = times(&en);
    tot_time =  (end_time - lcp_time)/DBL_CLK_TCK;
    if(Verbose) fprintf(stderr,"Elapsed time: %f seconds (LCP)\n", tot_time);
    
    if(lcp_filename!=NULL)
      write_lcp(lcp_filename, p, n);
    
    if(compute_avg_lcp) {
      long long psum = 0;
      double sum = 0;
      for(idx_t i = 0; i < n; i++) {
        psum += p[i];
        if(psum > (1L << 30)) {sum += psum; psum=0;} // avoid overflow
      }
      sum += psum;
      printf("Average LCP: %f\n", sum / n);
    }
    
    free(plcp); 
  }
  // now fnam is no longer needed
  fclose(f);
  // free memory
  free(x); free(p);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  printf("Elapsed time: %f seconds\n", tot_time);
  return 0;
}
