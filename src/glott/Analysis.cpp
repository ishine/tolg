// Copyright 2016-2018 Lauri Juvela and Manu Airaksinen
// LF modelling extraction code Copyright: Phonetics and Speech Laboratory, Trinity College Dublin
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//  <><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
//               GlottDNN Speech Parameter Extractor including LF modelling Rd extraction
//  <><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><><>
//
//  This program reads a speech file and extracts speech
//  parameters using glottal inverse filtering.
//
//  This program has been written in Aalto University,
//  Department of Signal Processing and Acoustics, Espoo, Finland
//
//  This program uses some code from the original GlottHMM vocoder program
//  written by Tuomo Raitio, now re-factored and re-written in C++
//
//  Authors: Lauri Juvela, Manu Airaksinen
//  Acknowledgements: Tuomo Raitio, Paavo Alku
//  File Analysis.cpp
//  Version: 1.0

// This program referred the Matlab code from the original @ Voice_Analysis_Toolkit https://github.com/jckane/Voice_Analysis_Toolkit
// written by John Kane (Phonetics and Speech Laboratory, Trinity College Dublin) in Matlab, now re-factored and re-written in C++
// Author: Xiao Zhang (Phonetics and Speech Laboratory, Trinity College Dublin)  zhangx16@tcd.ie



/***********************************************/
/*                 INCLUDE                     */
/***********************************************/

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cstring>

#include <iostream>
#include <iomanip>

#include <vector>
#include <gslwrap/vector_double.h>

#include "definitions.h"
#include "Filters.h"
#include "FileIo.h"
#include "ReadConfig.h"
#include "SpFunctions.h"
#include "AnalysisFunctions.h"

#include "Utils.h"


#include <gslwrap/random_generator.h>
#include <gslwrap/random_number_distribution.h>
#include <gsl/gsl_statistics_double.h>

#include "mex.h"
#include "math.h"
#include <algorithm>
#include <gsl/gsl_sort_vector.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_vector_int.h>
#include <gsl/gsl_filter.h>



const double pi = 3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679;

// Smooth function
gsl::vector smooth(const gsl::vector& input, int windowSize)
{
    gsl::vector output(input.size());
    int halfWindowSize = windowSize / 2;

    for (int i = 0; i < input.size(); i++)
    {
        double sum = 0.0;
        int count = 0;

        for (int j = i - halfWindowSize; j <= i + halfWindowSize; j++)
        {
            if (j >= 0 && j < input.size())
            {
                sum += input[j];
                count++;
            }
        }

        output[i] = sum / count;
    }

    return output;
}

// Medfilt1 function
gsl::vector medfilt1(const gsl::vector& input, int windowSize)
{
    gsl::vector output(input.size());
    int halfWindowSize = windowSize / 2;

    for (int i = 0; i < input.size(); i++)
    {
        std::vector<double> window;

        for (int j = i - halfWindowSize; j <= i + halfWindowSize; j++)
        {
            if (j >= 0 && j < input.size())
                window.push_back(input[j]);
        }

        std::sort(window.begin(), window.end());
        output[i] = window[windowSize / 2];
    }

    return output;
}

/* summation function */
double sum(double a[], int size)
{
    double summed=0;
    int i;
    for (i=0;i<size;i++)
    {
        summed=summed+a[i];
    }
    return summed;
}

