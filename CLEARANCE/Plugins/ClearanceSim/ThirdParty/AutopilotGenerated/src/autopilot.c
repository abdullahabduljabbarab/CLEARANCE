/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: autopilot.c
 *
 * Code generated for Simulink model 'autopilot'.
 *
 * Model version                  : 1.25
 * Simulink Coder version         : 26.1 (R2026a) 20-Nov-2025
 * C/C++ source code generated on : Thu Jul  9 02:59:05 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: Intel->x86-64 (Windows64)
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "autopilot.h"
#include "rtwtypes.h"
#include "autopilot_private.h"
#include <string.h>

/*
 * This function updates continuous states using the ODE4 fixed-step
 * solver algorithm
 */
static void rt_ertODEUpdateContinuousStates(RTWSolverInfo *si ,
  RT_MODEL_autopilot_T *const autopilot_M)
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
  int_T nXc = 8;
  rtsiSetSimTimeStep(si,MINOR_TIME_STEP);

  /* Save the state values at time t in y, we'll use x as ynew. */
  (void) memcpy(y, x,
                (uint_T)nXc*sizeof(real_T));

  /* Assumes that rtsiSetT and ModelOutputs are up-to-date */
  /* f0 = f(t,y) */
  rtsiSetdX(si, f0);
  autopilot_derivatives(autopilot_M);

  /* f1 = f(t + (h/2), y + (h/2)*f0) */
  temp = 0.5 * h;
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + (temp*f0[i]);
  }

  rtsiSetT(si, t + temp);
  rtsiSetdX(si, f1);
  autopilot_step(autopilot_M);
  autopilot_derivatives(autopilot_M);

  /* f2 = f(t + (h/2), y + (h/2)*f1) */
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + (temp*f1[i]);
  }

  rtsiSetdX(si, f2);
  autopilot_step(autopilot_M);
  autopilot_derivatives(autopilot_M);

  /* f3 = f(t + h, y + h*f2) */
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + (h*f2[i]);
  }

  rtsiSetT(si, tnew);
  rtsiSetdX(si, f3);
  autopilot_step(autopilot_M);
  autopilot_derivatives(autopilot_M);

  /* tnew = t + h
     ynew = y + (h/6)*(f0 + 2*f1 + 2*f2 + 2*f3) */
  temp = h / 6.0;
  for (i = 0; i < nXc; i++) {
    x[i] = y[i] + temp*(f0[i] + 2.0*f1[i] + 2.0*f2[i] + f3[i]);
  }

  rtsiSetSimTimeStep(si,MAJOR_TIME_STEP);
}

