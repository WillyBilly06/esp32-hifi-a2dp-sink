#ifndef LHDC_SNS_SYNTH_H
#define LHDC_SNS_SYNTH_H

#include "lhdc_dec_internal.h"

void lhdc_sns_synth_apply(float *spectrum,
                           const lhdc_dec_sns_params_t *params,
                           int mdct_size,
                           const uint16_t *band_off,
                           const uint16_t *band_scale,
                           int num_sfb);

void lhdc_dequant_sns_apply(const int32_t *qs, int rn, float *spectrum, int n, float step,
                            const lhdc_dec_sns_params_t *params, const uint16_t *band_off, int num_sfb);

lhdc_dec_ret_t lhdc_sns_decode_params(lhdc_dec_bit_reader_t *br,
                                        lhdc_dec_sns_params_t *params,
                                        int num_sfb);

#endif
