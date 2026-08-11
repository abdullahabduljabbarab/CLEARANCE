/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: missile.c
 *
 * Code generated for Simulink model 'missile'.
 *
 * Model version                  : 1.2
 * Simulink Coder version         : 26.1 (R2026a) 20-Nov-2025
 * C/C++ source code generated on : Tue Aug 11 03:54:23 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: Intel->x86-64 (Windows64)
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "missile.h"
#include <math.h>
#include "rt_nonfinite.h"
#include <emmintrin.h>
#include "rtwtypes.h"
#include "missile_private.h"
#include <string.h>

/*
 * This function updates continuous states using the ODE4 fixed-step
 * solver algorithm
 */
static void rt_ertODEUpdateContinuousStates(RTWSolverInfo *si ,
  RT_MODEL_missile_T *const missile_M, real_T missile_U_r_T[3], real_T
  missile_U_v_T[3], real_T missile_U_t, real_T missile_Y_r_M_out[3], real_T
  missile_Y_v_M_out[3], real_T *missile_Y_term_flag_out)
{
  time_T t = rtsiGetT(si);
  time_T tnew = rtsiGetSolverStopTime(si);
  time_T h = rtsiGetStepSize(si);
  real_T *x = rtsiGetContStates(si);
  ODE4_IntgData *id = (ODE4_IntgData *)rtsiGetSolverData(si);
  real_T *y = id->y;
  real_T *f0 = id->f[0];
  real_T *f1 = id->f[1];
  real_T *f2 = id->f[2];
  real_T *f3 = id->f[3];
  real_T temp;
  int_T i;
  int_T nXc = 6;
  rtsiSetSimTimeStep(si,MINOR_TIME_STEP);

  /* Save the state values at time t in y, we'll use x as ynew. */
  (void) memcpy(y, x,
                (uint_T)nXc*sizeof(real_T));

  /* Assumes that rtsiSetT and ModelOutputs are up-to-date */
  /* f0 = f(t,y) */
  rtsiSetdX(si, f0);
  missile_derivatives(missile_M);

  /* f1 = f(t + (h/2), y + (h/2)*f0) */
  temp = 0.5 * h;
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + (temp*f0[i]);
  }

  rtsiSetT(si, t + temp);
  rtsiSetdX(si, f1);
  missile_step(missile_M, missile_U_r_T, missile_U_v_T, missile_U_t,
               missile_Y_r_M_out, missile_Y_v_M_out, missile_Y_term_flag_out);
  missile_derivatives(missile_M);

  /* f2 = f(t + (h/2), y + (h/2)*f1) */
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + (temp*f1[i]);
  }

  rtsiSetdX(si, f2);
  missile_step(missile_M, missile_U_r_T, missile_U_v_T, missile_U_t,
               missile_Y_r_M_out, missile_Y_v_M_out, missile_Y_term_flag_out);
  missile_derivatives(missile_M);

  /* f3 = f(t + h, y + h*f2) */
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + (h*f2[i]);
  }

  rtsiSetT(si, tnew);
  rtsiSetdX(si, f3);
  missile_step(missile_M, missile_U_r_T, missile_U_v_T, missile_U_t,
               missile_Y_r_M_out, missile_Y_v_M_out, missile_Y_term_flag_out);
  missile_derivatives(missile_M);

  /* tnew = t + h
     ynew = y + (h/6)*(f0 + 2*f1 + 2*f2 + 2*f3) */
  temp = h / 6.0;
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + temp*(f0[i] + 2.0*f1[i] + 2.0*f2[i] + f3[i]);
  }

  rtsiSetSimTimeStep(si,MAJOR_TIME_STEP);
}

