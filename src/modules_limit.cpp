/* Calf DSP plugin pack
 * Limiter related plugins
 *
 * Copyright (C) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen and others
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */
#include <limits.h>
#include <memory.h>
#include <calf/giface.h>
#include <calf/audio_fx.h>
#include <calf/modules_limit.h>

using namespace dsp;
using namespace calf_plugins;

#define SET_IF_CONNECTED(name) if (params[AM::param_##name] != NULL) *params[AM::param_##name] = name;

/**********************************************************************
 * LIMITER by Christian Holschuh and Markus Schmidt
**********************************************************************/

limiter_audio_module::limiter_audio_module()
{
    is_active = false;
    srate = 0;
    asc_led    = 0.f;
    attack_old = -1.f;
    limit_old = -1.f;
    asc_old = true;
}

void limiter_audio_module::activate()
{
    is_active = true;
    // set all filters and strips
    params_changed();
    limiter.activate();
}

void limiter_audio_module::deactivate()
{
    is_active = false;
    limiter.deactivate();
}
void limiter_audio_module::set_srates()
{
    resampler[0].set_params(srate, *params[param_oversampling], 2);
    resampler[1].set_params(srate, *params[param_oversampling], 2);
    limiter.set_sample_rate(srate * *params[param_oversampling]);
}
void limiter_audio_module::params_changed()
{
    limiter.set_params(*params[param_limit], *params[param_attack], *params[param_release], 1.f, *params[param_asc], pow(0.5, (*params[param_asc_coeff] - 0.5) * 2 * -1), true);
    if( *params[param_attack] != attack_old) {
        attack_old = *params[param_attack];
        limiter.reset();
    }
    if(*params[param_limit] != limit_old or *params[param_asc] != asc_old) {
        asc_old = *params[param_asc];
        limit_old = *params[param_limit];
        limiter.reset_asc();
    }
    set_srates();
}

void limiter_audio_module::set_sample_rate(uint32_t sr)
{
    srate = sr;
    int meter[] = {param_meter_inL, param_meter_inR,  param_meter_outL, param_meter_outR, param_att};
    int clip[] = {param_clip_inL, param_clip_inR, param_clip_outL, param_clip_outR, -1};
    meters.init(params, meter, clip, 5, srate);
    set_srates();
}

uint32_t limiter_audio_module::process(uint32_t offset, uint32_t numsamples, uint32_t inputs_mask, uint32_t outputs_mask)
{
    bool bypass = *params[param_bypass] > 0.5f;
    numsamples += offset;
    if(bypass) {
        // everything bypassed
        while(offset < numsamples) {
            outs[0][offset] = ins[0][offset];
            outs[1][offset] = ins[1][offset];
            float values[] = {0, 0, 0, 0, 1};
            meters.process(values);
            ++offset;
        }
        asc_led    = 0.f;
    } else {
        asc_led   -= std::min(asc_led, numsamples);

        while(offset < numsamples) {
            // cycle through samples
            float inL = ins[0][offset];
            float inR = ins[1][offset];
            // in level
            inR *= *params[param_level_in];
            inL *= *params[param_level_in];
            // out vars
            float outL = inL;
            float outR = inR;
            
            // upsampling
            double *samplesL = resampler[0].upsample((double)outL);
            double *samplesR = resampler[1].upsample((double)outR);
            
            float tmpL;
            float tmpR;
            
            // process gain reduction
            float fickdich[0];
            for (int i = 0; i < *params[param_oversampling]; i ++) {
                tmpL = samplesL[i];
                tmpR = samplesR[i];
                limiter.process(tmpL, tmpR, fickdich);
                if(limiter.get_asc())
                    asc_led = srate >> 3;
            }
            
            // downsampling
            outL = resampler[0].downsample(samplesL);
            outR = resampler[1].downsample(samplesR);
            
            // should never be used. but hackers are paranoid by default.
            // so we make shure NOTHING is above limit
            outL = std::min(std::max(outL, -*params[param_limit]), *params[param_limit]);
            outR = std::min(std::max(outR, -*params[param_limit]), *params[param_limit]);

            // autolevel
            outL /= *params[param_limit];
            outR /= *params[param_limit];

            // out level
            outL *= *params[param_level_out];
            outR *= *params[param_level_out];

            // send to output
            outs[0][offset] = outL;
            outs[1][offset] = outR;

            float values[] = {inL, inR, outL, outR, bypass > 0.5 ? (float)1.0 : (float)limiter.get_attenuation()};
            meters.process (values);

            // next sample
            ++offset;
        } // cycle trough samples
    } // process (no bypass)
    meters.fall(numsamples);
    if (params[param_asc_led] != NULL) *params[param_asc_led] = asc_led;
    return outputs_mask;
}

