/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: radar.c
 *
 * Code generated for Simulink model 'radar'.
 *
 * Model version                  : 1.1
 * Simulink Coder version         : 26.1 (R2026a) 20-Nov-2025
 * C/C++ source code generated on : Thu Jul  9 06:52:34 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: Intel->x86-64 (Windows64)
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "radar.h"
#include "rtwtypes.h"
#include "radar_private.h"
#include <emmintrin.h>
#include <math.h>
#include <string.h>
#include "rt_nonfinite.h"

/* Forward declaration for local functions */
static void radar_permute(const creal_T a[320000], creal_T b[320000]);
static void radar_mvdr_beamform(const creal_T rx[320000], const real_T
  x_elements[8], real_T theta_look_rad, real_T load_factor, creal_T y[40000],
  creal_T w[8]);
static void radar_matched_filter(const creal_T rx[2500], const creal_T tx[25],
  creal_T y[2500], B_radar_T *radar_B);
static void FFTImplementationCallback_r2br_(const creal_T x[40000], const real_T
  costab[9], const real_T sintab[9], creal_T y[40000]);
static void radar_fft(const creal_T x[40000], creal_T y[40000], B_radar_T
                      *radar_B);
static void radar_fftshift(creal_T x[40000]);
static void radar_range_doppler(const creal_T rx_pulses[40000], const creal_T
  tx[25], real_T fs, real_T lambda, real_T PRI, creal_T RDM[40000], real_T
  range_axis_m[2500], real_T velocity_axis_mps[16], B_radar_T *radar_B);

/* Function for MATLAB Function: '<Root>/BeamformStage' */
static void radar_permute(const creal_T a[320000], creal_T b[320000])
{
  int32_T b_k;
  int32_T k;
  for (k = 0; k < 16; k++) {
    for (b_k = 0; b_k < 8; b_k++) {
      memcpy(&b[k * 2500 + b_k * 40000], &a[k * 20000 + b_k * 2500], 2500U *
             sizeof(creal_T));
    }
  }
}

