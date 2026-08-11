/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: missile.h
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

#ifndef missile_h_
#define missile_h_
#ifndef missile_COMMON_INCLUDES_
#define missile_COMMON_INCLUDES_
#include <stdlib.h>
#include "rtwtypes.h"
#include "rtw_continuous.h"
#include "rtw_solver.h"
#include "rt_nonfinite.h"
#include "math.h"
#endif                                 /* missile_COMMON_INCLUDES_ */

#include "missile_types.h"
#include "rtGetInf.h"
#include <string.h>
#include <stddef.h>

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

#ifndef rtmGetStopRequested
#define rtmGetStopRequested(rtm)       ((rtm)->Timing.stopRequestedFlag)
#endif

#ifndef rtmSetStopRequested
#define rtmSetStopRequested(rtm, val)  ((rtm)->Timing.stopRequestedFlag = (val))
#endif

#ifndef rtmGetStopRequestedPtr
#define rtmGetStopRequestedPtr(rtm)    (&((rtm)->Timing.stopRequestedFlag))
#endif

#ifndef rtmGetT
#define rtmGetT(rtm)                   (rtmGetTPtr((rtm))[0])
#endif

#ifndef rtmGetTPtr
#define rtmGetTPtr(rtm)                ((rtm)->Timing.t)
#endif

#ifndef rtmGetTStart
#define rtmGetTStart(rtm)              ((rtm)->Timing.tStart)
#endif

/* Block signals (default storage) */
typedef struct {
  real_T Integrate_v[3];               /* '<S2>/Integrate_v' */
  real_T a_cmd_sat[3];                 /* '<S1>/Sat_a_cmd' */
} B_missile_T;

/* Block states (default storage) for system '<Root>' */
typedef struct {
  real_T Vc_peak;                      /* '<S3>/LOS_Reversal_Check' */
  int32_T latched;                     /* '<S3>/Priority_Latch' */
} DW_missile_T;

/* Continuous states (default storage) */
typedef struct {
  real_T Integrate_r_CSTATE[3];        /* '<S2>/Integrate_r' */
  real_T Integrate_v_CSTATE[3];        /* '<S2>/Integrate_v' */
} X_missile_T;

/* State derivatives (default storage) */
typedef struct {
  real_T Integrate_r_CSTATE[3];        /* '<S2>/Integrate_r' */
  real_T Integrate_v_CSTATE[3];        /* '<S2>/Integrate_v' */
} XDot_missile_T;

/* State disabled  */
typedef struct {
  boolean_T Integrate_r_CSTATE[3];     /* '<S2>/Integrate_r' */
  boolean_T Integrate_v_CSTATE[3];     /* '<S2>/Integrate_v' */
} XDis_missile_T;

#ifndef ODE4_INTG
#define ODE4_INTG

/* ODE4 Integration Data */
typedef struct {
  real_T *y;                           /* output */
  real_T *f[4];                        /* derivatives */
} ODE4_IntgData;

#endif

/* Real-time Model Data Structure */
struct tag_RTM_missile_T {
  const char_T *errorStatus;
  RTWSolverInfo *solverInfo;
  B_missile_T *blockIO;
  X_missile_T *contStates;
  int_T *periodicContStateIndices;
  real_T *periodicContStateRanges;
  real_T *derivs;
  XDis_missile_T *contStateDisabled;
  boolean_T zCCacheNeedsReset;
  boolean_T derivCacheNeedsReset;
  boolean_T CTOutputIncnstWithState;
  real_T odeY[6];
  real_T odeF[4][6];
  ODE4_IntgData intgData;
  DW_missile_T *dwork;

  /*
   * Sizes:
   * The following substructure contains sizes information
   * for many of the model attributes such as inputs, outputs,
   * dwork, sample times, etc.
   */
  struct {
    int_T numContStates;
    int_T numPeriodicContStates;
    int_T numSampTimes;
  } Sizes;

  /*
   * Timing:
   * The following substructure contains information regarding
   * the timing information for the model.
   */
  struct {
    uint32_T clockTick0;
    time_T stepSize0;
    uint32_T clockTick1;
    struct {
      uint8_T TID[2];
    } TaskCounters;

    time_T tStart;
    SimTimeStep simTimeStep;
    boolean_T stopRequestedFlag;
    time_T *t;
    time_T tArray[2];
  } Timing;
};

/* External data declarations for dependent source files */
extern const char_T *RT_MEMORY_ALLOCATION_ERROR;

/* Model entry point functions */
extern RT_MODEL_missile_T *missile(real_T missile_U_r_T[3], real_T
  missile_U_v_T[3], real_T *missile_U_t, real_T missile_Y_r_M_out[3], real_T
  missile_Y_v_M_out[3], real_T *missile_Y_term_flag_out);
extern void missile_initialize(RT_MODEL_missile_T *const missile_M);
extern void missile_step(RT_MODEL_missile_T *const missile_M, real_T
  missile_U_r_T[3], real_T missile_U_v_T[3], real_T missile_U_t, real_T
  missile_Y_r_M_out[3], real_T missile_Y_v_M_out[3], real_T
  *missile_Y_term_flag_out);
extern void missile_terminate(RT_MODEL_missile_T * missile_M);

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<S1>/Closing_Velocity' : Eliminated nontunable gain of 1
 * Block '<S1>/LOS_Rate_Probe' : Eliminated nontunable gain of 1
 */

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Use the MATLAB hilite_system command to trace the generated code back
 * to the model.  For example,
 *
 * hilite_system('<S3>')    - opens system 3
 * hilite_system('<S3>/Kp') - opens and selects block Kp which resides in S3
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'missile'
 * '<S1>'   : 'missile/GuidanceSubsystem'
 * '<S2>'   : 'missile/KinematicSubsystem'
 * '<S3>'   : 'missile/TerminationSubsystem'
 * '<S4>'   : 'missile/GuidanceSubsystem/LOS_Rate'
 * '<S5>'   : 'missile/GuidanceSubsystem/Sat_a_cmd'
 * '<S6>'   : 'missile/GuidanceSubsystem/TPN_Law'
 * '<S7>'   : 'missile/TerminationSubsystem/Intercept_Check'
 * '<S8>'   : 'missile/TerminationSubsystem/LOS_Reversal_Check'
 * '<S9>'   : 'missile/TerminationSubsystem/Priority_Latch'
 * '<S10>'  : 'missile/TerminationSubsystem/Timeout_Check'
 */
#endif                                 /* missile_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
