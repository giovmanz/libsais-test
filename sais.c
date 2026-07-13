/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   
   Test suffix sorting and LCP building with libsais
      
   Reads a sequence of unsigned bytes/short/int from
   a file and compute SA and LCP array
   here using the sais+plcp (from libsais) algorithm  

   Note This implementationcan work with 32 bits for inputs of size ar most 2**31-1, 
   for larger files we need to switch to 64 bits (for both SA and LCP)
   Space usage 
     8bit input:  T + SA + LCP: 9n bytes 32bit, 17n 64bit
    16bit input: T + SA + LCP: 10n bytes 32bit, 18n 64bit (to be ckecked)
   Runinng time: truly linear 
   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
#define _GNU_SOURCE
#define LIBSAIS_OPENMP
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include "include/libsais.h"
#include "include/libsais64.h"
#include "include/libsais16.h"
#include "include/libsais16x64.h"


typedef uint8_t  uchar_t;

#ifdef USE_INT64
typedef int64_t  idx_t;
#else
typedef int32_t  idx_t;
#endif


#define DBL_CLK_TCK ((double) sysconf(_SC_CLK_TCK)) // clocks x secs 
int Verbose=0;

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
  if(c!=(size_t)n) {
    perror("Error writing the sa file");
    exit(1);
  }  fclose(sa);
}


void write_lcp(char *filename, idx_t *p, idx_t n)
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

// read and remap alphabet to 1..maxv+1, return new alphabet size 
static idx_t read_input_int32(FILE *f, idx_t n, void *x, int32_t *tmp32, const char *fnam)
{
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
   idx_t  *p, n;
   int c;
   clock_t end_time,start_time;
   struct tms en, st;   
   double tot_time;
   extern char *optarg;
   extern int optind, optopt;
   char *fnam;
   FILE *f;
   int input_is_16bit = 0, input_is_int32 = 0, num_threads = 0;
   int sa_extra_space = 6*1024; // as suggested in libsais64_long() for optimal performance with integer alphabet

  /* ------------ set default values ------------- */
  char *sa_filename = NULL;
  char *lcp_filename = NULL;
  int compute_avg_lcp = 0;

  /* ------------- read options from command line ----------- */
  while ((c=getopt(argc, argv, "vw:W:axit:")) != -1) {
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
      case '?':
        fprintf(stderr,"Unknown option: %c -main-\n", optopt);
        exit(1);
      }
  }
  // printf("Verbose: %d, Helper threads: %d, Group threshold: %d, MKq threshold: %d\n", Verbose, NumTreads, GrpThresh, MkqThresh);
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

   if (! (f=fopen(fnam, "rb"))) {
      perror(fnam);
      return 1;
   }
   if (fseeko(f, 0L, SEEK_END)) {
      perror(fnam);
      return 1;
   }
   n=ftello(f);
   if (n==0) {
      fprintf(stderr, "%s: file empty\n", fnam);
      return 0;
   }
   if (input_is_16bit && n % 2 != 0) {
      fprintf(stderr, "%s: input file size is not a multiple of 2 bytes\n", fnam);
      return 1;
   }
   if (input_is_int32 && n % 4 != 0) {
      fprintf(stderr, "%s: input file size is not a multiple of 4 bytes\n", fnam);
      return 1;
   }
   // allocate space for SA
   p=malloc((size_t) (n+sa_extra_space)*sizeof *p);
   if (! p) {
      fprintf(stderr, "malloc failed\n");
      return 1;
   }
   // allocate text, a number of bytes equal to the file length
   x=malloc((size_t) n*sizeof(uint8_t));
   if (! x) {
      fprintf(stderr, "malloc failed\n");
      free(p); 
      return 1;
   }
   // read text 
   rewind(f);
   idx_t k = 0;  // alphabet size for integer input
   if(input_is_int32) 
     k = read_input_int32(f, n, x, (int32_t *) p, fnam); // use p as a temporary buffer
   else { // uint8 or uint16 input 
     if(fread(x, sizeof(uint8_t), (size_t)n, f)!=(size_t)n) {
         perror(fnam);free(x); free(p);
         return 1;
     }
   }
   if(input_is_16bit) n /= 2;
   else if(input_is_int32) n /= 4;

   // for integer alphabet input, determine k = alphabet size (max value + 1)
   idx_t int32_k = 0;
   if (input_is_int32) {
     int32_t *xi = (int32_t *)x;
     for (idx_t i = 0; i < n; i++) {
       if (xi[i] < 0) {
         fprintf(stderr, "%s: int32 input must contain only non-negative values (symbol %lld at position %lld)\n",
                 fnam, (long long)xi[i], (long long)i);  free(x); free(p);
         return 1;
       }
       if ((idx_t)xi[i] + 1 > int32_k) int32_k = (idx_t)xi[i] + 1;
     }
     if(Verbose>1) fprintf(stderr,"Integer alphabet size (k): %lld\n", (long long)int32_k);
   }

  /* ---------  start measuring time ------------- */
  start_time = times(&st);
  int32_t e;
  if (input_is_int32) {
    #ifdef USE_INT64
    // libsais64.h only offers libsais64_long/libsais64_long_omp for integer
    // alphabets, which require a widened int64_t copy of T
    int64_t *T64 = malloc((size_t)(n+sa_extra_space)*sizeof(int64_t));
    if (!T64) {
      fprintf(stderr, "malloc failed\n");
      free(x); free(p);
      return 1;
    }
    int32_t *xi = (int32_t *)x;
    for (idx_t i = 0; i < n; i++) T64[i] = xi[i];
    if(num_threads==0) e = (int32_t) libsais64_long(T64, p, n, int32_k, sa_extra_space);
    else e = (int32_t) libsais64_long_omp(T64, p, n, int32_k, sa_extra_space, num_threads);
    free(T64);
    #else
    if(num_threads==0) e = libsais_int((int32_t *)x, p, n, int32_k, extra_space);
    else e = libsais_int_omp((int32_t *)x, p, n, int32_k, extra_space, num_threads);
    #endif
  } else if (input_is_16bit) {
    #ifdef USE_INT64
    if(num_threads==0) e = libsais16x64((const uint16_t *)x, p, n, sa_extra_space, NULL);
    else e = libsais16x64_omp((const uint16_t *)x, p, n, sa_extra_space, NULL, num_threads);
    #else
    if(num_threads==0) e = libsais16((const uint16_t *)x, p, n, extra_space, NULL);
    else e = libsais16_omp((const uint16_t *)x, p, n, extra_space, NULL, num_threads);
    #endif
  } else { // 8bit input
    #ifdef USE_INT64
    if(num_threads==0) e = libsais64((const uint8_t *)x, p, n, sa_extra_space, NULL);
    else e = libsais64_omp((const uint8_t *)x, p, n, sa_extra_space, NULL,num_threads);
    #else
    if(num_threads==0)   e = libsais((const uint8_t *)x, p, n, extra_space, NULL);
    else e = libsais_omp((const uint8_t *)x, p, n, extra_space, NULL,num_threads);
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