/* Model step function */
void missile_step(RT_MODEL_missile_T *const missile_M, real_T missile_U_r_T[3],
                  real_T missile_U_v_T[3], real_T missile_U_t, real_T
                  missile_Y_r_M_out[3], real_T missile_Y_v_M_out[3], real_T
                  *missile_Y_term_flag_out)
{
  B_missile_T *missile_B = missile_M->blockIO;
  DW_missile_T *missile_DW = missile_M->dwork;
  X_missile_T *missile_X = missile_M->contStates;
  __m128d tmp_0;
  real_T rtb_r_tmp[3];
  real_T Vc;
  real_T absxk;
  real_T absxk_tmp;
  real_T absxk_tmp_0;
  real_T r_dot_idx_0;
  real_T r_dot_idx_1;
  real_T r_dot_idx_2;
  real_T r_mag;
  real_T scale;
  real_T t;
  int32_T b_k;
  int32_T exitg1;
  int32_T rtb_reversed;
  boolean_T tmp;
  if (rtmIsMajorTimeStep(missile_M)) {
    /* set solver stop time */
    rtsiSetSolverStopTime(missile_M->solverInfo,((missile_M->Timing.clockTick0+1)*
      missile_M->Timing.stepSize0));
  }                                    /* end MajorTimeStep */

  /* Update absolute time of base rate at minor time step */
  if (rtmIsMinorTimeStep(missile_M)) {
    missile_M->Timing.t[0] = rtsiGetT(missile_M->solverInfo);
  }

  /* MATLAB Function: '<S1>/LOS_Rate' */
  scale = 3.312168642111238E-170;

  /* Outport: '<Root>/r_M_out' incorporates:
   *  Integrator: '<S2>/Integrate_r'
   */
  missile_Y_r_M_out[0] = missile_X->Integrate_r_CSTATE[0];

  /* Integrator: '<S2>/Integrate_v' */
  missile_B->Integrate_v[0] = missile_X->Integrate_v_CSTATE[0];

  /* MATLAB Function: '<S1>/LOS_Rate' incorporates:
   *  Inport: '<Root>/r_T'
   *  Inport: '<Root>/v_T'
   *  Integrator: '<S2>/Integrate_r'
   *  Integrator: '<S2>/Integrate_v'
   *  MATLAB Function: '<S3>/Intercept_Check'
   */
  Vc = missile_U_r_T[0] - missile_X->Integrate_r_CSTATE[0];
  rtb_r_tmp[0] = Vc;
  r_dot_idx_0 = missile_U_v_T[0] - missile_X->Integrate_v_CSTATE[0];
  absxk_tmp = fabs(Vc);
  if (absxk_tmp > 3.312168642111238E-170) {
    r_mag = 1.0;
    scale = absxk_tmp;
  } else {
    t = absxk_tmp / 3.312168642111238E-170;
    r_mag = t * t;
  }

  /* Outport: '<Root>/r_M_out' incorporates:
   *  Integrator: '<S2>/Integrate_r'
   */
  missile_Y_r_M_out[1] = missile_X->Integrate_r_CSTATE[1];

  /* Integrator: '<S2>/Integrate_v' */
  missile_B->Integrate_v[1] = missile_X->Integrate_v_CSTATE[1];

  /* MATLAB Function: '<S1>/LOS_Rate' incorporates:
   *  Inport: '<Root>/r_T'
   *  Inport: '<Root>/v_T'
   *  Integrator: '<S2>/Integrate_r'
   *  Integrator: '<S2>/Integrate_v'
   *  MATLAB Function: '<S3>/Intercept_Check'
   */
  Vc = missile_U_r_T[1] - missile_X->Integrate_r_CSTATE[1];
  rtb_r_tmp[1] = Vc;
  r_dot_idx_1 = missile_U_v_T[1] - missile_X->Integrate_v_CSTATE[1];
  absxk_tmp_0 = fabs(Vc);
  if (absxk_tmp_0 > scale) {
    t = scale / absxk_tmp_0;
    r_mag = r_mag * t * t + 1.0;
    scale = absxk_tmp_0;
  } else {
    t = absxk_tmp_0 / scale;
    r_mag += t * t;
  }

  /* Outport: '<Root>/r_M_out' incorporates:
   *  Integrator: '<S2>/Integrate_r'
   */
  missile_Y_r_M_out[2] = missile_X->Integrate_r_CSTATE[2];

  /* Integrator: '<S2>/Integrate_v' */
  missile_B->Integrate_v[2] = missile_X->Integrate_v_CSTATE[2];

  /* MATLAB Function: '<S1>/LOS_Rate' incorporates:
   *  Inport: '<Root>/r_T'
   *  Inport: '<Root>/v_T'
   *  Integrator: '<S2>/Integrate_r'
   *  Integrator: '<S2>/Integrate_v'
   *  MATLAB Function: '<S3>/Intercept_Check'
   */
  Vc = missile_U_r_T[2] - missile_X->Integrate_r_CSTATE[2];
  rtb_r_tmp[2] = Vc;
  r_dot_idx_2 = missile_U_v_T[2] - missile_X->Integrate_v_CSTATE[2];
  absxk = fabs(Vc);
  if (absxk > scale) {
    t = scale / absxk;
    r_mag = r_mag * t * t + 1.0;
    scale = absxk;
  } else {
    t = absxk / scale;
    r_mag += t * t;
  }

  r_mag = scale * sqrt(r_mag);
  if (rtIsNaN(r_mag)) {
    b_k = 0;
    do {
      exitg1 = 0;
      if (b_k < 3) {
        if (rtIsNaN(rtb_r_tmp[b_k])) {
          exitg1 = 1;
        } else {
          b_k++;
        }
      } else {
        r_mag = (rtInf);
        exitg1 = 1;
      }
    } while (exitg1 == 0);
  }

  if (r_mag < 1.0E-6) {
    missile_B->a_cmd_sat[0] = 0.0;
    missile_B->a_cmd_sat[1] = 0.0;
    missile_B->a_cmd_sat[2] = 0.0;
    Vc = 0.0;
  } else {
    tmp_0 = _mm_set1_pd(r_mag);
    _mm_storeu_pd(&missile_B->a_cmd_sat[0], _mm_div_pd(_mm_sub_pd(_mm_mul_pd
      (_mm_set_pd(r_dot_idx_0, rtb_r_tmp[1]), _mm_set_pd(Vc, r_dot_idx_2)),
      _mm_mul_pd(_mm_set_pd(rtb_r_tmp[0], r_dot_idx_1), _mm_set_pd(r_dot_idx_2,
      Vc))), _mm_mul_pd(tmp_0, tmp_0)));
    missile_B->a_cmd_sat[2] = (rtb_r_tmp[0] * r_dot_idx_1 - r_dot_idx_0 *
      rtb_r_tmp[1]) / (r_mag * r_mag);
    Vc = -((rtb_r_tmp[0] * r_dot_idx_0 + rtb_r_tmp[1] * r_dot_idx_1) + Vc *
           r_dot_idx_2) / r_mag;
  }

  /* MATLAB Function: '<S1>/TPN_Law' */
  r_dot_idx_0 = 4.0 * Vc;

  /* MATLAB Function: '<S1>/Sat_a_cmd' */
  scale = 3.312168642111238E-170;

  /* MATLAB Function: '<S1>/TPN_Law' */
  missile_B->a_cmd_sat[0] *= r_dot_idx_0;

  /* MATLAB Function: '<S1>/Sat_a_cmd' */
  r_mag = fabs(missile_B->a_cmd_sat[0]);
  if (r_mag > 3.312168642111238E-170) {
    r_dot_idx_1 = 1.0;
    scale = r_mag;
  } else {
    t = r_mag / 3.312168642111238E-170;
    r_dot_idx_1 = t * t;
  }

  /* MATLAB Function: '<S1>/TPN_Law' */
  missile_B->a_cmd_sat[1] *= r_dot_idx_0;

  /* MATLAB Function: '<S1>/Sat_a_cmd' */
  r_mag = fabs(missile_B->a_cmd_sat[1]);
  if (r_mag > scale) {
    t = scale / r_mag;
    r_dot_idx_1 = r_dot_idx_1 * t * t + 1.0;
    scale = r_mag;
  } else {
    t = r_mag / scale;
    r_dot_idx_1 += t * t;
  }

  /* MATLAB Function: '<S1>/TPN_Law' */
  missile_B->a_cmd_sat[2] *= r_dot_idx_0;

  /* MATLAB Function: '<S1>/Sat_a_cmd' */
  r_mag = fabs(missile_B->a_cmd_sat[2]);
  if (r_mag > scale) {
    t = scale / r_mag;
    r_dot_idx_1 = r_dot_idx_1 * t * t + 1.0;
    scale = r_mag;
  } else {
    t = r_mag / scale;
    r_dot_idx_1 += t * t;
  }

  r_dot_idx_1 = scale * sqrt(r_dot_idx_1);
  if (rtIsNaN(r_dot_idx_1)) {
    b_k = 0;
    do {
      exitg1 = 0;
      if (b_k < 3) {
        if (rtIsNaN(missile_B->a_cmd_sat[b_k])) {
          exitg1 = 1;
        } else {
          b_k++;
        }
      } else {
        r_dot_idx_1 = (rtInf);
        exitg1 = 1;
      }
    } while (exitg1 == 0);
  }

  if (r_dot_idx_1 > 392.4) {
    scale = 392.4 / r_dot_idx_1;
    _mm_storeu_pd(&missile_B->a_cmd_sat[0], _mm_mul_pd(_mm_loadu_pd
      (&missile_B->a_cmd_sat[0]), _mm_set1_pd(scale)));
    missile_B->a_cmd_sat[2] *= scale;
  }

  /* Outport: '<Root>/v_M_out' */
  missile_Y_v_M_out[0] = missile_B->Integrate_v[0];
  missile_Y_v_M_out[1] = missile_B->Integrate_v[1];
  missile_Y_v_M_out[2] = missile_B->Integrate_v[2];
  tmp = (rtmIsMajorTimeStep(missile_M) &&
         missile_M->Timing.TaskCounters.TID[1] == 0);
  if (tmp) {
    /* MATLAB Function: '<S3>/LOS_Reversal_Check' */
    if (Vc > missile_DW->Vc_peak) {
      missile_DW->Vc_peak = Vc;
    }

    rtb_reversed = ((missile_DW->Vc_peak > 0.0) && (Vc < 0.0));

    /* End of MATLAB Function: '<S3>/LOS_Reversal_Check' */
  }

  /* MATLAB Function: '<S3>/Intercept_Check' */
  scale = 3.312168642111238E-170;
  if (absxk_tmp > 3.312168642111238E-170) {
    Vc = 1.0;
    scale = absxk_tmp;
  } else {
    t = absxk_tmp / 3.312168642111238E-170;
    Vc = t * t;
  }

  if (absxk_tmp_0 > scale) {
    t = scale / absxk_tmp_0;
    Vc = Vc * t * t + 1.0;
    scale = absxk_tmp_0;
  } else {
    t = absxk_tmp_0 / scale;
    Vc += t * t;
  }

  if (absxk > scale) {
    t = scale / absxk;
    Vc = Vc * t * t + 1.0;
    scale = absxk;
  } else {
    t = absxk / scale;
    Vc += t * t;
  }

  Vc = scale * sqrt(Vc);
  if (rtIsNaN(Vc)) {
    b_k = 0;
    do {
      exitg1 = 0;
      if (b_k < 3) {
        if (rtIsNaN(rtb_r_tmp[b_k])) {
          exitg1 = 1;
        } else {
          b_k++;
        }
      } else {
        Vc = (rtInf);
        exitg1 = 1;
      }
    } while (exitg1 == 0);
  }

  if (tmp) {
    /* MATLAB Function: '<S3>/Priority_Latch' incorporates:
     *  Inport: '<Root>/t'
     *  MATLAB Function: '<S3>/Intercept_Check'
     *  MATLAB Function: '<S3>/Timeout_Check'
     */
    if (missile_DW->latched == 0) {
      if (Vc < 25.0) {
        missile_DW->latched = 1;
      } else if (missile_U_t >= 60.0) {
        missile_DW->latched = 2;
      } else if (rtb_reversed != 0) {
        missile_DW->latched = 3;
      }
    }

    /* Outport: '<Root>/term_flag_out' incorporates:
     *  MATLAB Function: '<S3>/Priority_Latch'
     */
    *missile_Y_term_flag_out = missile_DW->latched;
  }

  if (rtmIsMajorTimeStep(missile_M)) {
    rt_ertODEUpdateContinuousStates(missile_M->solverInfo, missile_M,
      missile_U_r_T, missile_U_v_T, missile_U_t, missile_Y_r_M_out,
      missile_Y_v_M_out, missile_Y_term_flag_out);

    /* Update absolute time for base rate */
    /* The "clockTick0" counts the number of times the code of this task has
     * been executed. The absolute time is the multiplication of "clockTick0"
     * and "Timing.stepSize0". Size of "clockTick0" ensures timer will not
     * overflow during the application lifespan selected.
     */
    ++missile_M->Timing.clockTick0;
    missile_M->Timing.t[0] = rtsiGetSolverStopTime(missile_M->solverInfo);

    {
      /* Update absolute timer for sample time: [0.02s, 0.0s] */
      /* The "clockTick1" counts the number of times the code of this task has
       * been executed. The resolution of this integer timer is 0.02, which is the step size
       * of the task. Size of "clockTick1" ensures timer will not overflow during the
       * application lifespan selected.
       */
      missile_M->Timing.clockTick1++;
    }
  }                                    /* end MajorTimeStep */
}