void lfSource(double &alpha, double &epsi, double Tc, double fs, double Tp, double Te, double Ta, double EE) {

    // Initialize
    double TolFun = 0.0000001;
    int MaxIter = 100;
    int count = 1;
    double Tb = Tc - Te;
    double omega_g = M_PI / Tp;
    double eps0, change = 1.0, f_eps, f_eps_prime;

    // Solve epsilon using Newton-Raphson method
    eps0 = 1 / Ta;
    while (count <= MaxIter && std::fabs(change) > TolFun) {
        f_eps = (eps0 * Ta - 1.0 + std::exp(-eps0 * Tb));
        f_eps_prime = (Ta - Tb * std::exp(-eps0 * Tb));
        change = f_eps / f_eps_prime;
        eps0 = eps0 - change;
        eps0 = eps0 - (eps0 * Ta - 1 + std::exp(-eps0 * Tb)) / (Ta - Tb * std::exp(-eps0 * Tb));
        count++;
    }
    epsi = eps0;

    // Solve alpha - Do Newton-Raphson iterations
    double a0 = 0.0;
    double change_alpha = 1.0;
    double E0 = -EE / (std::exp(a0 * Te) * std::sin(omega_g * Te));
    double A2, f_a, f_a_prime, e0, part1, part2, part3, partAtan, part4, A1;

    A2 = (-EE / ((epsi * epsi) * Ta)) * (1 - std::exp(-epsi * Tb) * (1 + epsi * Tb));

    int count_alpha = 1; // Add declaration and initialization of count_alpha here

    while (count_alpha <= MaxIter && std::fabs(change_alpha) > TolFun) {
        part1 = std::sqrt((a0 * a0) + (omega_g * omega_g));
        partAtan = 2 * std::atan((std::sqrt((a0 * a0) + (omega_g * omega_g)) - a0) / omega_g);
        part2 = std::sin(omega_g * Te - partAtan);
        part3 = omega_g * std::exp(-a0 * Te) - ((A2 / EE) * ((a0 * a0) + (omega_g * omega_g)) * std::sin(omega_g * Te));
        part4 = (std::sin(omega_g * Te) * (1 - 2 * a0 * A2 / EE) - omega_g * Te * std::exp(-a0 * Te));
        a0 = a0 - ((part1 * part2) + part3) / part4;

        part1 = std::sqrt((a0 * a0) + (omega_g * omega_g));
        partAtan = 2 * std::atan((std::sqrt((a0 * a0) + (omega_g * omega_g)) - a0) / omega_g);
        part2 = std::sin(omega_g * Te - partAtan);
        part3 = omega_g * std::exp(-a0 * Te) - ((A2 / EE) * ((a0 * a0) + (omega_g * omega_g)) * std::sin(omega_g * Te));
        part4 = (std::sin(omega_g * Te) * (1 - 2 * a0 * A2 / EE) - omega_g * Te * std::exp(-a0 * Te));

        a0 = a0 - ((part1 * part2) + part3) / part4;

        count_alpha++;
    }

    alpha = a0;

}


/*******************************************************************/
/*                          MAIN                                   */
/*******************************************************************/
void Rd2R(double Rd, double EE, double F0, double& Ra, double& Rk, double& Rg) {
    Ra = (-1 + (4.8 * Rd)) / 100;
    Rk = (22.4 + (11.8 * Rd)) / 100;
    double EI = (M_PI * Rk * EE) / 2;
    double UP = (Rd * EE) / (10 * F0);
    Rg = EI / (F0 * UP * M_PI);
}

void lf_cont(double F0, double fs, double Ra, double Rk, double Rg, double EE, gsl::vector& g_LF) {
    const double F0min = 20.0;
    const double F0max = 500.0;

    // Set LF model parameters
    double T0 = 1.0 / F0;
    double Ta = Ra * T0;
    double Te = ((1.0 + Rk) / (2.0 * Rg)) * T0;
    double Tp = Te / (Rk + 1.0);
    double Tb = ((1.0 - (Rk + 1.0) / (2.0 * Rg)) * 1.0 / F0);
    double Tc = Tb + Te;

//    if (F0 < F0min || F0 > F0max) {
//        // Handle invalid F0 value
//        // For example, you can clear the input vector:
//        g_LF.resize(0);
//    } else {
        // Solve area balance using Newton-Raphson method
    double alpha, epsi;

    lfSource(alpha, epsi, Tc, fs, Tp, Te, Ta, EE);

    double omega = M_PI / Tp;
    double E0 = -(std::abs(EE)) / (std::exp(alpha * Te) * std::sin(omega * Te));

    // Generate open phase and closed phase and combine
    double dt = 1.0 / fs;

    size_t T1_size = static_cast<size_t>(std::round(Te / dt));
    size_t T2_size = static_cast<size_t>(std::round((Tc - Te) / dt));

    // Ensure T1_size and T2_size are positive
    T1_size = std::max(T1_size, static_cast<size_t>(1));
    T2_size = std::max(T2_size, static_cast<size_t>(1));

    g_LF.resize(T1_size + T2_size);

    for (size_t i = 0; i < T1_size; i++) {
        double t = dt * i;
        g_LF[i] = E0 * std::exp(alpha * t) * std::sin(omega * t);
    }

    for (size_t i = 0; i < T2_size; i++) {
        double t = (T1_size * dt) + dt * i;
        g_LF[T1_size + i] = (-EE / (epsi * Ta)) * (std::exp(-epsi * (t - Te)) - std::exp(-epsi * Tb));
    }
//    }
}