/* Function for MATLAB Function: '<Root>/BeamformStage' */
static void radar_mvdr_beamform(const creal_T rx[320000], const real_T
  x_elements[8], real_T theta_look_rad, real_T load_factor, creal_T y[40000],
  creal_T w[8])
{
  __m128d tmp_0;
  creal_T R_hat[64];
  creal_T a[8];
  real_T tmp[2];
  real_T a_im;
  real_T a_re;
  real_T bim;
  real_T s;
  real_T sgnbi;
  real_T temp_im;
  real_T temp_re;
  int32_T b_a;
  int32_T b_k;
  int32_T ijA;
  int32_T jA;
  int32_T jj;
  int32_T kAcol;
  int32_T s_tmp;
  int8_T ipiv[8];
  static const int8_T c_b[64] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

  for (kAcol = 0; kAcol < 8; kAcol++) {
    for (b_k = 0; b_k < 8; b_k++) {
      temp_re = 0.0;
      temp_im = 0.0;
      for (jj = 0; jj < 40000; jj++) {
        b_a = 40000 * b_k + jj;
        jA = 40000 * kAcol + jj;
        s = rx[b_a].re;
        a_re = -rx[b_a].im;
        _mm_storeu_pd(&tmp[0], _mm_add_pd(_mm_add_pd(_mm_mul_pd(_mm_set1_pd
          (rx[jA].re), _mm_set_pd(a_re, s)), _mm_mul_pd(_mm_mul_pd(_mm_set1_pd
          (rx[jA].im), _mm_set_pd(s, a_re)), _mm_set_pd(1.0, -1.0))), _mm_set_pd
          (temp_im, temp_re)));
        temp_re = tmp[0];
        temp_im = tmp[1];
      }

      if (temp_im == 0.0) {
        jj = (b_k << 3) + kAcol;
        R_hat[jj].re = temp_re / 40000.0;
        R_hat[jj].im = 0.0;
      } else if (temp_re == 0.0) {
        jj = (b_k << 3) + kAcol;
        R_hat[jj].re = 0.0;
        R_hat[jj].im = temp_im / 40000.0;
      } else {
        _mm_storeu_pd((real_T *)&R_hat[kAcol + (b_k << 3)], _mm_div_pd
                      (_mm_set_pd(temp_im, temp_re), _mm_set1_pd(40000.0)));
      }
    }
  }

  temp_re = 0.0;
  temp_im = 0.0;
  for (b_k = 0; b_k < 8; b_k++) {
    tmp_0 = _mm_add_pd(_mm_loadu_pd((const real_T *)&R_hat[(b_k << 3) + b_k]),
                       _mm_set_pd(temp_im, temp_re));
    _mm_storeu_pd(&tmp[0], tmp_0);
    temp_re = tmp[0];
    temp_im = tmp[1];
  }

  if (temp_im == 0.0) {
    temp_re /= 8.0;
    temp_im = 0.0;
  } else if (temp_re == 0.0) {
    temp_re = 0.0;
    temp_im /= 8.0;
  } else {
    temp_re /= 8.0;
    temp_im /= 8.0;
  }

  temp_re *= load_factor;
  temp_im *= load_factor;
  for (kAcol = 0; kAcol < 64; kAcol++) {
    tmp_0 = _mm_add_pd(_mm_mul_pd(_mm_set_pd(temp_im, temp_re), _mm_set1_pd
      (c_b[kAcol])), _mm_loadu_pd((const real_T *)&R_hat[kAcol]));
    _mm_storeu_pd((real_T *)&R_hat[kAcol], tmp_0);
  }

  temp_im = sin(theta_look_rad);
  for (b_k = 0; b_k < 8; b_k++) {
    s = x_elements[b_k];
    temp_re = s * 6.283185307179586 * temp_im;
    if (temp_re == 0.0) {
      a_re = s * 0.0 * temp_im / 0.107068735;
      a_im = 0.0;
    } else if (s * 0.0 * temp_im == 0.0) {
      a_re = 0.0;
      a_im = temp_re / 0.107068735;
    } else {
      a_re = (rtNaN);
      a_im = temp_re / 0.107068735;
    }

    a[b_k].re = a_re;
    a[b_k].im = a_im;
    if (a_re == 0.0) {
      a_re = cos(a_im);
      a_im = sin(a_im);
      a[b_k].re = a_re;
      a[b_k].im = a_im;
    } else if (a_im == 0.0) {
      a_im = 0.0;
      a[b_k].re = (rtNaN);
      a[b_k].im = 0.0;
    } else {
      a_im = (rtNaN);
      a[b_k].re = (rtNaN);
      a[b_k].im = (rtNaN);
    }

    w[b_k].re = a_re;
    w[b_k].im = a_im;
    ipiv[b_k] = (int8_T)(b_k + 1);
  }

  for (b_k = 0; b_k < 7; b_k++) {
    jj = b_k * 9;
    kAcol = 9 - b_k;
    b_a = 0;
    temp_re = fabs(R_hat[jj].re) + fabs(R_hat[jj].im);
    for (jA = 2; jA < kAcol; jA++) {
      s_tmp = (jj + jA) - 1;
      s = fabs(R_hat[s_tmp].re) + fabs(R_hat[s_tmp].im);
      if (s > temp_re) {
        b_a = jA - 1;
        temp_re = s;
      }
    }

    kAcol = jj + b_a;
    if ((R_hat[kAcol].re != 0.0) || (R_hat[kAcol].im != 0.0)) {
      if (b_a != 0) {
        kAcol = b_k + b_a;
        ipiv[b_k] = (int8_T)(kAcol + 1);
        for (b_a = 0; b_a < 8; b_a++) {
          s_tmp = b_a << 3;
          jA = s_tmp + b_k;
          temp_re = R_hat[jA].re;
          temp_im = R_hat[jA].im;
          s_tmp += kAcol;
          R_hat[jA] = R_hat[s_tmp];
          R_hat[s_tmp].re = temp_re;
          R_hat[s_tmp].im = temp_im;
        }
      }

      kAcol = (jj - b_k) + 8;
      for (b_a = jj + 2; b_a <= kAcol; b_a++) {
        temp_im = R_hat[b_a - 1].re;
        temp_re = R_hat[b_a - 1].im;
        a_im = R_hat[jj].re;
        a_re = R_hat[jj].im;
        if (a_re == 0.0) {
          if (temp_re == 0.0) {
            R_hat[b_a - 1].re = temp_im / a_im;
            R_hat[b_a - 1].im = 0.0;
          } else if (temp_im == 0.0) {
            R_hat[b_a - 1].re = 0.0;
            R_hat[b_a - 1].im = temp_re / a_im;
          } else {
            _mm_storeu_pd((real_T *)&R_hat[b_a - 1], _mm_div_pd(_mm_set_pd
              (temp_re, temp_im), _mm_set1_pd(a_im)));
          }
        } else if (a_im == 0.0) {
          if (temp_im == 0.0) {
            R_hat[b_a - 1].re = temp_re / a_re;
            R_hat[b_a - 1].im = 0.0;
          } else if (temp_re == 0.0) {
            R_hat[b_a - 1].re = 0.0;
            R_hat[b_a - 1].im = -(temp_im / a_re);
          } else {
            R_hat[b_a - 1].re = temp_re / a_re;
            R_hat[b_a - 1].im = -(temp_im / a_re);
          }
        } else {
          s = fabs(a_im);
          bim = fabs(a_re);
          if (s > bim) {
            s = a_re / a_im;
            bim = s * a_re + a_im;
            R_hat[b_a - 1].re = (s * temp_re + temp_im) / bim;
            R_hat[b_a - 1].im = (temp_re - s * temp_im) / bim;
          } else if (bim == s) {
            if (a_im > 0.0) {
              bim = 0.5;
            } else {
              bim = -0.5;
            }

            if (a_re > 0.0) {
              sgnbi = 0.5;
            } else {
              sgnbi = -0.5;
            }

            _mm_storeu_pd((real_T *)&R_hat[b_a - 1], _mm_div_pd(_mm_add_pd
              (_mm_mul_pd(_mm_set_pd(temp_re, temp_im), _mm_set1_pd(bim)),
               _mm_mul_pd(_mm_mul_pd(_mm_set_pd(temp_im, temp_re), _mm_set1_pd
              (sgnbi)), _mm_set_pd(-1.0, 1.0))), _mm_set1_pd(s)));
          } else {
            s = a_im / a_re;
            _mm_storeu_pd((real_T *)&R_hat[b_a - 1], _mm_div_pd(_mm_add_pd
              (_mm_mul_pd(_mm_set1_pd(s), _mm_set_pd(temp_re, temp_im)),
               _mm_mul_pd(_mm_set_pd(temp_im, temp_re), _mm_set_pd(-1.0, 1.0))),
              _mm_set1_pd(s * a_im + a_re)));
          }
        }
      }
    }

    b_a = 6 - b_k;
    jA = jj + 10;
    for (s_tmp = 0; s_tmp <= b_a; s_tmp++) {
      kAcol = ((s_tmp << 3) + jj) + 8;
      if ((R_hat[kAcol].re != 0.0) || (R_hat[kAcol].im != 0.0)) {
        temp_im = R_hat[kAcol].re;
        s = R_hat[kAcol].im;
        temp_re = -temp_im - s * 0.0;
        temp_im = temp_im * 0.0 - s;
        kAcol = (jA - b_k) + 6;
        for (ijA = jA; ijA <= kAcol; ijA++) {
          int32_T R_hat_im_tmp;
          R_hat_im_tmp = ((jj + ijA) - jA) + 1;
          s = R_hat[R_hat_im_tmp].re;
          a_re = R_hat[R_hat_im_tmp].im;
          R_hat[ijA - 1].re += s * temp_re - a_re * temp_im;
          R_hat[ijA - 1].im += s * temp_im + a_re * temp_re;
        }
      }

      jA += 8;
    }
  }

  for (b_k = 0; b_k < 7; b_k++) {
    int8_T ipiv_0;
    ipiv_0 = ipiv[b_k];
    if (b_k + 1 != ipiv_0) {
      temp_re = w[b_k].re;
      temp_im = w[b_k].im;
      w[b_k] = w[ipiv_0 - 1];
      w[ipiv_0 - 1].re = temp_re;
      w[ipiv_0 - 1].im = temp_im;
    }
  }

  for (jj = 0; jj < 8; jj++) {
    kAcol = jj << 3;
    if ((w[jj].re != 0.0) || (w[jj].im != 0.0)) {
      for (b_a = jj + 2; b_a < 9; b_a++) {
        b_k = (b_a + kAcol) - 1;
        temp_re = R_hat[b_k].re;
        temp_im = w[jj].re;
        s = R_hat[b_k].im;
        a_re = w[jj].im;
        w[b_a - 1].re -= temp_re * temp_im - s * a_re;
        w[b_a - 1].im -= s * temp_im + temp_re * a_re;
      }
    }
  }

  for (jj = 7; jj >= 0; jj--) {
    kAcol = jj << 3;
    temp_re = w[jj].re;
    temp_im = w[jj].im;
    if ((temp_re != 0.0) || (temp_im != 0.0)) {
      b_k = jj + kAcol;
      a_im = R_hat[b_k].re;
      a_re = R_hat[b_k].im;
      if (a_re == 0.0) {
        if (temp_im == 0.0) {
          w[jj].re = temp_re / a_im;
          w[jj].im = 0.0;
        } else if (temp_re == 0.0) {
          w[jj].re = 0.0;
          w[jj].im = temp_im / a_im;
        } else {
          _mm_storeu_pd((real_T *)&w[jj], _mm_div_pd(_mm_set_pd(temp_im, temp_re),
            _mm_set1_pd(a_im)));
        }
      } else if (a_im == 0.0) {
        if (temp_re == 0.0) {
          w[jj].re = temp_im / a_re;
          w[jj].im = 0.0;
        } else if (temp_im == 0.0) {
          w[jj].re = 0.0;
          w[jj].im = -(temp_re / a_re);
        } else {
          w[jj].re = temp_im / a_re;
          w[jj].im = -(temp_re / a_re);
        }
      } else {
        s = fabs(a_im);
        bim = fabs(a_re);
        if (s > bim) {
          s = a_re / a_im;
          bim = s * a_re + a_im;
          w[jj].re = (s * temp_im + temp_re) / bim;
          w[jj].im = (temp_im - s * temp_re) / bim;
        } else if (bim == s) {
          if (a_im > 0.0) {
            bim = 0.5;
          } else {
            bim = -0.5;
          }

          if (a_re > 0.0) {
            sgnbi = 0.5;
          } else {
            sgnbi = -0.5;
          }

          _mm_storeu_pd((real_T *)&w[jj], _mm_div_pd(_mm_add_pd(_mm_mul_pd
            (_mm_set_pd(temp_im, temp_re), _mm_set1_pd(bim)), _mm_mul_pd
            (_mm_mul_pd(_mm_set_pd(temp_re, temp_im), _mm_set1_pd(sgnbi)),
             _mm_set_pd(-1.0, 1.0))), _mm_set1_pd(s)));
        } else {
          s = a_im / a_re;
          _mm_storeu_pd((real_T *)&w[jj], _mm_div_pd(_mm_add_pd(_mm_mul_pd
            (_mm_set1_pd(s), _mm_set_pd(temp_im, temp_re)), _mm_mul_pd
            (_mm_set_pd(temp_re, temp_im), _mm_set_pd(-1.0, 1.0))), _mm_set1_pd
            (s * a_im + a_re)));
        }
      }

      for (b_a = 0; b_a < jj; b_a++) {
        b_k = b_a + kAcol;
        temp_re = R_hat[b_k].re;
        temp_im = w[jj].re;
        s = R_hat[b_k].im;
        a_re = w[jj].im;
        w[b_a].re -= temp_re * temp_im - s * a_re;
        w[b_a].im -= s * temp_im + temp_re * a_re;
      }
    }
  }

  a_re = 0.0;
  a_im = 0.0;
  for (kAcol = 0; kAcol < 8; kAcol++) {
    tmp_0 = _mm_loadu_pd((const real_T *)&w[kAcol]);
    _mm_storeu_pd(&tmp[0], _mm_add_pd(_mm_add_pd(_mm_mul_pd(_mm_set1_pd(a[kAcol]
      .re), tmp_0), _mm_mul_pd(_mm_mul_pd(_mm_set1_pd(-a[kAcol].im),
      _mm_shuffle_pd(tmp_0, tmp_0, 1U)), _mm_set_pd(1.0, -1.0))), _mm_set_pd
      (a_im, a_re)));
    a_re = tmp[0];
    a_im = tmp[1];
  }

  for (kAcol = 0; kAcol < 8; kAcol++) {
    temp_im = w[kAcol].re;
    temp_re = w[kAcol].im;
    if (a_im == 0.0) {
      if (temp_re == 0.0) {
        w[kAcol].re = temp_im / a_re;
        w[kAcol].im = 0.0;
      } else if (temp_im == 0.0) {
        w[kAcol].re = 0.0;
        w[kAcol].im = temp_re / a_re;
      } else {
        _mm_storeu_pd((real_T *)&w[kAcol], _mm_div_pd(_mm_set_pd(temp_re,
          temp_im), _mm_set1_pd(a_re)));
      }
    } else if (a_re == 0.0) {
      if (temp_im == 0.0) {
        w[kAcol].re = temp_re / a_im;
        w[kAcol].im = 0.0;
      } else if (temp_re == 0.0) {
        w[kAcol].re = 0.0;
        w[kAcol].im = -(temp_im / a_im);
      } else {
        w[kAcol].re = temp_re / a_im;
        w[kAcol].im = -(temp_im / a_im);
      }
    } else {
      s = fabs(a_re);
      bim = fabs(a_im);
      if (s > bim) {
        s = a_im / a_re;
        bim = s * a_im + a_re;
        w[kAcol].re = (s * temp_re + temp_im) / bim;
        w[kAcol].im = (temp_re - s * temp_im) / bim;
      } else if (bim == s) {
        if (a_re > 0.0) {
          bim = 0.5;
        } else {
          bim = -0.5;
        }

        if (a_im > 0.0) {
          sgnbi = 0.5;
        } else {
          sgnbi = -0.5;
        }

        _mm_storeu_pd((real_T *)&w[kAcol], _mm_div_pd(_mm_add_pd(_mm_mul_pd
          (_mm_set_pd(temp_re, temp_im), _mm_set1_pd(bim)), _mm_mul_pd
          (_mm_mul_pd(_mm_set_pd(temp_im, temp_re), _mm_set1_pd(sgnbi)),
           _mm_set_pd(-1.0, 1.0))), _mm_set1_pd(s)));
      } else {
        s = a_re / a_im;
        _mm_storeu_pd((real_T *)&w[kAcol], _mm_div_pd(_mm_add_pd(_mm_mul_pd
          (_mm_set1_pd(s), _mm_set_pd(temp_re, temp_im)), _mm_mul_pd(_mm_set_pd
          (temp_im, temp_re), _mm_set_pd(-1.0, 1.0))), _mm_set1_pd(s * a_re +
          a_im)));
      }
    }
  }

  memset(&y[0], 0, 40000U * sizeof(creal_T));
  for (kAcol = 0; kAcol < 8; kAcol++) {
    temp_re = w[kAcol].re;
    temp_im = -w[kAcol].im;
    for (b_k = 0; b_k < 40000; b_k++) {
      jj = 40000 * kAcol + b_k;
      _mm_storeu_pd((real_T *)&y[b_k], _mm_add_pd(_mm_add_pd(_mm_mul_pd
        (_mm_set1_pd(rx[jj].re), _mm_set_pd(temp_im, temp_re)), _mm_mul_pd
        (_mm_mul_pd(_mm_set1_pd(rx[jj].im), _mm_set_pd(temp_re, temp_im)),
         _mm_set_pd(1.0, -1.0))), _mm_loadu_pd((const real_T *)&y[b_k])));
    }
  }
}