/**********************************************************************
 * MULTIBAND LIMITER by Markus Schmidt and Christian Holschuh 
**********************************************************************/

multibandlimiter_audio_module::multibandlimiter_audio_module()
{
    is_active = false;
    srate = 0;
    asc_led    = 0.f;
    attack_old = -1.f;
    channels = 2;
    buffer_size = 0;
    overall_buffer_size = 0;
    _sanitize = false;
    _mode = 0;
    over = 0;
    for(int i = 0; i < strips; i ++) {
        weight_old[i] = -1.f;
    }
    attack_old = -1.f;
    limit_old = -1.f;
    asc_old = true;
    crossover.init(2, 4, 44100);
}
multibandlimiter_audio_module::~multibandlimiter_audio_module()
{
    free(buffer);
}
void multibandlimiter_audio_module::activate()
{
    is_active = true;
    // set all filters and strips
    params_changed();
    // activate all strips
    for (int j = 0; j < strips; j ++) {
        strip[j].activate();
        strip[j].set_multi(true);
        strip[j].id = j;
    }
    broadband.activate();
    pos = 0;
}

void multibandlimiter_audio_module::deactivate()
{
    is_active = false;
    // deactivate all strips
    for (int j = 0; j < strips; j ++) {
        strip[j].deactivate();
    }
    broadband.deactivate();
}
void multibandlimiter_audio_module::set_srates()
{
    crossover.set_sample_rate(srate);
    for (int j = 0; j < strips; j ++) {
        strip[j].set_sample_rate(srate * *params[param_oversampling]);
    }
    broadband.set_sample_rate(srate * *params[param_oversampling]);
    for (int i = 0; i < strips; i++) {
        resampler[i][0].set_params(srate, *params[param_oversampling], 2);
        resampler[i][1].set_params(srate, *params[param_oversampling], 2);
    }
}
void multibandlimiter_audio_module::params_changed()
{
    // determine mute/solo states
    solo[0] = *params[param_solo0] > 0.f ? true : false;
    solo[1] = *params[param_solo1] > 0.f ? true : false;
    solo[2] = *params[param_solo2] > 0.f ? true : false;
    solo[3] = *params[param_solo3] > 0.f ? true : false;
    no_solo = (*params[param_solo0] > 0.f ||
            *params[param_solo1] > 0.f ||
            *params[param_solo2] > 0.f ||
            *params[param_solo3] > 0.f) ? false : true;

    int m = *params[param_mode];
    if (m != _mode) {
        _mode = *params[param_mode];
    }
    
    crossover.set_mode(_mode + 1);
    crossover.set_filter(0, *params[param_freq0]);
    crossover.set_filter(1, *params[param_freq1]);
    crossover.set_filter(2, *params[param_freq2]);
    
    // set the params of all strips
    float rel;

    rel = *params[param_release] *  pow(0.25, *params[param_release0] * -1);
    rel = (*params[param_minrel] > 0.5) ? std::max(2500 * (1.f / 30), rel) : rel;
    weight[0] = pow(0.25, *params[param_weight0] * -1);
    strip[0].set_params(*params[param_limit], *params[param_attack], rel, weight[0], *params[param_asc], pow(0.5, (*params[param_asc_coeff] - 0.5) * 2 * -1));
    *params[param_effrelease0] = rel;
    rel = *params[param_release] *  pow(0.25, *params[param_release1] * -1);
    rel = (*params[param_minrel] > 0.5) ? std::max(2500 * (1.f / *params[param_freq0]), rel) : rel;
    weight[1] = pow(0.25, *params[param_weight1] * -1);
    strip[1].set_params(*params[param_limit], *params[param_attack], rel, weight[1], *params[param_asc], pow(0.5, (*params[param_asc_coeff] - 0.5) * 2 * -1), true);
    *params[param_effrelease1] = rel;
    rel = *params[param_release] *  pow(0.25, *params[param_release2] * -1);
    rel = (*params[param_minrel] > 0.5) ? std::max(2500 * (1.f / *params[param_freq1]), rel) : rel;
    weight[2] = pow(0.25, *params[param_weight2] * -1);
    strip[2].set_params(*params[param_limit], *params[param_attack], rel, weight[2], *params[param_asc], pow(0.5, (*params[param_asc_coeff] - 0.5) * 2 * -1));
    *params[param_effrelease2] = rel;
    rel = *params[param_release] *  pow(0.25, *params[param_release3] * -1);
    rel = (*params[param_minrel] > 0.5) ? std::max(2500 * (1.f / *params[param_freq2]), rel) : rel;
    weight[3] = pow(0.25, *params[param_weight3] * -1);
    strip[3].set_params(*params[param_limit], *params[param_attack], rel, weight[3], *params[param_asc], pow(0.5, (*params[param_asc_coeff] - 0.5) * 2 * -1));
    *params[param_effrelease3] = rel;
    broadband.set_params(*params[param_limit], *params[param_attack], rel, 1.f, *params[param_asc], pow(0.5, (*params[param_asc_coeff] - 0.5) * 2 * -1));
    // rebuild multiband buffer
    if( *params[param_attack] != attack_old) {
        int bs = (int)(srate * (*params[param_attack] / 1000.f) * channels);
        buffer_size = bs - bs % channels; // buffer size attack rate
        attack_old = *params[param_attack];
        _sanitize = true;
        pos = 0;
        for (int j = 0; j < strips; j ++) {
            strip[j].reset();
        }
        broadband.reset();
    }
    if(*params[param_limit] != limit_old or *params[param_asc] != asc_old or *params[param_weight0] != weight_old[0] or *params[param_weight1] != weight_old[1] or *params[param_weight2] != weight_old[2] or *params[param_weight3] != weight_old[3] ) {
        asc_old = *params[param_asc];
        limit_old = *params[param_limit];
        weight_old[0] = *params[param_weight0];
        weight_old[1] = *params[param_weight1];
        weight_old[2] = *params[param_weight2];
        weight_old[3] = *params[param_weight3];
        for (int j = 0; j < strips; j ++) {
            strip[j].reset_asc();
        }
        broadband.reset_asc();
    }
    if (over != *params[param_oversampling]) {
        over = *params[param_oversampling];
        set_srates();
    }
}