/* Derivatives for root system: '<Root>' */
void missile_derivatives(RT_MODEL_missile_T *const missile_M)
{
  B_missile_T *missile_B = missile_M->blockIO;
  XDot_missile_T *_rtXdot;
  _rtXdot = ((XDot_missile_T *) missile_M->derivs);

  /* Derivatives for Integrator: '<S2>/Integrate_r' */
  _rtXdot->Integrate_r_CSTATE[0] = missile_B->Integrate_v[0];

  /* Derivatives for Integrator: '<S2>/Integrate_v' */
  _rtXdot->Integrate_v_CSTATE[0] = missile_B->a_cmd_sat[0];

  /* Derivatives for Integrator: '<S2>/Integrate_r' */
  _rtXdot->Integrate_r_CSTATE[1] = missile_B->Integrate_v[1];

  /* Derivatives for Integrator: '<S2>/Integrate_v' */
  _rtXdot->Integrate_v_CSTATE[1] = missile_B->a_cmd_sat[1];

  /* Derivatives for Integrator: '<S2>/Integrate_r' */
  _rtXdot->Integrate_r_CSTATE[2] = missile_B->Integrate_v[2];

  /* Derivatives for Integrator: '<S2>/Integrate_v' */
  _rtXdot->Integrate_v_CSTATE[2] = missile_B->a_cmd_sat[2];
}