/* Function for MATLAB Function: '<Root>/RangeDopplerStage' */
static void radar_matched_filter(const creal_T rx[2500], const creal_T tx[25],
  creal_T y[2500], B_radar_T *radar_B)
{
  creal_T x[25];
  real_T rx_0;
  real_T rx_1;
  real_T xtmp_im;
  real_T xtmp_re;
  int32_T b_k;
  int32_T i;
  int32_T k;
  memcpy(&x[0], &tx[0], 25U * sizeof(creal_T));
  for (i = 0; i < 12; i++) {
    xtmp_re = x[i].re;
    xtmp_im = x[i].im;
    x[i] = x[24 - i];
    x[24 - i].re = xtmp_re;
    x[24 - i].im = xtmp_im;
  }

  for (i = 0; i < 25; i++) {
    x[i].im = -x[i].im;
  }

  memset(&radar_B->y_full[0], 0, 2524U * sizeof(creal_T));
  for (k = 0; k < 25; k++) {
    xtmp_re = x[k].re;
    xtmp_im = x[k].im;
    for (b_k = 0; b_k < 2500; b_k++) {
      rx_0 = rx[b_k].re;
      rx_1 = rx[b_k].im;
      i = k + b_k;
      _mm_storeu_pd((real_T *)&radar_B->y_full[i], _mm_add_pd(_mm_add_pd
        (_mm_mul_pd(_mm_set1_pd(xtmp_re), _mm_set_pd(rx_1, rx_0)), _mm_mul_pd
         (_mm_mul_pd(_mm_set1_pd(xtmp_im), _mm_set_pd(rx_0, rx_1)), _mm_set_pd
          (1.0, -1.0))), _mm_loadu_pd((const real_T *)&radar_B->y_full[i])));
    }
  }

  memcpy(&y[0], &radar_B->y_full[24], 2500U * sizeof(creal_T));
}