//std::cout << "alpha " << alpha  << std::endl;

gsl::vector makePulseCentGCI(const gsl::vector pulse, int winLen, int start, int finish) {
    size_t pulseLen = pulse.size();

    // Find the index of the minimum value in pulse
    double minVal = pulse(0);
    size_t idx = 0;

    for (size_t i = 1; i < pulse.size(); ++i) {
        if (pulse(i) < minVal) {
            minVal = pulse(i);
            idx = i;
        }
    }

    size_t group_idx = idx + pulseLen;

    size_t pulseGroupLen = pulseLen * 3;



    gsl::vector pulseGroup(pulse.size() * 3);  // Create a vector to store pulseGroup

    // Repeat pulse three times
    for (size_t i = 0; i < pulseGroupLen; i += pulseLen) {
        for (size_t j = 0; j < pulseLen; ++j) {
            pulseGroup[i + j] = pulse[j];
        }
    }

    if (start == -1 && finish == -1) {
        if (winLen % 2 != 0) {
            start = group_idx - std::ceil(winLen / 2.0);
        } else {
            start = group_idx - winLen / 2;
        }
        finish = group_idx + std::floor(winLen / 2.0);
    } else {
        start = group_idx - start;
        finish = group_idx + finish;
    }

    if (finish > pulseGroupLen || start < 0) {
        return gsl::vector(); // Return empty vector if start or finish indices are out of range
    } else {
        gsl::vector LFgroup = pulseGroup.subvector(start, finish - start + 1); // Extract the desired segment of pulseGroup
        return LFgroup;
    }
}



// todo ??? Is ths Correspond?

double computeCorrelation(const gsl::vector X, const gsl::vector Y)
{
    double sum_X = 0.0, sum_Y = 0.0, sum_XY = 0.0;
    double squareSum_X = 0.0, squareSum_Y = 0.0;
    int n = X.size();

    for (int i = 0; i < n; i++)
    {
        // Sum of elements of vector X.
        sum_X += X[i];

        // Sum of elements of vector Y.
        sum_Y += Y[i];

        // Sum of X[i] * Y[i].
        sum_XY += X[i] * Y[i];

        // Sum of squares of vector elements.
        squareSum_X += X[i] * X[i];
        squareSum_Y += Y[i] * Y[i];
    }

    // Use the formula for calculating the correlation coefficient.
    double corr = (n * sum_XY - sum_X * sum_Y)
                  / sqrt((n * squareSum_X - sum_X * sum_X)
                         * (n * squareSum_Y - sum_Y * sum_Y));

    return corr;
}



gsl::matrix computeCorrelationMatrix(const gsl::vector& X, const gsl::vector& Y)
{
    int n = X.size();
    gsl::matrix corrMatrix(1, n); // Create a matrix to store correlation scores

    double sum_X = 0.0, sum_Y = 0.0, sum_XY = 0.0;
    double squareSum_X = 0.0, squareSum_Y = 0.0;

    for (int i = 0; i < n; i++)
    {
        // Sum of elements of vector X.
        sum_X += X[i];

        // Sum of elements of vector Y.
        sum_Y += Y[i];

        // Sum of X[i] * Y[i].
        sum_XY += X[i] * Y[i];

        // Sum of squares of vector elements.
        squareSum_X += X[i] * X[i];
        squareSum_Y += Y[i] * Y[i];
    }

    // Compute the correlation coefficient for each element
    for (int i = 0; i < n; i++)
    {
        double corr = (n * X[i] * Y[i] - sum_X * sum_Y)
                      / sqrt((n * X[i] * X[i] - sum_X * sum_X)
                             * (n * Y[i] * Y[i] - sum_Y * sum_Y));

        corrMatrix(0, i) = corr;
    }

    return corrMatrix;
}