/* Model step function */
void autopilot_step(RT_MODEL_autopilot_T *const autopilot_M)
{
  B_autopilot_T *autopilot_B = autopilot_M->blockIO;
  X_autopilot_T *autopilot_X = autopilot_M->contStates;
  ExtU_autopilot_T *autopilot_U = (ExtU_autopilot_T *) autopilot_M->inputs;
  ExtY_autopilot_T *autopilot_Y = (ExtY_autopilot_T *) autopilot_M->outputs;
  real_T rtb_Filter;
  if (rtmIsMajorTimeStep(autopilot_M)) {
    /* set solver stop time */
    rtsiSetSolverStopTime(&autopilot_M->solverInfo,
                          ((autopilot_M->Timing.clockTick0+1)*
      autopilot_M->Timing.stepSize0));
  }                                    /* end MajorTimeStep */

  /* Update absolute time of base rate at minor time step */
  if (rtmIsMinorTimeStep(autopilot_M)) {
    autopilot_M->Timing.t[0] = rtsiGetT(&autopilot_M->solverInfo);
  }

  /* Sum: '<S1>/Sum_V' */
  rtb_Filter = autopilot_U->V_cmd - autopilot_U->V;

  /* Gain: '<S42>/Filter Coefficient' incorporates:
   *  Gain: '<S32>/Derivative Gain'
   *  Integrator: '<S34>/Filter'
   *  Sum: '<S34>/SumD'
   */
  autopilot_B->FilterCoefficient = (0.0 * rtb_Filter -
    autopilot_X->Filter_CSTATE) * 100.0;

  /* Sum: '<S48>/Sum' incorporates:
   *  Gain: '<S44>/Proportional Gain'
   *  Integrator: '<S39>/Integrator'
   */
  autopilot_Y->delta_t_out = (0.05 * rtb_Filter + autopilot_X->Integrator_CSTATE)
    + autopilot_B->FilterCoefficient;

  /* Saturate: '<S1>/Sat_V' */
  if (autopilot_Y->delta_t_out > 1.0) {
    /* Sum: '<S48>/Sum' incorporates:
     *  Outport: '<Root>/delta_t_out'
     */
    autopilot_Y->delta_t_out = 1.0;
  } else if (autopilot_Y->delta_t_out < -0.4363323129985824) {
    /* Sum: '<S48>/Sum' incorporates:
     *  Outport: '<Root>/delta_t_out'
     */
    autopilot_Y->delta_t_out = -0.4363323129985824;
  }

  /* End of Saturate: '<S1>/Sat_V' */

  /* Gain: '<S36>/Integral Gain' */
  autopilot_B->IntegralGain = 0.02 * rtb_Filter;

  /* Sum: '<S1>/Sum_theta' */
  rtb_Filter = autopilot_U->theta_cmd - autopilot_U->theta;

  /* Gain: '<S146>/Filter Coefficient' incorporates:
   *  Gain: '<S136>/Derivative Gain'
   *  Integrator: '<S138>/Filter'
   *  Sum: '<S138>/SumD'
   */
  autopilot_B->FilterCoefficient_m = (0.05 * rtb_Filter -
    autopilot_X->Filter_CSTATE_p) * 100.0;

  /* Sum: '<S152>/Sum' incorporates:
   *  Gain: '<S148>/Proportional Gain'
   *  Integrator: '<S143>/Integrator'
   */
  autopilot_B->Sat_theta = (0.5 * rtb_Filter + autopilot_X->Integrator_CSTATE_a)
    + autopilot_B->FilterCoefficient_m;

  /* Saturate: '<S1>/Sat_theta' */
  if (autopilot_B->Sat_theta > 1.0E+6) {
    /* Sum: '<S152>/Sum' incorporates:
     *  Saturate: '<S1>/Sat_theta'
     */
    autopilot_B->Sat_theta = 1.0E+6;
  } else if (autopilot_B->Sat_theta < -1.0E+6) {
    /* Sum: '<S152>/Sum' incorporates:
     *  Saturate: '<S1>/Sat_theta'
     */
    autopilot_B->Sat_theta = -1.0E+6;
  }

  /* End of Saturate: '<S1>/Sat_theta' */

  /* Outport: '<Root>/delta_e_out' */
  autopilot_Y->delta_e_out = autopilot_B->Sat_theta;

  /* Gain: '<S140>/Integral Gain' */
  autopilot_B->IntegralGain_j = 0.0 * rtb_Filter;

  /* Sum: '<S1>/Sum_phi' */
  rtb_Filter = autopilot_U->phi_cmd - autopilot_U->phi;

  /* Gain: '<S94>/Filter Coefficient' incorporates:
   *  Gain: '<S84>/Derivative Gain'
   *  Integrator: '<S86>/Filter'
   *  Sum: '<S86>/SumD'
   */
  autopilot_B->FilterCoefficient_j = (0.05 * rtb_Filter -
    autopilot_X->Filter_CSTATE_l) * 100.0;

  /* Sum: '<S100>/Sum' incorporates:
   *  Gain: '<S96>/Proportional Gain'
   *  Integrator: '<S91>/Integrator'
   */
  autopilot_B->Sat_delta_a = (0.5 * rtb_Filter +
    autopilot_X->Integrator_CSTATE_ao) + autopilot_B->FilterCoefficient_j;

  /* Saturate: '<S1>/Sat_delta_a' */
  if (autopilot_B->Sat_delta_a > 0.4363323129985824) {
    /* Sum: '<S100>/Sum' incorporates:
     *  Saturate: '<S1>/Sat_delta_a'
     */
    autopilot_B->Sat_delta_a = 0.4363323129985824;
  } else if (autopilot_B->Sat_delta_a < -0.4363323129985824) {
    /* Sum: '<S100>/Sum' incorporates:
     *  Saturate: '<S1>/Sat_delta_a'
     */
    autopilot_B->Sat_delta_a = -0.4363323129985824;
  }

  /* End of Saturate: '<S1>/Sat_delta_a' */

  /* Outport: '<Root>/delta_a_out' */
  autopilot_Y->delta_a_out = autopilot_B->Sat_delta_a;

  /* Gain: '<S88>/Integral Gain' */
  autopilot_B->IntegralGain_d = 0.0 * rtb_Filter;
  if (rtmIsMajorTimeStep(autopilot_M)) {
    rt_ertODEUpdateContinuousStates(&autopilot_M->solverInfo, autopilot_M);

    /* Update absolute time for base rate */
    /* The "clockTick0" counts the number of times the code of this task has
     * been executed. The absolute time is the multiplication of "clockTick0"
     * and "Timing.stepSize0". Size of "clockTick0" ensures timer will not
     * overflow during the application lifespan selected.
     */
    ++autopilot_M->Timing.clockTick0;
    autopilot_M->Timing.t[0] = rtsiGetSolverStopTime(&autopilot_M->solverInfo);

    {
      /* Update absolute timer for sample time: [0.02s, 0.0s] */
      /* The "clockTick1" counts the number of times the code of this task has
       * been executed. The resolution of this integer timer is 0.02, which is the step size
       * of the task. Size of "clockTick1" ensures timer will not overflow during the
       * application lifespan selected.
       */
      autopilot_M->Timing.clockTick1++;
    }
  }                                    /* end MajorTimeStep */
}

