/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   
   Program to test suffix sorting and LCP building  
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

#ifdef USE_INT64
int64_t libsais64(const uint8_t * T, int64_t * SA, int64_t n, int64_t fs, int64_t * freq);
int64_t libsais64_plcp(const uint8_t * T, const int64_t * SA, int64_t * PLCP, int64_t n);
int64_t libsais64_lcp(const int64_t * PLCP, const int64_t * SA, int64_t * LCP, int64_t n);
int64_t libsais16x64(const uint16_t * T, int64_t * SA, int64_t n, int64_t fs, int64_t * freq);
int64_t libsais16x64_plcp(const uint16_t * T, const int64_t * SA, int64_t * PLCP, int64_t n);
int64_t libsais16x64_lcp(const int64_t * PLCP, const int64_t * SA, int64_t * LCP, int64_t n);
#else
int32_t libsais(const uint8_t * T, int32_t * SA, int32_t n, int32_t fs, int32_t * freq);
int32_t libsais_plcp(const uint8_t * T, const int32_t * SA, int32_t * PLCP, int32_t n);
int32_t libsais_lcp(const int32_t * PLCP, const int32_t * SA, int32_t * LCP, int32_t n);
int32_t libsais16(const uint16_t * T, int32_t * SA, int32_t n, int32_t fs, int32_t * freq);
int32_t libsais16_plcp(const uint16_t * T, const int32_t * SA, int32_t * PLCP, int32_t n);
int32_t libsais16_lcp(const int32_t * PLCP, const int32_t * SA, int32_t * LCP, int32_t n);
#endif

#define DBL_CLK_TCK ((double) sysconf(_SC_CLK_TCK)) // clocks x secs 
#define MIN(a, b) ((a)<=(b) ? (a) : (b))
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
   int input_is_16bit = 0;

  /* ------------ set default values ------------- */
  char *sa_filename = NULL;
  char *lcp_filename = NULL;
  int compute_avg_lcp = 0;

  /* ------------- read options from command line ----------- */
  while ((c=getopt(argc, argv, "vw:W:ax")) != -1) {
    switch (c) 
      {
      case 'w':
        sa_filename = optarg; break;
      case 'W':
        lcp_filename = optarg; break;
      case 'x':
        input_is_16bit = 1; break;
      // case 't':
      //   NumTreads = atoi(optarg); break;
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
  if(optind<argc)
    fnam=argv[optind];
  else {
    fprintf(stderr, "Usage:\n\t%s [-w safile][-W lcpfile][-x][-v][-a] file\n\n",argv[0]);
    fprintf(stderr,"\t-w safile   write sa to safile\n");    
    fprintf(stderr,"\t-W lcpfile  write lcp to lcpfile\n");
    fprintf(stderr,"\t-x          read input as sequence of uint16_t values\n");
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
   // allocate space for SA
   p=malloc((size_t) (n+1)*sizeof *p);
   if (! p) {
      fprintf(stderr, "malloc failed\n");
      return 1;
   }
   // allocate and read text with space for one extra symbol
   if (input_is_16bit) x=malloc((size_t) (n/2 + 1)*sizeof(uint16_t));
   else x=malloc((size_t) (n+1)*sizeof(uint8_t));
   if (! x) {
      fprintf(stderr, "malloc failed\n");
      free(p);
      return 1;
   }
   // read text
   rewind(f);
   if(fread(x, sizeof(uint8_t), (size_t)n, f)!=(size_t)n) {
       perror(fnam);free(x); free(p);
       return 1;
   }
   if(input_is_16bit) n /= 2;

  /* ---------  start measuring time ------------- */
  start_time = times(&st);
  int32_t e;
  if (input_is_16bit) {
    #ifdef USE_INT64
    e = libsais16x64((const uint16_t *)x, p, n, 1, NULL);
    #else
    e = libsais16((const uint16_t *)x, p, n, 1, NULL);
    #endif
  } else {
    #ifdef USE_INT64
    e = libsais64((const uint8_t *)x, p, n, 1, NULL);
    #else
    e = libsais((const uint8_t *)x, p, n, 1, NULL);
    #endif
  }
  if(e<0) {
    fprintf(stderr,"Error: suffix array construction returned %d\n", e);
    free(x); free(p);
    return 1;
  }
  end_time = times(&en);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  if(Verbose>1) fprintf(stderr,"Elapsed time: %f seconds (SA)\n", tot_time);
  
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
    if (input_is_16bit) {
      #ifdef USE_INT64
      e = libsais16x64_plcp((const uint16_t *)x, p, plcp, n);
      if(e<0) {fprintf(stderr,"Error: libsais16x64_plcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      e = libsais16x64_lcp(plcp, p, p, n);
      if(e<0) {fprintf(stderr,"Error: libsais16x64_lcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #else
      e = libsais16_plcp((const uint16_t *)x, p, plcp, n);
      if(e<0) {fprintf(stderr,"Error: libsais16_plcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      e = libsais16_lcp(plcp, p, p, n);
      if(e<0) {fprintf(stderr,"Error: libsais16_lcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #endif
    } else {
      #ifdef USE_INT64
      e = libsais64_plcp((const uint8_t *)x, p, plcp, n);
      if(e<0) {fprintf(stderr,"Error: libsais64_plcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      e = libsais64_lcp(plcp, p, p, n);
      if(e<0) {fprintf(stderr,"Error: libsais64_lcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #else
      e = libsais_plcp((const uint8_t *)x, p, plcp, n);
      if(e<0) {fprintf(stderr,"Error: libsais_plcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      e = libsais_lcp(plcp, p, p, n);
      if(e<0) {fprintf(stderr,"Error: libsais_lcp returned %d\n", e); free(x); free(p); free(plcp); return 1;}
      #endif
    }
    end_time = times(&en);
    tot_time =  (end_time - lcp_time)/DBL_CLK_TCK;
    if(Verbose) fprintf(stderr,"Elapsed time: %f seconds (SA+LCP)\n", tot_time);
    
    if(lcp_filename!=NULL)
      write_lcp(lcp_filename, p, n);
    
    if(compute_avg_lcp) {
      long long sum = 0;
      for(idx_t i = 0; i < n; i++) sum += p[i];
      printf("Average LCP: %f\n", (double) sum / n);
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