/* Model initialize function */
void missile_initialize(RT_MODEL_missile_T *const missile_M)
{
  DW_missile_T *missile_DW = missile_M->dwork;
  X_missile_T *missile_X = missile_M->contStates;

  /* InitializeConditions for Integrator: '<S2>/Integrate_r' */
  missile_X->Integrate_r_CSTATE[0] = 0.0;

  /* InitializeConditions for Integrator: '<S2>/Integrate_v' */
  missile_X->Integrate_v_CSTATE[0] = 482.72860889636706;

  /* InitializeConditions for Integrator: '<S2>/Integrate_r' */
  missile_X->Integrate_r_CSTATE[1] = 0.0;

  /* InitializeConditions for Integrator: '<S2>/Integrate_v' */
  missile_X->Integrate_v_CSTATE[1] = 58.26335066399506;

  /* InitializeConditions for Integrator: '<S2>/Integrate_r' */
  missile_X->Integrate_r_CSTATE[2] = 0.0;

  /* InitializeConditions for Integrator: '<S2>/Integrate_v' */
  missile_X->Integrate_v_CSTATE[2] = 116.52670132799012;

  /* SystemInitialize for MATLAB Function: '<S3>/LOS_Reversal_Check' */
  missile_DW->Vc_peak = (rtMinusInf);

  /* SystemInitialize for MATLAB Function: '<S3>/Priority_Latch' */
  missile_DW->latched = 0;
}

