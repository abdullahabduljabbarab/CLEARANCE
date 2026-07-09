/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 *
 * File: radar.h
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

#ifndef radar_h_
#define radar_h_
#ifndef radar_COMMON_INCLUDES_
#define radar_COMMON_INCLUDES_
#include "rtwtypes.h"
#include "rt_nonfinite.h"
#include "math.h"
#endif                                 /* radar_COMMON_INCLUDES_ */

#include "radar_types.h"
#include "rtGetNaN.h"
#include "rt_defines.h"

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

/* Block signals (default storage) */
typedef struct {
  creal_T dcv[320000];
  creal_T a__2[40000];
  creal_T rx_bf[40000];                /* '<Root>/BeamformStage' */
  creal_T range_compressed[40000];
  creal_T range_compressed_m[40000];
  creal_T x[40000];
  creal_T dcv1[40000];
  real_T RDM_power[40000];             /* '<Root>/RangeDopplerStage' */
  creal_T y_full[2524];
  boolean_T det_mask[40000];
  real_T range_axis_m[2500];           /* '<Root>/RangeDopplerStage' */
} B_radar_T;

/* External inputs (root inport signals with default storage) */
typedef struct {
  creal_T rx_cube[320000];             /* '<Root>/rx_cube' */
  real_T look_angle_rad;               /* '<Root>/look_angle_rad' */
  creal_T tx[25];                      /* '<Root>/tx' */
} ExtU_radar_T;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  real_T det_range_m[16];              /* '<Root>/det_range_m' */
  real_T det_velocity_mps[16];         /* '<Root>/det_velocity_mps' */
  real_T det_snr_dB[16];               /* '<Root>/det_snr_dB' */
  int32_T n_detections;                /* '<Root>/n_detections' */
} ExtY_radar_T;

/* Real-time Model Data Structure */
struct tag_RTM_radar_T {
  const char_T * volatile errorStatus;
  B_radar_T *blockIO;
  ExtU_radar_T *inputs;
  ExtY_radar_T *outputs;
};

/* Model entry point functions */
extern void radar_initialize(RT_MODEL_radar_T *const radar_M);
extern void radar_step(RT_MODEL_radar_T *const radar_M);
extern void radar_terminate(RT_MODEL_radar_T *const radar_M);

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
 * '<Root>' : 'radar'
 * '<S1>'   : 'radar/BeamformStage'
 * '<S2>'   : 'radar/CFARStage'
 * '<S3>'   : 'radar/RangeDopplerStage'
 */
#endif                                 /* radar_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