std::vector<double> medfilt1(const std::vector<double>& input, int windowSize) {
    std::vector<double> output(input.size());
    int halfWindowSize = windowSize / 2;

    for (int i = 0; i < input.size(); i++) {
        std::vector<double> window;

        for (int j = std::max(0, i - halfWindowSize); j <= std::min(i + halfWindowSize, static_cast<int>(input.size()) - 1); j++) {
            window.push_back(input[j]);
        }

        std::sort(window.begin(), window.end());

        if (window.size() % 2 == 0) {
            output[i] = (window[window.size() / 2 - 1] + window[window.size() / 2]) / 2.0;
        } else {
            output[i] = window[window.size() / 2];
        }
    }

    return output;
}

std::vector<double> smooth(const std::vector<double>& input, int windowSize) {
    std::vector<double> output(input.size());
    int halfWindowSize = windowSize / 2;

    for (int i = 0; i < input.size(); i++) {
        double sum = 0.0;
        int count = 0;

        for (int j = std::max(0, i - halfWindowSize); j <= std::min(i + halfWindowSize, static_cast<int>(input.size()) - 1); j++) {
            sum += input[j];
            count++;
        }

        output[i] = sum / static_cast<double>(count);
    }

    return output;
}



int main(int argc, char *argv[]) {

   if (CheckCommandLineAnalysis(argc) == EXIT_FAILURE) {
      return EXIT_FAILURE;
   }

   const char *wav_filename = argv[1];
   const char *default_config_filename = argv[2];
   const char *user_config_filename = argv[3];

   /* Declare configuration parameter struct */
   Param params;

   /* Read configuration file */
   if (ReadConfig(default_config_filename, true, &params) == EXIT_FAILURE)
      return EXIT_FAILURE;
   if (argc > 3) {
      if (ReadConfig(user_config_filename, false, &params) == EXIT_FAILURE)
         return EXIT_FAILURE;
   }

   /* Read sound file and allocate data */
   AnalysisData data;

   if(ReadWavFile(wav_filename, &(data.signal), &params) == EXIT_FAILURE)
      return EXIT_FAILURE;

   data.AllocateData(params);

   /* High-pass filter signal to eliminate low frequency "rumble" */
   HighPassFiltering(params, &(data.signal));

   if(!params.use_external_f0 || !params.use_external_gci || (params.signal_polarity == POLARITY_DETECT))
      GetIaifResidual(params, data.signal, (&data.source_signal_iaif));

   /* Read or estimate signal polarity */
   PolarityDetection(params, &(data.signal), &(data.source_signal_iaif));

   /* Read or estimate fundamental frequency (F0)  */
   if(GetF0(params, data.signal, data.source_signal_iaif, &(data.fundf)) == EXIT_FAILURE)
      return EXIT_FAILURE;

   /* Read or estimate glottal closure instants (GCIs)*/
   if(GetGci(params, data.signal, data.source_signal_iaif, data.fundf, &(data.gci_inds)) == EXIT_FAILURE)
      return EXIT_FAILURE;

   /* Estimate frame log-energy (Gain) */
   GetGain(params, data.fundf, data.signal, &(data.frame_energy));

   /* Spectral analysis for vocal tract transfer function*/
   if(params.qmf_subband_analysis) {
      SpectralAnalysisQmf(params, data, &(data.poly_vocal_tract));
   } else {
      SpectralAnalysis(params, data, &(data.poly_vocal_tract));
   }

   /* Smooth vocal tract estimates in LSF domain */
   Poly2Lsf(data.poly_vocal_tract, &data.lsf_vocal_tract);
   MedianFilter(5, &data.lsf_vocal_tract);
   MovingAverageFilter(3, &data.lsf_vocal_tract);
   Lsf2Poly(data.lsf_vocal_tract, &data.poly_vocal_tract);

   /* Perform glottal inverse filtering with the estimated VT AR polynomials */
   InverseFilter(params, data, &(data.poly_glot), &(data.source_signal));

   /* Re-estimate GCIs on the residual */
   //if(GetGci(params, data.signal, data.source_signal, data.fundf, &(data.gci_inds)) == EXIT_FAILURE)
   //   return EXIT_FAILURE;

   /* Extract pitch synchronous (excitation) waveforms at each frame */
   if (params.use_waveforms_directly) {
      GetPulses(params, data.signal, data.gci_inds, data.fundf, &(data.excitation_pulses));
   } else {
      GetPulses(params, data.source_signal, data.gci_inds, data.fundf, &(data.excitation_pulses));
   }

   HnrAnalysis(params, data.source_signal, data.fundf, &(data.hnr_glot));

   /* Convert vocal tract AR polynomials to LSF */
   Poly2Lsf(data.poly_vocal_tract, &(data.lsf_vocal_tract));

   /* Convert glottal source AR polynomials to LSF */
   Poly2Lsf(data.poly_glot, &(data.lsf_glot));

   /* Write analyzed features to files */
   data.SaveData(params);

    // start to do the Rd param extraction
    // declare the struct variable
    LfData lf_data;
/******************************** Initial settings *********************************************************************/

    // Dynamic programming weights
    double time_wgt = 0.1;
    double freq_wgt = 0.3;
    double trans_wgt = 0.3;

    // EE=zeros(1,length(GCI));
    lf_data.EE.resize(data.gci_inds.size());
    lf_data.EE.set_zero();


    // Rd_set=[0.3:0.17:2];
    //    double start = 0.3;
    //    double step = 0.17;
    //    double end = 2.0;
    int size = static_cast<int>((2.0 - 0.3) / 0.17) + 2;



    lf_data.Rd_set.resize(size);
    for (int i = 0; i < size; i++) {
        double value = 0.3 + i * 0.17;
        lf_data.Rd_set[i] = value;
    }



    // pulseNum=2;
    double pulseNum = 2;

    // Dynamic programming settings
    // nframe=length(GCI);
    double nframe = data.gci_inds.size();


    // ncands = 5; Number of candidate LF model configurations to consider
    double ncands = 5;

    // Rd_n=zeros(nframe,ncands);
    lf_data.Rd_n = gsl::matrix(nframe, ncands);
    // cost=zeros(nframe,ncands);      % cumulative cost
    lf_data.cost = gsl::matrix(nframe, ncands);
    // prev=zeros(nframe,ncands);      % traceback pointer
    lf_data.prev = gsl::matrix(nframe, ncands);

/******************************** Do processing - exhaustive search and dynamic programming ***************************/

    // for n=1:length(GCI)
    for (int n = 0; n < data.gci_inds.size(); ++n) {
        double pulseLen;

        if (n == 0)
        {
            pulseLen = round((data.gci_inds[n + 1] - data.gci_inds[n]) * pulseNum);
            lf_data.F0_cur = params.fs / (round(data.gci_inds[n + 1] - data.gci_inds[n]));
        }
        else
        {
            pulseLen = round((data.gci_inds[n] - data.gci_inds[n - 1]) * pulseNum);
            lf_data.F0_cur = params.fs / (round(data.gci_inds[n] - data.gci_inds[n - 1]));
        }

        // pulseLen=abs(pulseLen);
        pulseLen = std::abs(pulseLen);


        //        if GCI(n)-round(pulseLen/2) > 0
        //            start=GCI(n)-round(pulseLen/2);
        //        else start=1;
        //        end
        int start;
        int finish;

        if (data.gci_inds[n] - round(pulseLen / 2) > 0) {
            start = data.gci_inds[n] - round(pulseLen / 2);
        } else {
            start = 1;
        }


        //        if GCI(n)+round(pulseLen/2) <= length(glot)
        //        finish = GCI(n)+round(pulseLen/2);
        //        else finish = length(glot);
        //        end
        if (data.gci_inds[n] + round(pulseLen / 2) <= data.source_signal.size())
        {
            finish = data.gci_inds[n] + round(pulseLen / 2);
        }
        else
        {
            finish = data.source_signal.size();
        }



        //        glot_seg=glot(start:finish);
        //        glot_seg=glot_seg(:);
        int segment_length = finish - start + 1;
        lf_data.glot_seg.resize(segment_length);




        for (int i = start; i <= finish; ++i)
        {
            lf_data.glot_seg[i - start] = data.source_signal[i];
        }
        //        glot_seg_spec=20*log10(abs(fft(glot_seg)));


        //        size_t fft_len = lf_data.glot_seg.size();
        //        ComplexVector glot_seg_spec(fft_len);
        //        // Perform FFT on glot_seg
        //        FFTRadix2(lf_data.glot_seg, fft_len, &glot_seg_spec);
        //
        //        lf_data.glot_seg_spec = glot_seg_spec.getAbs();
        //
        //        for (size_t i = 0; i < lf_data.glot_seg_spec.size(); i++) {
        //            lf_data.glot_seg_spec(i) = 20 * log10(lf_data.glot_seg_spec(i));
        //        }

        //  glot_seg_spec=20*log10(abs(fft(glot_seg)));
        ComplexVector glot_seg_spec;
        FFTRadix2(lf_data.glot_seg, &glot_seg_spec);

        lf_data.glot_seg_spec = glot_seg_spec.getAbs();
        for (size_t i = 0; i < lf_data.glot_seg_spec.size(); i++) {
            lf_data.glot_seg_spec(i) = 20 * log10(lf_data.glot_seg_spec(i));
        }


        //   freq=linspace(0,fs,length(glot_seg));
        lf_data.freq.resize(lf_data.glot_seg.size());

        //  double start = 0.0;
        //  double stop = params.fs;
        //  double step = (stop - start) / (lf_data.glot_seg.size() - 1);

        for (size_t i = 0; i < lf_data.glot_seg.size(); i++) {
            lf_data.freq(i) = 0.0 + i * ((params.fs - 0.0) / (lf_data.glot_seg.size() - 1));
        }



        // err_mat=zeros(1,length(Rd_set));
        lf_data.err_mat.resize(lf_data.Rd_set.size());
        lf_data.err_mat.set_zero();

        // err_mat_time=zeros(1,length(Rd_set));
        lf_data.err_mat_time.resize(lf_data.Rd_set.size());
        lf_data.err_mat_time.set_zero();

        // EE(n)=abs(min(glot_seg));
        double min_value = lf_data.glot_seg.min();

        double abs_min_value = std::abs(min_value);
        lf_data.EE(n) = abs_min_value;


        // for m=1:length(Rd_set)
        for (int m = 0; m < lf_data.Rd_set.size(); ++m) {
            //         [Ra_cur,Rk_cur,Rg_cur] = Rd2R(Rd_set(m),EE(n),F0_cur);
            Rd2R(lf_data.Rd_set(m), lf_data.EE(n), lf_data.F0_cur, lf_data.Ra_cur, lf_data.Rk_cur, lf_data.Rg_cur);




            //          pulse = lf_cont(F0_cur,fs,Ra_cur,Rk_cur,Rg_cur,EE(n));
            lf_cont(lf_data.F0_cur, params.fs, lf_data.Ra_cur, lf_data.Rk_cur, lf_data.Rg_cur, lf_data.EE(n), lf_data.pulse);

            // LFgroup = makePulseCentGCI(pulse,pulseLen,GCI(n)-start,finish-GCI(n));
            lf_data.LFgroup = makePulseCentGCI(lf_data.pulse, pulseLen, data.gci_inds(n)-start, finish-data.gci_inds(n));

            // LFgroup_win=LFgroup(:);
            lf_data.LFgroup_win = lf_data.LFgroup;


            //  glot_seg_spec=20*log10(abs(fft(glot_seg)));
            ComplexVector LFgroup_win_spec;
            FFTRadix2(lf_data.LFgroup, &LFgroup_win_spec);

            lf_data.LFgroup_win_spec = LFgroup_win_spec.getAbs();
            // Print the values of temp
            for (size_t i = 0; i < lf_data.LFgroup_win_spec.size(); i++) {
                lf_data.LFgroup_win_spec(i) = 20 * log10(lf_data.LFgroup_win_spec(i));
            }


/******************************** Time domain error function **********************************************************/
        //                    cor_time = corrcoef(glot_seg,LFgroup_win);
        //                    cor_time=abs(cor_time(2));
        //                    err_time=1-cor_time;
        //                    err_mat_time(m)=err_time;


            lf_data.cor_time = computeCorrelation(lf_data.glot_seg, lf_data.LFgroup_win);

            lf_data.cor_time = std::abs(lf_data.cor_time);
            lf_data.err_time = 1 - lf_data.cor_time;
            lf_data.err_mat_time[m] = lf_data.err_time;




/******************************* Frequency domain error function ******************************************************/
        //            % Frequency domain error function
        //            cor_freq = corrcoef(glot_seg_spec(freq<MVF),LFgroup_win_spec(freq<MVF));
        //            cor_freq=abs(cor_freq(2));
        //            err_freq=1-cor_freq;


            lf_data.cor_freq = computeCorrelation(lf_data.glot_seg_spec, lf_data.LFgroup_win_spec);

            lf_data.cor_freq = std::abs(lf_data.cor_freq);
            lf_data.err_freq = 1 - lf_data.cor_freq;



/******************************** Combined error with weights *********************************************************/
//          err_mat(m)=(err_time*time_wgt)+(err_freq*freq_wgt);

            lf_data.err_mat[m] = (lf_data.err_time * time_wgt) + (lf_data.err_freq * freq_wgt);


        }


/******************************** Find best ncands (local costs and Rd values) ****************************************/
//          [err_mat_sort,err_mat_sortIdx]=sort(err_mat);
//          Rd_n(n,1:ncands)=Rd_set(err_mat_sortIdx(1:ncands));


            // Copy err_mat to a new vector for sorting
            lf_data.err_mat_sort = lf_data.err_mat;

            // Convert gsl vector "err_mat_sort_std" into std::vector & Sort std::vector in ascending order
            std::vector<double> err_mat_sort_std(lf_data.err_mat_sort.size());
            for (size_t i = 0; i < lf_data.err_mat_sort.size(); ++i) {
                err_mat_sort_std[i] = lf_data.err_mat_sort[i];
            }
            std::sort(err_mat_sort_std.begin(), err_mat_sort_std.end());

            // Copy sorted elements back to gsl::vector
            for (size_t i = 0; i < lf_data.err_mat_sort.size(); ++i) {
                lf_data.err_mat_sort[i] = err_mat_sort_std[i];
            }

            // Create a new vector called "err_mat_sortIdx"
            lf_data.err_mat_sortIdx.resize(lf_data.err_mat_sort.size());
            // Obtain the sorted indices
            for (size_t i = 0; i < lf_data.err_mat_sort.size(); ++i) {
                for (size_t j = 0; j < lf_data.err_mat_sort.size(); ++j) {
                    if (lf_data.err_mat_sort[i] == lf_data.err_mat[j]) {
                        lf_data.err_mat_sortIdx[i] = j;
                        break;
                    }
                }
            }



            //  Rd_n(n,1:ncands)=Rd_set(err_mat_sortIdx(1:ncands));

                // 1. Get the err_mat_sortIdx(1:ncands) like the index value of the vectors
            lf_data.Rd_set_err_mat_sortIdx = lf_data.err_mat_sortIdx.subvector(1, ncands);


                // 2. Use the ID vectors to tract the values to replace "Rd_set(err_mat_sortIdx(1:ncands))"
            lf_data.Rd_set_err_mat_sortVal.resize(lf_data.Rd_set_err_mat_sortIdx.size());

            for (size_t i = 0; i < ncands; i++)
            {
                int index = lf_data.Rd_set_err_mat_sortIdx[i];
                lf_data.Rd_set_err_mat_sortVal(i) = lf_data.Rd_set[index];
                lf_data.Rd_n(n, i) = lf_data.Rd_set_err_mat_sortVal(i);

            }






            // exh_err_n=err_mat_sort(1:ncands);
            lf_data.exh_err_n = lf_data.err_mat_sort.subvector(1, ncands);


            // cost(n,1:ncands) = exh_err_n(:)';
            for (size_t i = 0; i < ncands; i++)
            {
                lf_data.cost(n, i) = lf_data.exh_err_n(i);
            }



/******************************** Find optimum Rd value (dynamic programming) ****************************************/
            if (n > 1) {

                gsl::matrix costm(ncands, ncands); // transition cost matrix: rows (previous), cols (current)
                costm.set_all(0); // Initialize costm to all zeros

                for (int c = 0; c < ncands; ++c) {
                    // Transitions TO states in current frame
                    Rd2R(lf_data.Rd_n(n, c), lf_data.EE(n), lf_data.F0_cur, lf_data.Ra_try, lf_data.Rk_try, lf_data.Rg_try);


                    lf_cont(lf_data.F0_cur, params.fs, lf_data.Ra_try, lf_data.Rk_try, lf_data.Rg_try, lf_data.EE(n), lf_data.LFpulse_cur);


                    for (int p = 0; p < ncands; ++p) {

                        // Transitions FROM states in previous frame
                        // [Ra_prev,Rk_prev,Rg_prev] = Rd2R(Rd_n(n-1,p),EE(n),F0_cur);

                        Rd2R(lf_data.Rd_n(n-1,p), lf_data.EE(n), lf_data.F0_cur, lf_data.Ra_prev, lf_data.Rk_prev, lf_data.Rg_prev);

                        // LFpulse_prev = lf_cont(F0_cur,fs,Ra_prev,Rk_prev,Rg_prev,EE(n));
                        lf_cont(lf_data.F0_cur, params.fs, lf_data.Ra_prev, lf_data.Rk_prev, lf_data.Rg_cur, lf_data.EE(n), lf_data.LFpulse_prev);


                        if (std::isnan( lf_data.LFpulse_cur(0)) || std::isnan( lf_data.LFpulse_prev(0))) {
                            costm(p, c) = 0;
                        } else {
                            double cor_cur = computeCorrelation( lf_data.LFpulse_cur,  lf_data.LFpulse_prev);
                            costm(p, c) = (1 - std::abs(cor_cur)) * trans_wgt; // transition cost
                        }



                        //           costm=costm+repmat(cost(n-1,1:ncands)',1,ncands);  % add in cumulative costs
                        //           [costi,previ]=min(costm,[],1);
                        //           cost(n,1:ncands)=cost(n,1:ncands)+costi;
                        //           prev(n,1:ncands)=previ;

                        std::vector<double> costi(ncands);
                        std::vector<int> previ(ncands);

                        for (int j = 0; j < ncands; j++) {
                            for (int i = 0; i < costm.get_rows(); i++) {
                                costi[i] = costm(i, j);
                            }
                            // Find the index of the minimum value in costi
                            double minVal = costi[0];
                            size_t idx = 0;
                            for (size_t i = 1; i < costi.size(); ++i) {
                                if (costi[i] < minVal) {
                                    minVal = costi[i];
                                    idx = i;
                                }
                            }
                            previ[j] = idx;
                            lf_data.cost(n, j) += costi[previ[j]];
                        }


                        // Update prev matrix
                        for (int j = 0; j < ncands; j++) {
                            lf_data.prev(n, j) = previ[j];
                        }
                    }
                }
            }


        // gsl::vector_int idx_values(n);  // Declare a gsl::vector_int to store the idx values
/************************************** Do traceback ******************************************************************/
        //        best=zeros(n,1);
        //        [~,best(n)]=min(cost(n,1:ncands));
        lf_data.best.resize(n+1);  // Declare a gsl::vector_int to store the idx values
        lf_data.best.set_zero(); // Declare a gsl::vector_int to store the idx values

        for (size_t i = 0; i < n; ++i) {
            // Find the index of the minimum value in the subset of cost matrix
            double minVal = lf_data.cost(i, 0);
            size_t idx = 0;

            for (size_t j = 1; j < ncands; ++j) {
                if (lf_data.cost(i, j) < minVal) {
                    minVal = lf_data.cost(i, j);
                    idx = j;
                }
            }

            lf_data.best(i) = static_cast<int>(idx);  // Store the idx value in the gsl::vector_int
        }

        //        for i=n:-1:2
        //          best(i-1)=prev(i,best(i));
        //        end


        for (int i = n; i >= 2; i--) {
            lf_data.best(i - 2) = lf_data.prev(i, lf_data.best(i - 1));
        }

    }


    //    Rd_opt=zeros(1,nframe);
    lf_data.Rd_opt.resize(nframe);
    lf_data.Rd_opt.set_zero(); // Declare a gsl::vector_int to store the idx values


    //    for n=1:nframe
    //    Rd_opt(n) = Rd_n(n,best(n));
    //    end
    for (int n = 0; n < nframe; n++) {
        lf_data.Rd_opt[n] = lf_data.Rd_n(n, lf_data.best[n]);
    }



    medfilt1(lf_data.Rd_opt, 11);

    smooth(lf_data.Rd_opt, 5);


    //    Rd_opt = smooth(medfilt1(Rd_opt,11),5)*.5;
    for (size_t i = 0; i < lf_data.Rd_opt.size(); i++) {
        lf_data.Rd_opt[i] *= 0.5;
    }



    /* Finish */
    //    std::cout << "*********************Finished analysis.*********************" << std::endl << std::endl;
    //    std::cout << "*********************Rd_opt params*********************"<< lf_data.Rd_opt << std::endl;

    return EXIT_SUCCESS;

}

/***********/
/*   EOF   */
/***********/