/* Model terminate function */
void missile_terminate(RT_MODEL_missile_T * missile_M)
{
  rt_FREE(missile_M->solverInfo);

  /* model code */
  rt_FREE(missile_M->blockIO);
  rt_FREE(missile_M->contStates);
  rt_FREE(missile_M->dwork);
  rt_FREE(missile_M);
}

/* Model data allocation function */
RT_MODEL_missile_T *missile(real_T missile_U_r_T[3], real_T missile_U_v_T[3],
  real_T *missile_U_t, real_T missile_Y_r_M_out[3], real_T missile_Y_v_M_out[3],
  real_T *missile_Y_term_flag_out)
{
  RT_MODEL_missile_T *missile_M;
  missile_M = (RT_MODEL_missile_T *) malloc(sizeof(RT_MODEL_missile_T));
  if (missile_M == (NULL)) {
    return (NULL);
  }

  (void) memset((char *)missile_M, 0,
                sizeof(RT_MODEL_missile_T));

  {
    /* Setup solver object */
    RTWSolverInfo *rt_SolverInfo = (RTWSolverInfo *) malloc(sizeof(RTWSolverInfo));
    rt_VALIDATE_MEMORY(missile_M,rt_SolverInfo);
    missile_M->solverInfo = (rt_SolverInfo);
    rtsiSetSimTimeStepPtr(missile_M->solverInfo, &missile_M->Timing.simTimeStep);
    rtsiSetTPtr(missile_M->solverInfo, &rtmGetTPtr(missile_M));
    rtsiSetStepSizePtr(missile_M->solverInfo, &missile_M->Timing.stepSize0);
    rtsiSetdXPtr(missile_M->solverInfo, &missile_M->derivs);
    rtsiSetContStatesPtr(missile_M->solverInfo, (real_T **)
                         &missile_M->contStates);
    rtsiSetNumContStatesPtr(missile_M->solverInfo,
      &missile_M->Sizes.numContStates);
    rtsiSetNumPeriodicContStatesPtr(missile_M->solverInfo,
      &missile_M->Sizes.numPeriodicContStates);
    rtsiSetPeriodicContStateIndicesPtr(missile_M->solverInfo,
      &missile_M->periodicContStateIndices);
    rtsiSetPeriodicContStateRangesPtr(missile_M->solverInfo,
      &missile_M->periodicContStateRanges);
    rtsiSetContStateDisabledPtr(missile_M->solverInfo, (boolean_T**)
      &missile_M->contStateDisabled);
    rtsiSetErrorStatusPtr(missile_M->solverInfo, (&rtmGetErrorStatus(missile_M)));
    rtsiSetRTModelPtr(missile_M->solverInfo, missile_M);
  }

  rtsiSetSolverName(missile_M->solverInfo,"ode4");

  /* block I/O */
  {
    B_missile_T *b = (B_missile_T *) malloc(sizeof(B_missile_T));
    rt_VALIDATE_MEMORY(missile_M,b);
    missile_M->blockIO = (b);
  }

  /* states (continuous) */
  {
    X_missile_T *x = (X_missile_T *) malloc(sizeof(X_missile_T));
    rt_VALIDATE_MEMORY(missile_M,x);
    missile_M->contStates = (x);
  }

  /* disabled states */
  {
    XDis_missile_T *xdis = (XDis_missile_T *) malloc(sizeof(XDis_missile_T));
    rt_VALIDATE_MEMORY(missile_M,xdis);
    missile_M->contStateDisabled = (xdis);
  }

  /* states (dwork) */
  {
    DW_missile_T *dwork = (DW_missile_T *) malloc(sizeof(DW_missile_T));
    rt_VALIDATE_MEMORY(missile_M,dwork);
    missile_M->dwork = (dwork);
  }

  {
    B_missile_T *missile_B = missile_M->blockIO;
    DW_missile_T *missile_DW = missile_M->dwork;
    X_missile_T *missile_X = missile_M->contStates;
    XDis_missile_T *missile_XDis = ((XDis_missile_T *)
      missile_M->contStateDisabled);
    rtsiSetSimTimeStep(missile_M->solverInfo, MAJOR_TIME_STEP);
    rtsiSetIsMinorTimeStepWithModeChange(missile_M->solverInfo, false);
    rtsiSetIsContModeFrozen(missile_M->solverInfo, false);
    missile_M->intgData.y = missile_M->odeY;
    missile_M->intgData.f[0] = missile_M->odeF[0];
    missile_M->intgData.f[1] = missile_M->odeF[1];
    missile_M->intgData.f[2] = missile_M->odeF[2];
    missile_M->intgData.f[3] = missile_M->odeF[3];
    missile_M->contStates = ((X_missile_T *) missile_X);
    missile_M->contStateDisabled = ((XDis_missile_T *) missile_XDis);
    missile_M->Timing.tStart = (0.0);
    rtsiSetSolverData(missile_M->solverInfo, (void *)&missile_M->intgData);
    rtmSetTPtr(missile_M, &missile_M->Timing.tArray[0]);
    missile_M->Timing.stepSize0 = 0.02;

    /* block I/O */
    (void) memset(((void *) missile_B), 0,
                  sizeof(B_missile_T));

    /* states (continuous) */
    {
      (void) memset((void *)missile_X, 0,
                    sizeof(X_missile_T));
    }

    /* disabled states */
    {
      (void) memset((void *)missile_XDis, 0,
                    sizeof(XDis_missile_T));
    }

    /* states (dwork) */
    (void) memset((void *)missile_DW, 0,
                  sizeof(DW_missile_T));

    /* external inputs */
    (void)memset(&missile_U_r_T[0], 0, 3U * sizeof(real_T));
    (void)memset(&missile_U_v_T[0], 0, 3U * sizeof(real_T));
    *missile_U_t = 0.0;

    /* external outputs */
    (void)memset(&missile_Y_r_M_out[0], 0, 3U * sizeof(real_T));
    (void)memset(&missile_Y_v_M_out[0], 0, 3U * sizeof(real_T));
    *missile_Y_term_flag_out = 0.0;
  }

  return missile_M;
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