/* Derivatives for root system: '<Root>' */
void autopilot_derivatives(RT_MODEL_autopilot_T *const autopilot_M)
{
  B_autopilot_T *autopilot_B = autopilot_M->blockIO;
  X_autopilot_T *autopilot_X = autopilot_M->contStates;
  XDot_autopilot_T *_rtXdot;
  _rtXdot = ((XDot_autopilot_T *) autopilot_M->derivs);

  /* Derivatives for Integrator: '<S39>/Integrator' */
  _rtXdot->Integrator_CSTATE = autopilot_B->IntegralGain;

  /* Derivatives for Integrator: '<S34>/Filter' */
  _rtXdot->Filter_CSTATE = autopilot_B->FilterCoefficient;

  /* Derivatives for Integrator: '<S143>/Integrator' */
  _rtXdot->Integrator_CSTATE_a = autopilot_B->IntegralGain_j;

  /* Derivatives for Integrator: '<S138>/Filter' */
  _rtXdot->Filter_CSTATE_p = autopilot_B->FilterCoefficient_m;

  /* Derivatives for Integrator: '<S91>/Integrator' */
  _rtXdot->Integrator_CSTATE_ao = autopilot_B->IntegralGain_d;

  /* Derivatives for Integrator: '<S86>/Filter' */
  _rtXdot->Filter_CSTATE_l = autopilot_B->FilterCoefficient_j;

  /* Derivatives for TransferFcn: '<Root>/ail_act' */
  _rtXdot->ail_act_CSTATE = -20.0 * autopilot_X->ail_act_CSTATE;
  _rtXdot->ail_act_CSTATE += autopilot_B->Sat_delta_a;

  /* Derivatives for TransferFcn: '<Root>/elev_act' */
  _rtXdot->elev_act_CSTATE = -20.0 * autopilot_X->elev_act_CSTATE;
  _rtXdot->elev_act_CSTATE += autopilot_B->Sat_theta;
}