/* Function for MATLAB Function: '<Root>/RangeDopplerStage' */
static void FFTImplementationCallback_r2br_(const creal_T x[40000], const real_T
  costab[9], const real_T sintab[9], creal_T y[40000])
{
  real_T tmp[2];
  int32_T chan;
  int32_T iDelta2;
  int32_T j;
  int32_T ju;
  for (chan = 0; chan < 2500; chan++) {
    __m128d tmp_0;
    int32_T iheight;
    int32_T ix;
    int32_T iy;
    ix = chan << 4;
    iy = ix;
    ju = 0;
    for (iDelta2 = 0; iDelta2 < 15; iDelta2++) {
      boolean_T tst;
      y[iy] = x[ix + iDelta2];
      iy = 16;
      tst = true;
      while (tst) {
        iy >>= 1;
        ju ^= iy;
        tst = ((ju & iy) == 0);
      }

      iy = ix + ju;
    }

    y[iy] = x[ix + 15];
    for (ju = ix; ju <= ix + 14; ju += 2) {
      __m128d tmp_1;
      tmp_0 = _mm_set_pd(y[ju].im, y[ju].re);
      tmp_1 = _mm_set_pd(y[ju + 1].im, y[ju + 1].re);
      _mm_storeu_pd((real_T *)&y[ju + 1], _mm_sub_pd(tmp_0, tmp_1));
      _mm_storeu_pd((real_T *)&y[ju], _mm_add_pd(tmp_0, tmp_1));
    }

    ju = 2;
    iDelta2 = 4;
    iy = 4;
    iheight = 13;
    while (iy > 0) {
      int32_T b_i;
      int32_T ihi;
      int32_T istart;
      b_i = ix;
      ihi = ix + iheight;
      while (b_i < ihi) {
        istart = b_i + ju;
        tmp_0 = _mm_set_pd(y[istart].im, y[istart].re);
        _mm_storeu_pd((real_T *)&y[istart], _mm_sub_pd(_mm_loadu_pd((const
          real_T *)&y[b_i]), tmp_0));
        _mm_storeu_pd((real_T *)&y[b_i], _mm_add_pd(_mm_loadu_pd((const real_T *)
          &y[b_i]), tmp_0));
        b_i += iDelta2;
      }

      istart = ix + 1;
      for (j = iy; j < 8; j += iy) {
        real_T twid_im;
        real_T twid_re;
        twid_re = costab[j];
        twid_im = sintab[j];
        b_i = istart;
        ihi = istart + iheight;
        while (b_i < ihi) {
          int32_T tmp_2;
          tmp_2 = b_i + ju;
          tmp_0 = _mm_loadu_pd((const real_T *)&y[tmp_2]);
          _mm_storeu_pd(&tmp[0], _mm_add_pd(_mm_mul_pd(tmp_0, _mm_set1_pd
            (twid_re)), _mm_mul_pd(_mm_mul_pd(_mm_shuffle_pd(tmp_0, tmp_0, 1U),
            _mm_set1_pd(twid_im)), _mm_set_pd(1.0, -1.0))));
          tmp_0 = _mm_set_pd(tmp[1], tmp[0]);
          _mm_storeu_pd((real_T *)&y[tmp_2], _mm_sub_pd(_mm_loadu_pd((const
            real_T *)&y[b_i]), tmp_0));
          _mm_storeu_pd((real_T *)&y[b_i], _mm_add_pd(_mm_loadu_pd((const real_T
            *)&y[b_i]), tmp_0));
          b_i += iDelta2;
        }

        istart++;
      }

      iy >>= 1;
      ju = iDelta2;
      iDelta2 += iDelta2;
      iheight -= ju;
    }
  }
}