void multibandlimiter_audio_module::set_sample_rate(uint32_t sr)
{
    srate = sr;
    // rebuild buffer
    overall_buffer_size = (int)(srate * (100.f / 1000.f) * channels) + channels; // buffer size max attack rate
    buffer = (float*) calloc(overall_buffer_size, sizeof(float));
    pos = 0;
    set_srates();
    int meter[] = {param_meter_inL, param_meter_inR,  param_meter_outL, param_meter_outR, param_att0, param_att1, param_att2, param_att3};
    int clip[] = {param_clip_inL, param_clip_inR, param_clip_outL, param_clip_outR, -1, -1, -1, -1};
    meters.init(params, meter, clip, 8, srate);
}

#define BYPASSED_COMPRESSION(index) \
    if(params[param_att##index] != NULL) \
        *params[param_att##index] = 1.0; \

#define ACTIVE_COMPRESSION(index) \
    if(params[param_att##index] != NULL) \
        *params[param_att##index] = strip[index].get_attenuation(); \


uint32_t multibandlimiter_audio_module::process(uint32_t offset, uint32_t numsamples, uint32_t inputs_mask, uint32_t outputs_mask)
{
    bool bypass = *params[param_bypass] > 0.5f;
    numsamples += offset;
    float batt = 0.f;
    if(bypass) {
        // everything bypassed
        while(offset < numsamples) {
            outs[0][offset] = ins[0][offset];
            outs[1][offset] = ins[1][offset];
            float values[] = {0, 0, 0, 0, 1, 1, 1, 1};
            meters.process(values);
            ++offset;
        }
        asc_led    = 0.f;
    } else {
        // process all strips
        asc_led     -= std::min(asc_led, numsamples);
        float _tmpL[channels];
        float _tmpR[channels];
        while(offset < numsamples) {
            float inL  = 0.f; // input
            float inR  = 0.f;
            float outL = 0.f; // final output
            float outR = 0.f;
            float tmpL = 0.f; // used for temporary purposes
            float tmpR = 0.f;
            float sumL = 0.f; // 
            float sumR = 0.f;
            double overL[strips][16];
            double overR[strips][16];
            float _null[0];
            
            bool asc_active = false;
            
            // cycle through samples
            if(!_sanitize) {
                inL = ins[0][offset];
                inR = ins[1][offset];
            }
            // in level
            inR *= *params[param_level_in];
            inL *= *params[param_level_in];
            
            // process crossover
            float xin[] = {inL, inR};
            crossover.process(xin);
            
            // cycle over bands
            for (int i = 0; i < strips; i++) {
                
                // fetch values from crossover
                tmpL = crossover.get_value(0, i);
                tmpR = crossover.get_value(1, i);
                
                // upsample
                double *samplesL = resampler[0].upsample((double)tmpL);
                double *samplesR = resampler[1].upsample((double)tmpR);
                
                // copy to cache
                memcpy(overL[i], samplesL, sizeof(double) * *params[param_oversampling]);
                memcpy(overR[i], samplesR, sizeof(double) * *params[param_oversampling]);
            }
            
            // cycle over upsampled samples
            for (int o = 0; o < *params[param_oversampling]; o ++) {
                
                sumL = 0.f;
                sumR = 0.f;
                
                // cycle over bands
                for (int i = 0; i < strips; i++) {
                    
                    // sum up for multiband coefficient
                    sumL += ((fabs(overL[i]) > *params[param_limit]) ? *params[param_limit] * (fabs(overL[i]) / overL[i]) : overL[i]) * weight[i];
                    sumR += ((fabs(overR[i]) > *params[param_limit]) ? *params[param_limit] * (fabs(overR[i]) / overR[i]) : overR[i]) * weight[i];
                    
                }
            }
            
            
                sumL = 0.f;
                sumR = 0.f;
                
                // cycle over upsampled samples
                for (int o = 0; o < *params[param_oversampling]; o ++) {
                    
                    
                    
                    
                    
                    
                    tmpL = samplesL[o];
                    tmpR = samplesR[o];
                    
                    // sum up for multiband coefficient
                    sumL += ((fabs(tmpL) > *params[param_limit]) ? *params[param_limit] * (fabs(tmpL) / tmpL) : tmpL) * weight[i];
                    sumR += ((fabs(tmpR) > *params[param_limit]) ? *params[param_limit] * (fabs(tmpR) / tmpR) : tmpR) * weight[i];
                    
                    // strip limiter
                    strip[i].process(tmpL, tmpR, _null);
                    
                    // strip asc
                    asc_active = asc_active || strip[i].get_asc();
                    
                    // add to output
                    overL[o] += tmpL;
                    overR[o] += tmpR;
                }
            }
            
            // cycle over upsampled samples
            for (int o = 0; o < *params[param_oversampling]; o ++) {
                
                // broadband limiter
                broadband.process(overL[o], overR[o], _null);
                
                // broadband asc
                asc_active = asc_active || broadband.get_asc();
            }
            
            // downsample
            outL = resampler[0][0].downsample(overL);
            outR = resampler[0][1].downsample(overR);
            
            // should never be used. but hackers are paranoid by default.
            // so we make shure NOTHING is above limit
            outL = std::min(std::max(outL, -*params[param_limit]), *params[param_limit]);
            outR = std::min(std::max(outR, -*params[param_limit]), *params[param_limit]);
                
            if(asc_active)  {
                asc_led = srate >> 3;
            }
                
            for (int i = 0; i < strips; i++) {
                left  = crossover.get_value(0, i);
                right = crossover.get_value(1, i);
                
                // out vars
                outL = 0.f;
                outR = 0.f;
                float left;
                float right;
                float sum_left = 0.f;
                float sum_right = 0.f;
                bool asc_active = false;
            
            for (int i = 0; i < strips; i++) {
                // upsampling
                samplesL = resampler[i][0].upsample((double)_tmpL[i]);
                samplesR = resampler[i][1].upsample((double)_tmpR[i]);
                for (int o = 0; o < *params[param_oversampling]; o ++) {
                    if (!i and !o) {
                        // zero oversample buffer in first loop
                        overbuf[0][o] = 0.f;
                        overbuf[1][o] = 0.f;
                    }
                    // process gain reduction
                    tmpL = samplesL[o];
                    tmpR = samplesR[o];
                    strip[i].process(tmpL, tmpR, buffer);
                    // sum up output of limiters
                    if (solo[i] || no_solo) {
                        overbuf[0][o] += tmpL;
                        overbuf[1][o] += tmpR;
                    }
                    asc_active = asc_active || strip[i].get_asc();
                } // process single strip again for limiter
            }
            for (int o = 0; o < *params[param_oversampling]; o ++) {
                float fickdich[0];
                broadband.process(overbuf[0][o], overbuf[1][o], fickdich);
                asc_active = asc_active || broadband.get_asc();
            }
            
            // downsampling
            outL = resampler[0][0].downsample((double*)&overbuf[0][0]);
            outR = resampler[0][1].downsample((double*)&overbuf[1][0]);
            
            
            
            
            
            
            
            
            
                for (int i = 0; i < strips; i++) {
                    left  = crossover.get_value(0, i);
                    right = crossover.get_value(1, i);
                    
                    // remember filtered values for limiting
                    // (we need multiband_coeff before we can call the limiter bands)
                    _tmpL[i] = left;
                    _tmpR[i] = right;
    
                    // sum up for multiband coefficient
                    sum_left += ((fabs(left) > *params[param_limit]) ? *params[param_limit] * (fabs(left) / left) : left) * weight[i];
                    sum_right += ((fabs(right) > *params[param_limit]) ? *params[param_limit] * (fabs(right) / right) : right) * weight[i];
                } // process single strip with filter
    
                // write multiband coefficient to buffer
                buffer[pos] = std::min(*params[param_limit] / std::max(fabs(sum_left), fabs(sum_right)), 1.0);
    
                for (int i = 0; i < strips; i++) {
                    // process gain reduction
                    strip[i].process(_tmpL[i], _tmpR[i], buffer);
                    // sum up output of limiters
                    if (solo[i] || no_solo) {
                        outL += _tmpL[i];
                        outR += _tmpR[i];
                    }
                    asc_active = asc_active || strip[i].get_asc();
                } // process single strip again for limiter
                float fickdich[0];
                broadband.process(outL, outR, fickdich);
                asc_active = asc_active || broadband.get_asc();
                
                // should never be used. but hackers are paranoid by default.
                // so we make shure NOTHING is above limit
                outL = std::max(outL, -*params[param_limit]);
                outL = std::min(outL, *params[param_limit]);
                outR = std::max(outR, -*params[param_limit]);
                outR = std::min(outR, *params[param_limit]);
                
                if(asc_active)  {
                    asc_led = srate >> 3;
                }
            
                samplesL[o] = outL;
                samplesR[o] = outR;
            }
            
<<<<<<< HEAD
=======
            // downsampling
            outL = resampler[0].downsample(samplesL);
            outR = resampler[1].downsample(samplesR);

>>>>>>> master
            // autolevel
            outL /= *params[param_limit];
            outR /= *params[param_limit];

            // out level
            outL *= *params[param_level_out];
            outR *= *params[param_level_out];

            // send to output
            outs[0][offset] = outL;
            outs[1][offset] = outR;

            // next sample
            ++offset;
            pos = (pos + channels * 16) % buffer_size;
            if(pos == 0) _sanitize = false;
            
            batt = broadband.get_attenuation();
            
            float values[] = {inL, inR, outL, outR,
                bypass > 0.5 ? (float)1.0 : (float)strip[0].get_attenuation() * batt,
                bypass > 0.5 ? (float)1.0 : (float)strip[1].get_attenuation() * batt,
                bypass > 0.5 ? (float)1.0 : (float)strip[2].get_attenuation() * batt,
                bypass > 0.5 ? (float)1.0 : (float)strip[3].get_attenuation() * batt};
            meters.process(values);
        } // cycle trough samples
    } // process all strips (no bypass)
    if (params[param_asc_led] != NULL) *params[param_asc_led] = asc_led;
    meters.fall(numsamples);
    return outputs_mask;
}

bool multibandlimiter_audio_module::get_graph(int index, int subindex, int phase, float *data, int points, cairo_iface *context, int *mode) const
{
    return crossover.get_graph(subindex, phase, data, points, context, mode);
}
bool multibandlimiter_audio_module::get_layers(int index, int generation, unsigned int &layers) const
{
    return crossover.get_layers(index, generation, layers);
}