/* Model initialize function */
void autopilot_initialize(RT_MODEL_autopilot_T *const autopilot_M)
{
  X_autopilot_T *autopilot_X = autopilot_M->contStates;
  B_autopilot_T *autopilot_B = autopilot_M->blockIO;
  XDis_autopilot_T *autopilot_XDis = ((XDis_autopilot_T *)
    autopilot_M->contStateDisabled);
  ExtU_autopilot_T *autopilot_U = (ExtU_autopilot_T *) autopilot_M->inputs;
  ExtY_autopilot_T *autopilot_Y = (ExtY_autopilot_T *) autopilot_M->outputs;

  /* Registration code */
  {
    /* Setup solver object */
    rtsiSetSimTimeStepPtr(&autopilot_M->solverInfo,
                          &autopilot_M->Timing.simTimeStep);
    rtsiSetTPtr(&autopilot_M->solverInfo, &rtmGetTPtr(autopilot_M));
    rtsiSetStepSizePtr(&autopilot_M->solverInfo, &autopilot_M->Timing.stepSize0);
    rtsiSetdXPtr(&autopilot_M->solverInfo, &autopilot_M->derivs);
    rtsiSetContStatesPtr(&autopilot_M->solverInfo, (real_T **)
                         &autopilot_M->contStates);
    rtsiSetNumContStatesPtr(&autopilot_M->solverInfo,
      &autopilot_M->Sizes.numContStates);
    rtsiSetNumPeriodicContStatesPtr(&autopilot_M->solverInfo,
      &autopilot_M->Sizes.numPeriodicContStates);
    rtsiSetPeriodicContStateIndicesPtr(&autopilot_M->solverInfo,
      &autopilot_M->periodicContStateIndices);
    rtsiSetPeriodicContStateRangesPtr(&autopilot_M->solverInfo,
      &autopilot_M->periodicContStateRanges);
    rtsiSetContStateDisabledPtr(&autopilot_M->solverInfo, (boolean_T**)
      &autopilot_M->contStateDisabled);
    rtsiSetErrorStatusPtr(&autopilot_M->solverInfo, (&rtmGetErrorStatus
      (autopilot_M)));
    rtsiSetRTModelPtr(&autopilot_M->solverInfo, autopilot_M);
  }

  rtsiSetSimTimeStep(&autopilot_M->solverInfo, MAJOR_TIME_STEP);
  rtsiSetIsMinorTimeStepWithModeChange(&autopilot_M->solverInfo, false);
  rtsiSetIsContModeFrozen(&autopilot_M->solverInfo, false);
  autopilot_M->intgData.y = autopilot_M->odeY;
  autopilot_M->intgData.f[0] = autopilot_M->odeF[0];
  autopilot_M->intgData.f[1] = autopilot_M->odeF[1];
  autopilot_M->intgData.f[2] = autopilot_M->odeF[2];
  autopilot_M->intgData.f[3] = autopilot_M->odeF[3];
  autopilot_M->contStates = ((X_autopilot_T *) autopilot_X);
  autopilot_M->contStateDisabled = ((XDis_autopilot_T *) autopilot_XDis);
  autopilot_M->Timing.tStart = (0.0);
  rtsiSetSolverData(&autopilot_M->solverInfo, (void *)&autopilot_M->intgData);
  rtsiSetSolverName(&autopilot_M->solverInfo,"ode4");
  rtmSetTPtr(autopilot_M, &autopilot_M->Timing.tArray[0]);
  autopilot_M->Timing.stepSize0 = 0.02;

  /* block I/O */
  (void) memset(((void *) autopilot_B), 0,
                sizeof(B_autopilot_T));

  /* states (continuous) */
  {
    (void) memset((void *)autopilot_X, 0,
                  sizeof(X_autopilot_T));
  }

  /* disabled states */
  {
    (void) memset((void *)autopilot_XDis, 0,
                  sizeof(XDis_autopilot_T));
  }

  /* external inputs */
  (void)memset(autopilot_U, 0, sizeof(ExtU_autopilot_T));

  /* external outputs */
  (void)memset(autopilot_Y, 0, sizeof(ExtY_autopilot_T));

  /* InitializeConditions for Integrator: '<S39>/Integrator' */
  autopilot_X->Integrator_CSTATE = 0.0;

  /* InitializeConditions for Integrator: '<S34>/Filter' */
  autopilot_X->Filter_CSTATE = 0.0;

  /* InitializeConditions for Integrator: '<S143>/Integrator' */
  autopilot_X->Integrator_CSTATE_a = 0.0;

  /* InitializeConditions for Integrator: '<S138>/Filter' */
  autopilot_X->Filter_CSTATE_p = 0.0;

  /* InitializeConditions for Integrator: '<S91>/Integrator' */
  autopilot_X->Integrator_CSTATE_ao = 0.0;

  /* InitializeConditions for Integrator: '<S86>/Filter' */
  autopilot_X->Filter_CSTATE_l = 0.0;

  /* InitializeConditions for TransferFcn: '<Root>/ail_act' */
  autopilot_X->ail_act_CSTATE = 0.0;

  /* InitializeConditions for TransferFcn: '<Root>/elev_act' */
  autopilot_X->elev_act_CSTATE = 0.0;
}

/* Model terminate function */
void autopilot_terminate(RT_MODEL_autopilot_T *const autopilot_M)
{
  /* (no terminate code required) */
  UNUSED_PARAMETER(autopilot_M);
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
