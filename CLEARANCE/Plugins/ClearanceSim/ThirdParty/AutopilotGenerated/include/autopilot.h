/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: autopilot.h
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

#ifndef autopilot_h_
#define autopilot_h_
#ifndef autopilot_COMMON_INCLUDES_
#define autopilot_COMMON_INCLUDES_
#include "rtwtypes.h"
#include "rtw_continuous.h"
#include "rtw_solver.h"
#include "math.h"
#endif                                 /* autopilot_COMMON_INCLUDES_ */

#include "autopilot_types.h"
#include <string.h>
#include "rt_defines.h"

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
  real_T FilterCoefficient;            /* '<S42>/Filter Coefficient' */
  real_T IntegralGain;                 /* '<S36>/Integral Gain' */
  real_T FilterCoefficient_m;          /* '<S146>/Filter Coefficient' */
  real_T Sat_theta;                    /* '<S1>/Sat_theta' */
  real_T IntegralGain_j;               /* '<S140>/Integral Gain' */
  real_T FilterCoefficient_j;          /* '<S94>/Filter Coefficient' */
  real_T Sat_delta_a;                  /* '<S1>/Sat_delta_a' */
  real_T IntegralGain_d;               /* '<S88>/Integral Gain' */
} B_autopilot_T;

/* Continuous states (default storage) */
typedef struct {
  real_T Integrator_CSTATE;            /* '<S39>/Integrator' */
  real_T Filter_CSTATE;                /* '<S34>/Filter' */
  real_T Integrator_CSTATE_a;          /* '<S143>/Integrator' */
  real_T Filter_CSTATE_p;              /* '<S138>/Filter' */
  real_T Integrator_CSTATE_ao;         /* '<S91>/Integrator' */
  real_T Filter_CSTATE_l;              /* '<S86>/Filter' */
  real_T ail_act_CSTATE;               /* '<Root>/ail_act' */
  real_T elev_act_CSTATE;              /* '<Root>/elev_act' */
} X_autopilot_T;

/* State derivatives (default storage) */
typedef struct {
  real_T Integrator_CSTATE;            /* '<S39>/Integrator' */
  real_T Filter_CSTATE;                /* '<S34>/Filter' */
  real_T Integrator_CSTATE_a;          /* '<S143>/Integrator' */
  real_T Filter_CSTATE_p;              /* '<S138>/Filter' */
  real_T Integrator_CSTATE_ao;         /* '<S91>/Integrator' */
  real_T Filter_CSTATE_l;              /* '<S86>/Filter' */
  real_T ail_act_CSTATE;               /* '<Root>/ail_act' */
  real_T elev_act_CSTATE;              /* '<Root>/elev_act' */
} XDot_autopilot_T;

/* State disabled  */
typedef struct {
  boolean_T Integrator_CSTATE;         /* '<S39>/Integrator' */
  boolean_T Filter_CSTATE;             /* '<S34>/Filter' */
  boolean_T Integrator_CSTATE_a;       /* '<S143>/Integrator' */
  boolean_T Filter_CSTATE_p;           /* '<S138>/Filter' */
  boolean_T Integrator_CSTATE_ao;      /* '<S91>/Integrator' */
  boolean_T Filter_CSTATE_l;           /* '<S86>/Filter' */
  boolean_T ail_act_CSTATE;            /* '<Root>/ail_act' */
  boolean_T elev_act_CSTATE;           /* '<Root>/elev_act' */
} XDis_autopilot_T;

#ifndef ODE4_INTG
#define ODE4_INTG

/* ODE4 Integration Data */
typedef struct {
  real_T *y;                           /* output */
  real_T *f[4];                        /* derivatives */
} ODE4_IntgData;

#endif

