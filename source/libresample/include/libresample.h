/**********************************************************************

  resample.h

  Real-time library interface by Dominic Mazzoni

  Based on resample-1.7:
    http://www-ccrma.stanford.edu/~jos/resample/

  Dual-licensed as LGPL and BSD; see README.md and LICENSE* files.

**********************************************************************/

#ifndef LIBRESAMPLE_INCLUDED
#define LIBRESAMPLE_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

void *resample_open(int      highQuality,
                    double   minFactor,
                    double   maxFactor);

void *resample_dup(const void *handle);

/* The low-pass filter a handle uses depends only on highQuality, not on the factors: one copy can serve every
   handle (synthLib::Resampler shares one per process instead of one per channel). resample_filter_size: the
   number of floats in each of the two tables; resample_build_filter fills them exactly as resample_open does;
   resample_open_with_filter opens a handle that reads the caller's tables, which must outlive it (it never
   writes or frees them). */
int resample_filter_size(int highQuality);
void resample_build_filter(int highQuality, float *Imp, float *ImpD);
void *resample_open_with_filter(int highQuality, double minFactor, double maxFactor,
                                const float *Imp, const float *ImpD);

int resample_get_filter_width(const void *handle);

int resample_process(void   *handle,
                     double  factor,
                     float  *inBuffer,
                     int     inBufferLen,
                     int     lastFlag,
                     int    *inBufferUsed,
                     float  *outBuffer,
                     int     outBufferLen);

void resample_close(void *handle);

#ifdef __cplusplus
}		/* extern "C" */
#endif	/* __cplusplus */

#endif /* LIBRESAMPLE_INCLUDED */