/* Function for MATLAB Function: '<Root>/RangeDopplerStage' */
static void radar_fft(const creal_T x[40000], creal_T y[40000], B_radar_T
                      *radar_B)
{
  int32_T i;
  int32_T i_0;
  static const real_T b[9] = { 1.0, 0.9238795325112867, 0.7071067811865476,
    0.3826834323650898, 0.0, -0.3826834323650898, -0.7071067811865476,
    -0.9238795325112867, -1.0 };

  static const real_T c[9] = { 0.0, -0.3826834323650898, -0.7071067811865476,
    -0.9238795325112867, -1.0, -0.9238795325112867, -0.7071067811865476,
    -0.3826834323650898, -0.0 };

  for (i = 0; i < 16; i++) {
    for (i_0 = 0; i_0 < 2500; i_0++) {
      radar_B->x[i + (i_0 << 4)] = x[2500 * i + i_0];
    }
  }

  FFTImplementationCallback_r2br_(radar_B->x, b, c, radar_B->dcv1);
  for (i = 0; i < 16; i++) {
    for (i_0 = 0; i_0 < 2500; i_0++) {
      y[i_0 + 2500 * i] = radar_B->dcv1[(i_0 << 4) + i];
    }
  }
}

/* Function for MATLAB Function: '<Root>/RangeDopplerStage' */
static void radar_fftshift(creal_T x[40000])
{
  int32_T i1;
  int32_T j;
  int32_T k;
  i1 = -1;
  for (j = 0; j < 2500; j++) {
    i1++;
    for (k = 0; k < 8; k++) {
      real_T tmp_im;
      real_T tmp_re;
      int32_T tmp_re_tmp;
      tmp_re_tmp = k * 2500 + i1;
      tmp_re = x[tmp_re_tmp].re;
      tmp_im = x[tmp_re_tmp].im;
      x[tmp_re_tmp] = x[tmp_re_tmp + 20000];
      x[tmp_re_tmp + 20000].re = tmp_re;
      x[tmp_re_tmp + 20000].im = tmp_im;
    }
  }
}