/* External inputs (root inport signals with default storage) */
typedef struct {
  real_T V_cmd;                        /* '<Root>/V_cmd' */
  real_T phi;                          /* '<Root>/phi' */
  real_T theta;                        /* '<Root>/theta' */
  real_T V;                            /* '<Root>/V' */
  real_T phi_cmd;                      /* '<Root>/phi_cmd' */
  real_T theta_cmd;                    /* '<Root>/theta_cmd' */
} ExtU_autopilot_T;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  real_T delta_t_out;                  /* '<Root>/delta_t_out' */
  real_T delta_e_out;                  /* '<Root>/delta_e_out' */
  real_T delta_a_out;                  /* '<Root>/delta_a_out' */
} ExtY_autopilot_T;

/* Real-time Model Data Structure */
struct tag_RTM_autopilot_T {
  const char_T *errorStatus;
  RTWSolverInfo solverInfo;
  B_autopilot_T *blockIO;
  X_autopilot_T *contStates;
  int_T *periodicContStateIndices;
  real_T *periodicContStateRanges;
  real_T *derivs;
  ExtU_autopilot_T *inputs;
  ExtY_autopilot_T *outputs;
  XDis_autopilot_T *contStateDisabled;
  boolean_T zCCacheNeedsReset;
  boolean_T derivCacheNeedsReset;
  boolean_T CTOutputIncnstWithState;
  real_T odeY[8];
  real_T odeF[4][8];
  ODE4_IntgData intgData;

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

/* Model entry point functions */
extern void autopilot_initialize(RT_MODEL_autopilot_T *const autopilot_M);
extern void autopilot_step(RT_MODEL_autopilot_T *const autopilot_M);
extern void autopilot_terminate(RT_MODEL_autopilot_T *const autopilot_M);

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<S1>/delta_e_probe_out1' : Unused code path elimination
 * Block '<S1>/delta_e_probe_out2' : Unused code path elimination
 * Block '<S1>/delta_e_probe_out3' : Unused code path elimination
 * Block '<S1>/phi_in_probe' : Unused code path elimination
 * Block '<S1>/phi_to_ws' : Unused code path elimination
 * Block '<S1>/pid_theta_out_probe' : Unused code path elimination
 * Block '<S1>/sat_theta_input_probe' : Unused code path elimination
 * Block '<Root>/delta_e_log_ws' : Unused code path elimination
 * Block '<Root>/delta_e_scope' : Unused code path elimination
 * Block '<Root>/delta_e_to_ws' : Unused code path elimination
 * Block '<Root>/probe_force_phi' : Unused code path elimination
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
 * '<Root>' : 'autopilot'
 * '<S1>'   : 'autopilot/AutopilotSubsystem'
 * '<S2>'   : 'autopilot/AutopilotSubsystem/PID_V'
 * '<S3>'   : 'autopilot/AutopilotSubsystem/PID_phi'
 * '<S4>'   : 'autopilot/AutopilotSubsystem/PID_theta'
 * '<S5>'   : 'autopilot/AutopilotSubsystem/PID_V/Anti-windup'
 * '<S6>'   : 'autopilot/AutopilotSubsystem/PID_V/D Gain'
 * '<S7>'   : 'autopilot/AutopilotSubsystem/PID_V/External Derivative'
 * '<S8>'   : 'autopilot/AutopilotSubsystem/PID_V/Filter'
 * '<S9>'   : 'autopilot/AutopilotSubsystem/PID_V/Filter ICs'
 * '<S10>'  : 'autopilot/AutopilotSubsystem/PID_V/I Gain'
 * '<S11>'  : 'autopilot/AutopilotSubsystem/PID_V/Ideal P Gain'
 * '<S12>'  : 'autopilot/AutopilotSubsystem/PID_V/Ideal P Gain Fdbk'
 * '<S13>'  : 'autopilot/AutopilotSubsystem/PID_V/Integrator'
 * '<S14>'  : 'autopilot/AutopilotSubsystem/PID_V/Integrator ICs'
 * '<S15>'  : 'autopilot/AutopilotSubsystem/PID_V/N Copy'
 * '<S16>'  : 'autopilot/AutopilotSubsystem/PID_V/N Gain'
 * '<S17>'  : 'autopilot/AutopilotSubsystem/PID_V/P Copy'
 * '<S18>'  : 'autopilot/AutopilotSubsystem/PID_V/Parallel P Gain'
 * '<S19>'  : 'autopilot/AutopilotSubsystem/PID_V/Reset Signal'
 * '<S20>'  : 'autopilot/AutopilotSubsystem/PID_V/Saturation'
 * '<S21>'  : 'autopilot/AutopilotSubsystem/PID_V/Saturation Fdbk'
 * '<S22>'  : 'autopilot/AutopilotSubsystem/PID_V/Sum'
 * '<S23>'  : 'autopilot/AutopilotSubsystem/PID_V/Sum Fdbk'
 * '<S24>'  : 'autopilot/AutopilotSubsystem/PID_V/Tracking Mode'
 * '<S25>'  : 'autopilot/AutopilotSubsystem/PID_V/Tracking Mode Sum'
 * '<S26>'  : 'autopilot/AutopilotSubsystem/PID_V/Tsamp - Integral'
 * '<S27>'  : 'autopilot/AutopilotSubsystem/PID_V/Tsamp - Ngain'
 * '<S28>'  : 'autopilot/AutopilotSubsystem/PID_V/postSat Signal'
 * '<S29>'  : 'autopilot/AutopilotSubsystem/PID_V/preInt Signal'
 * '<S30>'  : 'autopilot/AutopilotSubsystem/PID_V/preSat Signal'
 * '<S31>'  : 'autopilot/AutopilotSubsystem/PID_V/Anti-windup/Passthrough'
 * '<S32>'  : 'autopilot/AutopilotSubsystem/PID_V/D Gain/Internal Parameters'
 * '<S33>'  : 'autopilot/AutopilotSubsystem/PID_V/External Derivative/Error'
 * '<S34>'  : 'autopilot/AutopilotSubsystem/PID_V/Filter/Cont. Filter'
 * '<S35>'  : 'autopilot/AutopilotSubsystem/PID_V/Filter ICs/Internal IC - Filter'
 * '<S36>'  : 'autopilot/AutopilotSubsystem/PID_V/I Gain/Internal Parameters'
 * '<S37>'  : 'autopilot/AutopilotSubsystem/PID_V/Ideal P Gain/Passthrough'
 * '<S38>'  : 'autopilot/AutopilotSubsystem/PID_V/Ideal P Gain Fdbk/Disabled'
 * '<S39>'  : 'autopilot/AutopilotSubsystem/PID_V/Integrator/Continuous'
 * '<S40>'  : 'autopilot/AutopilotSubsystem/PID_V/Integrator ICs/Internal IC'
 * '<S41>'  : 'autopilot/AutopilotSubsystem/PID_V/N Copy/Disabled'
 * '<S42>'  : 'autopilot/AutopilotSubsystem/PID_V/N Gain/Internal Parameters'
 * '<S43>'  : 'autopilot/AutopilotSubsystem/PID_V/P Copy/Disabled'
 * '<S44>'  : 'autopilot/AutopilotSubsystem/PID_V/Parallel P Gain/Internal Parameters'
 * '<S45>'  : 'autopilot/AutopilotSubsystem/PID_V/Reset Signal/Disabled'
 * '<S46>'  : 'autopilot/AutopilotSubsystem/PID_V/Saturation/Passthrough'
 * '<S47>'  : 'autopilot/AutopilotSubsystem/PID_V/Saturation Fdbk/Disabled'
 * '<S48>'  : 'autopilot/AutopilotSubsystem/PID_V/Sum/Sum_PID'
 * '<S49>'  : 'autopilot/AutopilotSubsystem/PID_V/Sum Fdbk/Disabled'
 * '<S50>'  : 'autopilot/AutopilotSubsystem/PID_V/Tracking Mode/Disabled'
 * '<S51>'  : 'autopilot/AutopilotSubsystem/PID_V/Tracking Mode Sum/Passthrough'
 * '<S52>'  : 'autopilot/AutopilotSubsystem/PID_V/Tsamp - Integral/TsSignalSpecification'
 * '<S53>'  : 'autopilot/AutopilotSubsystem/PID_V/Tsamp - Ngain/Passthrough'
 * '<S54>'  : 'autopilot/AutopilotSubsystem/PID_V/postSat Signal/Forward_Path'
 * '<S55>'  : 'autopilot/AutopilotSubsystem/PID_V/preInt Signal/Internal PreInt'
 * '<S56>'  : 'autopilot/AutopilotSubsystem/PID_V/preSat Signal/Forward_Path'
 * '<S57>'  : 'autopilot/AutopilotSubsystem/PID_phi/Anti-windup'
 * '<S58>'  : 'autopilot/AutopilotSubsystem/PID_phi/D Gain'
 * '<S59>'  : 'autopilot/AutopilotSubsystem/PID_phi/External Derivative'
 * '<S60>'  : 'autopilot/AutopilotSubsystem/PID_phi/Filter'
 * '<S61>'  : 'autopilot/AutopilotSubsystem/PID_phi/Filter ICs'
 * '<S62>'  : 'autopilot/AutopilotSubsystem/PID_phi/I Gain'
 * '<S63>'  : 'autopilot/AutopilotSubsystem/PID_phi/Ideal P Gain'
 * '<S64>'  : 'autopilot/AutopilotSubsystem/PID_phi/Ideal P Gain Fdbk'
 * '<S65>'  : 'autopilot/AutopilotSubsystem/PID_phi/Integrator'
 * '<S66>'  : 'autopilot/AutopilotSubsystem/PID_phi/Integrator ICs'
 * '<S67>'  : 'autopilot/AutopilotSubsystem/PID_phi/N Copy'
 * '<S68>'  : 'autopilot/AutopilotSubsystem/PID_phi/N Gain'
 * '<S69>'  : 'autopilot/AutopilotSubsystem/PID_phi/P Copy'
 * '<S70>'  : 'autopilot/AutopilotSubsystem/PID_phi/Parallel P Gain'
 * '<S71>'  : 'autopilot/AutopilotSubsystem/PID_phi/Reset Signal'
 * '<S72>'  : 'autopilot/AutopilotSubsystem/PID_phi/Saturation'
 * '<S73>'  : 'autopilot/AutopilotSubsystem/PID_phi/Saturation Fdbk'
 * '<S74>'  : 'autopilot/AutopilotSubsystem/PID_phi/Sum'
 * '<S75>'  : 'autopilot/AutopilotSubsystem/PID_phi/Sum Fdbk'
 * '<S76>'  : 'autopilot/AutopilotSubsystem/PID_phi/Tracking Mode'
 * '<S77>'  : 'autopilot/AutopilotSubsystem/PID_phi/Tracking Mode Sum'
 * '<S78>'  : 'autopilot/AutopilotSubsystem/PID_phi/Tsamp - Integral'
 * '<S79>'  : 'autopilot/AutopilotSubsystem/PID_phi/Tsamp - Ngain'
 * '<S80>'  : 'autopilot/AutopilotSubsystem/PID_phi/postSat Signal'
 * '<S81>'  : 'autopilot/AutopilotSubsystem/PID_phi/preInt Signal'
 * '<S82>'  : 'autopilot/AutopilotSubsystem/PID_phi/preSat Signal'
 * '<S83>'  : 'autopilot/AutopilotSubsystem/PID_phi/Anti-windup/Passthrough'
 * '<S84>'  : 'autopilot/AutopilotSubsystem/PID_phi/D Gain/Internal Parameters'
 * '<S85>'  : 'autopilot/AutopilotSubsystem/PID_phi/External Derivative/Error'
 * '<S86>'  : 'autopilot/AutopilotSubsystem/PID_phi/Filter/Cont. Filter'
 * '<S87>'  : 'autopilot/AutopilotSubsystem/PID_phi/Filter ICs/Internal IC - Filter'
 * '<S88>'  : 'autopilot/AutopilotSubsystem/PID_phi/I Gain/Internal Parameters'
 * '<S89>'  : 'autopilot/AutopilotSubsystem/PID_phi/Ideal P Gain/Passthrough'
 * '<S90>'  : 'autopilot/AutopilotSubsystem/PID_phi/Ideal P Gain Fdbk/Disabled'
 * '<S91>'  : 'autopilot/AutopilotSubsystem/PID_phi/Integrator/Continuous'
 * '<S92>'  : 'autopilot/AutopilotSubsystem/PID_phi/Integrator ICs/Internal IC'
 * '<S93>'  : 'autopilot/AutopilotSubsystem/PID_phi/N Copy/Disabled'
 * '<S94>'  : 'autopilot/AutopilotSubsystem/PID_phi/N Gain/Internal Parameters'
 * '<S95>'  : 'autopilot/AutopilotSubsystem/PID_phi/P Copy/Disabled'
 * '<S96>'  : 'autopilot/AutopilotSubsystem/PID_phi/Parallel P Gain/Internal Parameters'
 * '<S97>'  : 'autopilot/AutopilotSubsystem/PID_phi/Reset Signal/Disabled'
 * '<S98>'  : 'autopilot/AutopilotSubsystem/PID_phi/Saturation/Passthrough'
 * '<S99>'  : 'autopilot/AutopilotSubsystem/PID_phi/Saturation Fdbk/Disabled'
 * '<S100>' : 'autopilot/AutopilotSubsystem/PID_phi/Sum/Sum_PID'
 * '<S101>' : 'autopilot/AutopilotSubsystem/PID_phi/Sum Fdbk/Disabled'
 * '<S102>' : 'autopilot/AutopilotSubsystem/PID_phi/Tracking Mode/Disabled'
 * '<S103>' : 'autopilot/AutopilotSubsystem/PID_phi/Tracking Mode Sum/Passthrough'
 * '<S104>' : 'autopilot/AutopilotSubsystem/PID_phi/Tsamp - Integral/TsSignalSpecification'
 * '<S105>' : 'autopilot/AutopilotSubsystem/PID_phi/Tsamp - Ngain/Passthrough'
 * '<S106>' : 'autopilot/AutopilotSubsystem/PID_phi/postSat Signal/Forward_Path'
 * '<S107>' : 'autopilot/AutopilotSubsystem/PID_phi/preInt Signal/Internal PreInt'
 * '<S108>' : 'autopilot/AutopilotSubsystem/PID_phi/preSat Signal/Forward_Path'
 * '<S109>' : 'autopilot/AutopilotSubsystem/PID_theta/Anti-windup'
 * '<S110>' : 'autopilot/AutopilotSubsystem/PID_theta/D Gain'
 * '<S111>' : 'autopilot/AutopilotSubsystem/PID_theta/External Derivative'
 * '<S112>' : 'autopilot/AutopilotSubsystem/PID_theta/Filter'
 * '<S113>' : 'autopilot/AutopilotSubsystem/PID_theta/Filter ICs'
 * '<S114>' : 'autopilot/AutopilotSubsystem/PID_theta/I Gain'
 * '<S115>' : 'autopilot/AutopilotSubsystem/PID_theta/Ideal P Gain'
 * '<S116>' : 'autopilot/AutopilotSubsystem/PID_theta/Ideal P Gain Fdbk'
 * '<S117>' : 'autopilot/AutopilotSubsystem/PID_theta/Integrator'
 * '<S118>' : 'autopilot/AutopilotSubsystem/PID_theta/Integrator ICs'
 * '<S119>' : 'autopilot/AutopilotSubsystem/PID_theta/N Copy'
 * '<S120>' : 'autopilot/AutopilotSubsystem/PID_theta/N Gain'
 * '<S121>' : 'autopilot/AutopilotSubsystem/PID_theta/P Copy'
 * '<S122>' : 'autopilot/AutopilotSubsystem/PID_theta/Parallel P Gain'
 * '<S123>' : 'autopilot/AutopilotSubsystem/PID_theta/Reset Signal'
 * '<S124>' : 'autopilot/AutopilotSubsystem/PID_theta/Saturation'
 * '<S125>' : 'autopilot/AutopilotSubsystem/PID_theta/Saturation Fdbk'
 * '<S126>' : 'autopilot/AutopilotSubsystem/PID_theta/Sum'
 * '<S127>' : 'autopilot/AutopilotSubsystem/PID_theta/Sum Fdbk'
 * '<S128>' : 'autopilot/AutopilotSubsystem/PID_theta/Tracking Mode'
 * '<S129>' : 'autopilot/AutopilotSubsystem/PID_theta/Tracking Mode Sum'
 * '<S130>' : 'autopilot/AutopilotSubsystem/PID_theta/Tsamp - Integral'
 * '<S131>' : 'autopilot/AutopilotSubsystem/PID_theta/Tsamp - Ngain'
 * '<S132>' : 'autopilot/AutopilotSubsystem/PID_theta/postSat Signal'
 * '<S133>' : 'autopilot/AutopilotSubsystem/PID_theta/preInt Signal'
 * '<S134>' : 'autopilot/AutopilotSubsystem/PID_theta/preSat Signal'
 * '<S135>' : 'autopilot/AutopilotSubsystem/PID_theta/Anti-windup/Passthrough'
 * '<S136>' : 'autopilot/AutopilotSubsystem/PID_theta/D Gain/Internal Parameters'
 * '<S137>' : 'autopilot/AutopilotSubsystem/PID_theta/External Derivative/Error'
 * '<S138>' : 'autopilot/AutopilotSubsystem/PID_theta/Filter/Cont. Filter'
 * '<S139>' : 'autopilot/AutopilotSubsystem/PID_theta/Filter ICs/Internal IC - Filter'
 * '<S140>' : 'autopilot/AutopilotSubsystem/PID_theta/I Gain/Internal Parameters'
 * '<S141>' : 'autopilot/AutopilotSubsystem/PID_theta/Ideal P Gain/Passthrough'
 * '<S142>' : 'autopilot/AutopilotSubsystem/PID_theta/Ideal P Gain Fdbk/Disabled'
 * '<S143>' : 'autopilot/AutopilotSubsystem/PID_theta/Integrator/Continuous'
 * '<S144>' : 'autopilot/AutopilotSubsystem/PID_theta/Integrator ICs/Internal IC'
 * '<S145>' : 'autopilot/AutopilotSubsystem/PID_theta/N Copy/Disabled'
 * '<S146>' : 'autopilot/AutopilotSubsystem/PID_theta/N Gain/Internal Parameters'
 * '<S147>' : 'autopilot/AutopilotSubsystem/PID_theta/P Copy/Disabled'
 * '<S148>' : 'autopilot/AutopilotSubsystem/PID_theta/Parallel P Gain/Internal Parameters'
 * '<S149>' : 'autopilot/AutopilotSubsystem/PID_theta/Reset Signal/Disabled'
 * '<S150>' : 'autopilot/AutopilotSubsystem/PID_theta/Saturation/Passthrough'
 * '<S151>' : 'autopilot/AutopilotSubsystem/PID_theta/Saturation Fdbk/Disabled'
 * '<S152>' : 'autopilot/AutopilotSubsystem/PID_theta/Sum/Sum_PID'
 * '<S153>' : 'autopilot/AutopilotSubsystem/PID_theta/Sum Fdbk/Disabled'
 * '<S154>' : 'autopilot/AutopilotSubsystem/PID_theta/Tracking Mode/Disabled'
 * '<S155>' : 'autopilot/AutopilotSubsystem/PID_theta/Tracking Mode Sum/Passthrough'
 * '<S156>' : 'autopilot/AutopilotSubsystem/PID_theta/Tsamp - Integral/TsSignalSpecification'
 * '<S157>' : 'autopilot/AutopilotSubsystem/PID_theta/Tsamp - Ngain/Passthrough'
 * '<S158>' : 'autopilot/AutopilotSubsystem/PID_theta/postSat Signal/Forward_Path'
 * '<S159>' : 'autopilot/AutopilotSubsystem/PID_theta/preInt Signal/Internal PreInt'
 * '<S160>' : 'autopilot/AutopilotSubsystem/PID_theta/preSat Signal/Forward_Path'
 */
#endif                                 /* autopilot_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