/* Function for MATLAB Function: '<Root>/RangeDopplerStage' */
static void radar_range_doppler(const creal_T rx_pulses[40000], const creal_T
  tx[25], real_T fs, real_T lambda, real_T PRI, creal_T RDM[40000], real_T
  range_axis_m[2500], real_T velocity_axis_mps[16], B_radar_T *radar_B)
{
  __m128d tmp_1;
  real_T tmp[2];
  real_T tmp_0[2];
  real_T b;
  int32_T i;
  int32_T n;
  int32_T tmp_2;
  static const real_T b_0[16] = { 0.08000000000000002, 0.11976908948440362,
    0.2321999210749252, 0.3978521825875242, 0.5880830931031207, 0.77,
    0.9121478174124759, 0.9899478963375506, 0.9899478963375506,
    0.9121478174124759, 0.77, 0.5880830931031207, 0.3978521825875242,
    0.2321999210749252, 0.11976908948440362, 0.08000000000000002 };

  for (n = 0; n < 16; n++) {
    radar_matched_filter(&rx_pulses[2500 * n], tx, &radar_B->range_compressed
                         [2500 * n], radar_B);
  }

  for (n = 0; n < 16; n++) {
    b = b_0[n];
    for (i = 0; i < 2500; i++) {
      tmp_2 = 2500 * n + i;
      _mm_storeu_pd((real_T *)&radar_B->range_compressed_m[tmp_2], _mm_mul_pd
                    (_mm_loadu_pd((const real_T *)&radar_B->
        range_compressed[tmp_2]), _mm_set1_pd(b)));
    }
  }

  radar_fft(radar_B->range_compressed_m, RDM, radar_B);
  radar_fftshift(RDM);
  b = 2.0 * fs;
  for (n = 0; n <= 2498; n += 2) {
    tmp_0[0] = n;
    tmp_0[1] = n + 1;
    tmp_1 = _mm_loadu_pd(&tmp_0[0]);
    _mm_storeu_pd(&range_axis_m[n], _mm_div_pd(_mm_mul_pd(_mm_set1_pd
      (2.99792458E+8), tmp_1), _mm_set1_pd(b)));
  }

  b = 16.0 * PRI;
  for (n = 0; n <= 14; n += 2) {
    tmp[0] = n;
    tmp[1] = n + 1;
    tmp_1 = _mm_loadu_pd(&tmp[0]);
    _mm_storeu_pd(&velocity_axis_mps[n], _mm_div_pd(_mm_mul_pd(_mm_div_pd
      (_mm_sub_pd(tmp_1, _mm_set1_pd(8.0)), _mm_set1_pd(b)), _mm_set1_pd(lambda)),
      _mm_set1_pd(2.0)));
  }
}

real_T rt_hypotd_snf(real_T u0, real_T u1)
{
  real_T a;
  real_T b;
  real_T y;
  a = fabs(u0);
  b = fabs(u1);
  if (a < b) {
    a /= b;
    y = sqrt(a * a + 1.0) * b;
  } else if (a > b) {
    b /= a;
    y = sqrt(b * b + 1.0) * a;
  } else if (rtIsNaN(b)) {
    y = (rtNaN);
  } else {
    y = a * 1.4142135623730951;
  }

  return y;
}

/* Model step function */
void radar_step(RT_MODEL_radar_T *const radar_M)
{
  B_radar_T *radar_B = radar_M->blockIO;
  ExtU_radar_T *radar_U = (ExtU_radar_T *) radar_M->inputs;
  ExtY_radar_T *radar_Y = (ExtY_radar_T *) radar_M->outputs;
  creal_T w_mvdr[8];
  real_T rtb_velocity_axis_mps[16];
  real_T top_p[16];
  real_T tmp[2];
  real_T min_val;
  real_T noise_count;
  real_T re;
  real_T training_sum;
  int32_T dd;
  int32_T i;
  int32_T k;
  int32_T min_idx;
  int32_T n_found;
  int32_T t;
  int16_T top_r[16];
  int8_T top_d[16];
  static const real_T b[8] = { -0.18737028625, -0.13383591875,
    -0.08030155124999999, -0.02676718375, 0.02676718375, 0.08030155124999999,
    0.13383591875, 0.18737028625 };

  /* MATLAB Function: '<Root>/BeamformStage' */
  radar_permute(radar_U->rx_cube, radar_B->dcv);
  radar_mvdr_beamform(radar_B->dcv, b, radar_U->look_angle_rad, 0.001,
                      radar_B->a__2, w_mvdr);
  for (n_found = 0; n_found < 16; n_found++) {
    for (i = 0; i < 2500; i++) {
      re = 0.0;
      noise_count = 0.0;
      for (k = 0; k < 8; k++) {
        dd = (2500 * k + i) + 20000 * n_found;
        training_sum = w_mvdr[k].re;
        min_val = -w_mvdr[k].im;
        _mm_storeu_pd(&tmp[0], _mm_add_pd(_mm_add_pd(_mm_mul_pd(_mm_set1_pd
          (radar_U->rx_cube[dd].re), _mm_set_pd(min_val, training_sum)),
          _mm_mul_pd(_mm_mul_pd(_mm_set1_pd(radar_U->rx_cube[dd].im), _mm_set_pd
          (training_sum, min_val)), _mm_set_pd(1.0, -1.0))), _mm_set_pd
          (noise_count, re)));
        re = tmp[0];
        noise_count = tmp[1];
      }

      k = 2500 * n_found + i;
      radar_B->rx_bf[k].re = re;
      radar_B->rx_bf[k].im = noise_count;
    }
  }

  /* End of MATLAB Function: '<Root>/BeamformStage' */

  /* MATLAB Function: '<Root>/RangeDopplerStage' */
  radar_range_doppler(radar_B->rx_bf, radar_U->tx, 250000.0, 0.107068735, 0.01,
                      radar_B->a__2, radar_B->range_axis_m,
                      rtb_velocity_axis_mps, radar_B);
  for (k = 0; k < 40000; k++) {
    training_sum = rt_hypotd_snf(radar_B->a__2[k].re, radar_B->a__2[k].im);
    radar_B->RDM_power[k] = training_sum * training_sum;

    /* MATLAB Function: '<Root>/CFARStage' */
    radar_B->det_mask[k] = false;
  }

  /* End of MATLAB Function: '<Root>/RangeDopplerStage' */

  /* MATLAB Function: '<Root>/CFARStage' */
  for (n_found = 0; n_found < 2476; n_found++) {
    for (k = 0; k < 10; k++) {
      training_sum = 0.0;
      for (i = 0; i < 25; i++) {
        for (dd = 0; dd < 7; dd++) {
          if (fabs((real_T)i - 12.0) <= 4.0) {
            if (!(fabs((real_T)dd - 3.0) <= 1.0)) {
              training_sum += radar_B->RDM_power[((k + dd) * 2500 + n_found) + i];
            }
          } else {
            training_sum += radar_B->RDM_power[((k + dd) * 2500 + n_found) + i];
          }
        }
      }

      i = ((k + 3) * 2500 + n_found) + 12;
      if (radar_B->RDM_power[i] > training_sum / 148.0 * 14.48087752476884) {
        radar_B->det_mask[i] = true;
      }
    }
  }

  for (i = 0; i < 16; i++) {
    /* Outport: '<Root>/det_range_m' incorporates:
     *  MATLAB Function: '<Root>/CFARStage'
     */
    radar_Y->det_range_m[i] = (rtNaN);

    /* Outport: '<Root>/det_velocity_mps' incorporates:
     *  MATLAB Function: '<Root>/CFARStage'
     */
    radar_Y->det_velocity_mps[i] = (rtNaN);

    /* Outport: '<Root>/det_snr_dB' incorporates:
     *  MATLAB Function: '<Root>/CFARStage'
     */
    radar_Y->det_snr_dB[i] = (rtNaN);

    /* MATLAB Function: '<Root>/CFARStage' */
    top_p[i] = 0.0;
    top_r[i] = 0;
    top_d[i] = 0;
  }

  /* MATLAB Function: '<Root>/CFARStage' */
  n_found = -1;
  training_sum = 0.0;
  noise_count = 0.0;
  for (dd = 0; dd < 2500; dd++) {
    for (t = 0; t < 16; t++) {
      i = 2500 * t + dd;
      if (radar_B->det_mask[i]) {
        if (n_found + 1 < 16) {
          n_found++;
          top_p[n_found] = radar_B->RDM_power[i];
          top_r[n_found] = (int16_T)(dd + 1);
          top_d[n_found] = (int8_T)(t + 1);
        } else {
          min_val = top_p[0];
          min_idx = 0;
          for (k = 0; k < 15; k++) {
            re = top_p[k + 1];
            if (re < min_val) {
              min_val = re;
              min_idx = k + 1;
            }
          }

          re = radar_B->RDM_power[i];
          if (re > min_val) {
            top_p[min_idx] = re;
            top_r[min_idx] = (int16_T)(dd + 1);
            top_d[min_idx] = (int8_T)(t + 1);
          }
        }
      } else {
        training_sum += radar_B->RDM_power[i];
        noise_count++;
      }
    }
  }

  if (noise_count > 0.0) {
    noise_count = training_sum / noise_count;
  } else {
    noise_count = 2.220446049250313E-16;
  }

  for (i = 0; i < n_found; i++) {
    k = n_found - i;
    for (dd = 0; dd < k; dd++) {
      re = top_p[dd];
      training_sum = top_p[dd + 1];
      if (re < training_sum) {
        top_p[dd] = training_sum;
        top_p[dd + 1] = re;
        t = top_r[dd];
        top_r[dd] = top_r[dd + 1];
        top_r[dd + 1] = (int16_T)t;
        t = top_d[dd];
        top_d[dd] = top_d[dd + 1];
        top_d[dd + 1] = (int8_T)t;
      }
    }
  }

  i = ((n_found + 1) / 2) << 1;
  k = i - 2;

  /* MATLAB Function: '<Root>/CFARStage' incorporates:
   *  Outport: '<Root>/det_range_m'
   *  Outport: '<Root>/det_snr_dB'
   *  Outport: '<Root>/det_velocity_mps'
   */
  for (dd = 0; dd <= k; dd += 2) {
    radar_Y->det_range_m[dd] = radar_B->range_axis_m[top_r[dd] - 1];
    radar_Y->det_range_m[dd + 1] = radar_B->range_axis_m[top_r[dd + 1] - 1];
    radar_Y->det_velocity_mps[dd] = rtb_velocity_axis_mps[top_d[dd] - 1];
    radar_Y->det_velocity_mps[dd + 1] = rtb_velocity_axis_mps[top_d[dd + 1] - 1];
    training_sum = fmax(noise_count, 2.220446049250313E-16);
    _mm_storeu_pd(&radar_Y->det_snr_dB[dd], _mm_mul_pd(_mm_set_pd(log10(fmax
      (top_p[dd + 1], 2.220446049250313E-16) / training_sum), log10(fmax
      (top_p[dd], 2.220446049250313E-16) / training_sum)), _mm_set1_pd(10.0)));
  }

  for (dd = i; dd <= n_found; dd++) {
    radar_Y->det_range_m[dd] = radar_B->range_axis_m[top_r[dd] - 1];
    radar_Y->det_velocity_mps[dd] = rtb_velocity_axis_mps[top_d[dd] - 1];
    radar_Y->det_snr_dB[dd] = log10(fmax(top_p[dd], 2.220446049250313E-16) /
      fmax(noise_count, 2.220446049250313E-16)) * 10.0;
  }

  /* Outport: '<Root>/n_detections' incorporates:
   *  MATLAB Function: '<Root>/CFARStage'
   */
  radar_Y->n_detections = n_found + 1;
}

/* Model initialize function */
void radar_initialize(RT_MODEL_radar_T *const radar_M)
{
  ExtU_radar_T *radar_U = (ExtU_radar_T *) radar_M->inputs;
  ExtY_radar_T *radar_Y = (ExtY_radar_T *) radar_M->outputs;

  /* Registration code */

  /* external inputs */
  (void)memset(radar_U, 0, sizeof(ExtU_radar_T));

  /* external outputs */
  (void)memset(radar_Y, 0, sizeof(ExtY_radar_T));
}

/* Model terminate function */
void radar_terminate(RT_MODEL_radar_T *const radar_M)
{
  /* (no terminate code required) */
  UNUSED_PARAMETER(radar_M);
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
