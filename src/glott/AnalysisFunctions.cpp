// Copyright 2016-2018 Lauri Juvela and Manu Airaksinen
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

#include <gslwrap/vector_double.h>
#include <gslwrap/vector_int.h>
#include <cmath>
#include <cfloat>

#include "definitions.h"
#include "SpFunctions.h"
#include "FileIo.h"
#include "InverseFiltering.h"
#include "PitchEstimation.h"
#include "AnalysisFunctions.h"
#include "Utils.h"
#include "Filters.h"
#include "QmfFunctions.h"

int PULSE_NOT_FOUND = -1;












int PolarityDetection(const Param &params, gsl::vector *signal,
                      gsl::vector *source_signal_iaif) {
  switch (params.signal_polarity) {
    case POLARITY_DEFAULT:
      return EXIT_SUCCESS;

    case POLARITY_INVERT:
      std::cout << " -- Inverting polarity (SIGNAL_POLARITY = \"INVERT\")"
                << std::endl;
      (*signal) *= (double)-1.0;
      return EXIT_SUCCESS;

    case POLARITY_DETECT:
      std::cout << "Using automatic polarity detection ...";

      if (Skewness(*source_signal_iaif) > 0) {
        std::cout << "... Detected negative polarity. Inverting signal."
                  << std::endl;
        (*signal) *= (double)-1.0;
        (*source_signal_iaif) *= (double)-1.0;
      } else {
        std::cout << "... Detected positive polarity." << std::endl;
      }
      return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}

/**
 * Get the F0 vector if the analyzed signal.
 * input: params, signal
 * output: fundf: Obtained F0 vector.
 *
 */
int GetF0(const Param &params, const gsl::vector &signal,
          const gsl::vector &source_signal_iaif, gsl::vector *fundf) {
  std::cout << "F0 analysis ";

  if (params.use_external_f0) {
    std::cout << "using external F0 file: " << params.external_f0_filename
              << " ...";
    gsl::vector fundf_ext;
    if (ReadGslVector(params.external_f0_filename.c_str(), params.data_type,
                      &fundf_ext) == EXIT_FAILURE)
      return EXIT_FAILURE;

    if (fundf_ext.size() != (size_t)params.number_of_frames) {
      std::cout << "Warning: External F0 file length differs from number of "
                   "frames. Interpolating external "
                   "F0 length to match number of frames.  External F0 length: "
                << fundf_ext.size()
                << ", Number of frames: " << params.number_of_frames
                << std::endl;
      InterpolateNearest(fundf_ext, params.number_of_frames, fundf);
    } else {
      fundf->copy(fundf_ext);
    }
  } else {
    *fundf = gsl::vector(params.number_of_frames);
    gsl::vector signal_frame = gsl::vector(params.frame_length);
    // gsl::vector glottal_frame = gsl::vector(2*params.frame_length); // Longer
    // frame
    gsl::vector glottal_frame =
        gsl::vector(params.frame_length_long);  // Longer frame
    int frame_index;
    double ff;
    gsl::matrix fundf_candidates(params.number_of_frames,
                                 NUMBER_OF_F0_CANDIDATES);
    gsl::vector candidates_vec(NUMBER_OF_F0_CANDIDATES);
    for (frame_index = 0; frame_index < params.number_of_frames;
         frame_index++) {
      GetFrame(signal, frame_index, params.frame_shift, &signal_frame, NULL);
      GetFrame(source_signal_iaif, frame_index, params.frame_shift,
               &glottal_frame, NULL);

      FundamentalFrequency(params, glottal_frame, signal_frame, &ff,
                           &candidates_vec);
      (*fundf)(frame_index) = ff;

      fundf_candidates.set_row_vec(frame_index, candidates_vec);
    }

    /* Copy original F0 */
    gsl::vector fundf_orig(*fundf);

    /* Process */
    MedianFilter(3, fundf);
    FillF0Gaps(fundf);
    FundfPostProcessing(params, fundf_orig, fundf_candidates, fundf);
    MedianFilter(3, fundf);
    FillF0Gaps(fundf);
    FundfPostProcessing(params, fundf_orig, fundf_candidates, fundf);
    MedianFilter(3, fundf);
  }
  std::cout << " done." << std::endl;
  return EXIT_SUCCESS;
}

/**
 * Get the glottal closure instants (GCIs) of the analyzed signal.
 * input: params, signal
 * output: gci_signal: Sparse signal-length representation of gcis as ones and otherwise zeros
 * gci_inds I think so
 */



// This code should be modified for the later days cuz the low efficicency, which read the RemoveDuplicatedGciIndices
// This is very low efficiency!!!!!!!!!! TODO
void RemoveDuplicateGciIndices(gsl::vector_int *gci_inds) {
    gsl::vector_int temp(gci_inds->size());
    size_t temp_index = 0;

    for(size_t i = 0; i < gci_inds->size(); ++i) {
        bool is_duplicate = false;
        for (size_t j = 0; j < temp_index; ++j) {
            if ((*gci_inds)(i) == temp(j)) {
                is_duplicate = true;
                break;
            }
        }

        if (!is_duplicate) {
            temp(temp_index) = (*gci_inds)(i);
            temp_index++;
        }
    }

    // Resize gci_inds to the correct size
    *gci_inds = gsl::vector_int(temp_index);

    // Copy unique values from temp to gci_inds
    for(size_t i = 0; i < temp_index; ++i) {
        (*gci_inds)(i) = temp(i);
    }
}



int GetGci(const Param &params, const gsl::vector &signal, const gsl::vector &source_signal_iaif, const gsl::vector &fundf, gsl::vector_int *gci_inds) {
	if(params.use_external_gci) {
		std::cout << "Reading GCI information from external file: " << params.external_gci_filename << " ...";
		gsl::vector gcis;
		if(ReadGslVector(params.external_gci_filename.c_str(), params.data_type, &gcis) == EXIT_FAILURE)
         return EXIT_FAILURE;
		*gci_inds = gsl::vector_int(gcis.size());
		size_t i;
		for (i=0; i<gci_inds->size();i++) {
			(*gci_inds)(i) = (int)round(gcis(i) * params.fs);
		}
	} else {
      std::cout << "GCI estimation using the SEDREAMS algorithm ...";

      gsl::vector mean_based_signal(signal.size(),true);

      MeanBasedSignal(signal, params.fs, getMeanF0(fundf), &mean_based_signal);
      MovingAverageFilter(3,&mean_based_signal); // remove small fluctuation

      SedreamsGciDetection(source_signal_iaif,mean_based_signal,gci_inds);

	}

    RemoveDuplicateGciIndices(gci_inds);
    std::cout << " done." << std::endl;
	return EXIT_SUCCESS;
}

int GetGain(const Param &params, const gsl::vector &fundf,
            const gsl::vector &signal, gsl::vector *gain_ptr) {
  // double E_REF = 0.00001;
  gsl::vector frame = gsl::vector(params.frame_length);
  gsl::vector unvoiced_frame = gsl::vector(params.frame_length_unvoiced);
  gsl::vector gain = gsl::vector(params.number_of_frames);
  ComplexVector frame_fft;
  size_t NFFT = 4096;  // Long FFT
  double MIN_LOG_POWER = -100.0;
  gsl::vector fft_mag(NFFT / 2 + 1);
  // int min_uv_frequency = rint((double)NFFT/(double)(params.fs)*000.0);

  int frame_index;

  frame.set_all(1.0);
  ApplyWindowingFunction(params.default_windowing_function, &frame);
  double frame_energy_compensation = sqrt(frame.size() / getSquareSum(frame));
  double frame_energy;
  bool frame_is_voiced;
  for (frame_index = 0; frame_index < params.number_of_frames; frame_index++) {
    frame_is_voiced = fundf(frame_index) > 0.0;
    if (frame_is_voiced) {
      GetFrame(signal, frame_index, params.frame_shift, &frame, NULL);
      ApplyWindowingFunction(params.default_windowing_function, &frame);
      frame_energy = getEnergy(frame);
      if (frame_energy == 0.0) frame_energy = +DBL_MIN;

      frame_energy *= frame_energy_compensation;  //(8.0/3.0);// Compensate
                                                  // windowing gain loss
      gain(frame_index) = FrameEnergy2LogEnergy(frame_energy, frame.size());

    } else {
      GetFrame(signal, frame_index, params.frame_shift, &unvoiced_frame, NULL);
      ApplyWindowingFunction(params.default_windowing_function,
                             &unvoiced_frame);
      frame_energy = getEnergy(unvoiced_frame);

      if (frame_energy == 0.0) frame_energy = +DBL_MIN;

      frame_energy *=
          frame_energy_compensation;  // Compensate windowing gain loss
      gain(frame_index) =
          FrameEnergy2LogEnergy(frame_energy, unvoiced_frame.size());
    }

    /* Clip gain at lower bound (prevent very low values for zero frames) */
    if (gain(frame_index) < MIN_LOG_POWER) gain(frame_index) = MIN_LOG_POWER;
  }
  *gain_ptr = gain;
  return EXIT_SUCCESS;
}

/**
 *
 *
 */
int SpectralAnalysis(const Param &params, const AnalysisData &data,
                     gsl::matrix *poly_vocal_tract) {                         
  gsl::vector frame(params.frame_length);
  gsl::vector unvoiced_frame(params.frame_length_unvoiced, true);
  gsl::vector pre_frame(params.lpc_order_vt * 2, true);
  gsl::vector lp_weight(params.frame_length + params.lpc_order_vt * 3, true);
  gsl::vector A(params.lpc_order_vt + 1, true);
  gsl::vector G(params.lpc_order_glot_iaif, true);
  gsl::vector B(1);
  B(0) = 1.0;
  // gsl::vector lip_radiation(2);lip_radiation(0) = 1.0; lip_radiation(1) =
  // 0.99;
  gsl::vector frame_pre_emph(params.frame_length);
  gsl::vector frame_full;  // frame + preframe
  gsl::vector residual(params.frame_length);

  if (params.use_external_lsf_vt == false) {
    std::cout << "Spectral analysis ...";
    /* Do analysis frame-wise */
    size_t frame_index;
    for (frame_index = 0; frame_index < (size_t)params.number_of_frames;
         frame_index++) {
      // GetPitchSynchFrame(data.signal, frame_index, params.frame_shift,
      // &frame, &pre_frame);
      /** Voiced analysis **/
      if (data.fundf(frame_index) != 0) {
        if (params.use_pitch_synchronous_analysis)
          GetPitchSynchFrame(params, data.signal, data.gci_inds, frame_index,
                             params.frame_shift, data.fundf(frame_index),
                             &frame, &pre_frame);
        else
          GetFrame(data.signal, frame_index, params.frame_shift, &frame,
                   &pre_frame);

        /* Estimate Weighted Linear Prediction weight */
        GetLpWeight(params, params.lp_weighting_function, data.gci_inds, frame,
                    frame_index, &lp_weight);
        /* Pre-emphasis and windowing */
        Filter(std::vector<double>{1.0, -params.gif_pre_emphasis_coefficient},
               B, frame, &frame_pre_emph);
        ApplyWindowingFunction(params.default_windowing_function,
                               &frame_pre_emph);
        /* First-loop envelope */
        ArAnalysis(params.lpc_order_vt, params.warping_lambda_vt,
                   params.lp_weighting_function, lp_weight, frame_pre_emph, &A);
        /* Second-loop envelope (if IAIF is used) */

        if (params.use_iterative_gif) {
          ConcatenateFrames(pre_frame, frame, &frame_full);
          if (params.warping_lambda_vt != 0.0) {
            Filter(A, B, frame_full, &residual);
          } else {
            WFilter(A, B, frame_full, params.warping_lambda_vt, &residual);
          }
          ApplyWindowingFunction(params.default_windowing_function, &residual);
          ArAnalysis(params.lpc_order_glot_iaif, 0.0, NONE, lp_weight, residual,
                     &G);
          Filter(G, B, frame, &frame_pre_emph);  // Iterated pre-emphasis
          ApplyWindowingFunction(params.default_windowing_function,
                                 &frame_pre_emph);
          ArAnalysis(params.lpc_order_vt, params.warping_lambda_vt,
                     params.lp_weighting_function, lp_weight, frame_pre_emph,
                     &A);
        }
        /** Unvoiced analysis **/
      } else {
        GetFrame(data.signal, frame_index, params.frame_shift, &unvoiced_frame,
                 &pre_frame);
        if (params.unvoiced_pre_emphasis_coefficient > 0.0) {
          Filter(
              std::vector<double>{
                  1.0, -1.0 * params.unvoiced_pre_emphasis_coefficient},
              std::vector<double>{1.0}, unvoiced_frame, &unvoiced_frame);
        }
        ApplyWindowingFunction(params.default_windowing_function,
                               &unvoiced_frame);
        ArAnalysis(params.lpc_order_vt, params.warping_lambda_vt, NONE,
                   lp_weight, unvoiced_frame, &A);
      }
      poly_vocal_tract->set_col_vec(frame_index, A);
    }
  } else {
    std::cout << "Using external vocal tract LSFs ... ";
    /* Read external vocal tract filter LSFs*/
    gsl::matrix external_lsf;
    ReadGslMatrix(params.external_lsf_vt_filename, params.data_type,
                  params.lpc_order_vt, &external_lsf);
    if (external_lsf.size2() < poly_vocal_tract->size2()) {
      std::cerr << "Warning: external LSF is missing "
                << poly_vocal_tract->size2() - external_lsf.size2()
                << " frames, zero-padding at the end" << std::endl;
    }
    gsl::vector a(params.lpc_order_vt + 1);
    for (size_t i = 0; i < poly_vocal_tract->size2(); i++) {
      if (i < external_lsf.size2()) {
        /* Convert external lsf to filter polynomial */
        Lsf2Poly(external_lsf.get_col_vec(i), &a);
      } else {
        /* Pad missing frames with a flat filter */
        a.set_all(0.0);
        a(0) = 1.0;
      }
      poly_vocal_tract->set_col_vec(i, a);
    }
  }

  std::cout << " done." << std::endl;
  return EXIT_SUCCESS;
}

int SpectralAnalysisQmf(const Param &params, const AnalysisData &data,
                        gsl::matrix *poly_vocal_tract) {
  gsl::vector frame(params.frame_length);
  gsl::vector frame_pre_emph(params.frame_length);
  gsl::vector pre_frame(params.lpc_order_vt, true);
  gsl::vector frame_qmf1(frame.size() / 2);  // Downsampled low-band frame
  gsl::vector frame_qmf2(frame.size() / 2);  // Downsampled high-band frame
  gsl::vector lp_weight_downsampled(frame_qmf1.size() +
                                    params.lpc_order_vt_qmf1);
  gsl::vector B(1);
  B(0) = 1.0;

  gsl::vector H0 =
      StdVector2GslVector(kCUTOFF05PI);  // Load hard-coded low-pass filter
  gsl::vector H1 = Qmf::GetMatchingFilter(H0);

  gsl::vector lp_weight(params.frame_length + params.lpc_order_vt, true);
  gsl::vector A(params.lpc_order_vt + 1, true);
  gsl::vector A_qmf1(params.lpc_order_vt_qmf1 + 1, true);
  gsl::vector A_qmf2(params.lpc_order_vt_qmf2 + 1, true);
  // gsl::vector lsf_qmf1(params.lpc_order_vt_qmf1,true);
  // gsl::vector lsf_qmf2(params.lpc_order_vt_qmf2,true);
  // gsl::vector gain_qmf(params.number_of_frames);
  double gain_qmf, e1, e2;

  gsl::vector lip_radiation(2);
  lip_radiation(0) = 1.0;
  lip_radiation(1) = -params.gif_pre_emphasis_coefficient;

  // gsl::vector frame_full; // frame + preframe
  // gsl::vector residual_full; // residual with preframe

  std::cout << "QMF sub-band-based spectral analysis ...";

  size_t frame_index;
  for (frame_index = 0; frame_index < (size_t)params.number_of_frames;
       frame_index++) {
    GetFrame(data.signal, frame_index, params.frame_shift, &frame, &pre_frame);

    /** Voiced analysis (Low-band = QCP, High-band = LPC) **/
    if (data.fundf(frame_index) != 0) {
      /* Pre-emphasis */
      Filter(lip_radiation, B, frame, &frame_pre_emph);
      Qmf::GetSubBands(frame_pre_emph, H0, H1, &frame_qmf1, &frame_qmf2);
      /* Gain differences between frame_qmf1 and frame_qmf2: */

      e1 = getEnergy(frame_qmf1);
      e2 = getEnergy(frame_qmf2);
      if (e1 == 0.0) e1 += DBL_MIN;
      if (e2 == 0.0) e2 += DBL_MIN;
      gain_qmf = 20 * log10(e2 / e1);

      /** Low-band analysis **/
      GetLpWeight(params, params.lp_weighting_function, data.gci_inds, frame,
                  frame_index, &lp_weight);
      Qmf::Decimate(lp_weight, 2, &lp_weight_downsampled);

      ApplyWindowingFunction(params.default_windowing_function, &frame_qmf1);
      ArAnalysis(params.lpc_order_vt_qmf1, 0.0, params.lp_weighting_function,
                 lp_weight_downsampled, frame_qmf1, &A_qmf1);

      /** High-band analysis **/
      // ApplyWindowingFunction(params.default_windowing_function,&frame_qmf2);
      ArAnalysis(params.lpc_order_vt_qmf2, 0.0, NONE, lp_weight_downsampled,
                 frame_qmf2, &A_qmf2);

      Qmf::CombinePoly(A_qmf1, A_qmf2, gain_qmf, (int)frame_qmf1.size(), &A);
      /** Unvoiced analysis (Low-band = LPC, High-band = LPC, no pre-emphasis)
       * **/
    } else {
      // Qmf::GetSubBands(frame, H0, H1, &frame_qmf1, &frame_qmf2);

      // e1 = getEnergy(frame_qmf1);
      // e2 = getEnergy(frame_qmf2);
      // if(e1 == 0.0)
      //   e1 += DBL_MIN;
      // if(e2 == 0.0)
      //   e2 += DBL_MIN;
      // gain_qmf = 20*log10(e2/e1);

      /** Low-band analysis **/
      // ApplyWindowingFunction(params.default_windowing_function,&frame_qmf1);
      // ArAnalysis(params.lpc_order_vt_qmf1,0.0,NONE, lp_weight_downsampled,
      // frame_qmf2, &A_qmf1);

      /** High-band analysis **/
      // ApplyWindowingFunction(params.default_windowing_function,&frame_qmf2);
      // ArAnalysis(params.lpc_order_vt_qmf2,0.0,NONE, lp_weight_downsampled,
      // frame_qmf2, &A_qmf2);
      ApplyWindowingFunction(params.default_windowing_function, &frame);
      ArAnalysis(params.lpc_order_vt, 0.0, NONE, lp_weight_downsampled, frame,
                 &A);
    }

    poly_vocal_tract->set_col_vec(frame_index, A);
    // Poly2Lsf(A_qmf1,&lsf_qmf1);
    // Poly2Lsf(A_qmf2,&lsf_qmf2);
    // lsf_qmf1->set_col_vec(frame_index,lsf_qmf1);
    // lsf_qmf2->set_col_vec(frame_index,lsf_qmf2);
  }
  return EXIT_SUCCESS;
}

int InverseFilter(const Param &params, const AnalysisData &data,
                  gsl::matrix *poly_glot, gsl::vector *source_signal) {
  size_t frame_index;
  gsl::vector frame(params.frame_length, true);
  gsl::vector pre_frame(2 * params.lpc_order_vt, true);
  gsl::vector frame_full(frame.size() + pre_frame.size());  // Pre-frame + frame
  gsl::vector frame_residual(params.frame_length);
  gsl::vector a_glot(params.lpc_order_glot + 1);
  gsl::vector b(1);
  b(0) = 1.0;

  // for linear frequency scale inverse filtering
  gsl::vector a_lin(params.lpc_order_vt + 1);
  gsl::vector a_lin_high_order(3 * params.lpc_order_vt +
                               1);  // arbitrary high order
  size_t NFFT = 4096;
  gsl::vector impulse(params.frame_length);
  gsl::vector imp_response(params.frame_length);
  gsl::vector pre_frame_high_order(3 * a_lin_high_order.size());
  gsl::vector frame_full_high_order(frame.size() + pre_frame_high_order.size());

  for (frame_index = 0; frame_index < (size_t)params.number_of_frames;
       frame_index++) {
    if (params.use_pitch_synchronous_analysis) {
      GetPitchSynchFrame(params, data.signal, data.gci_inds, frame_index,
                         params.frame_shift, data.fundf(frame_index), &frame,
                         &pre_frame);
      frame_residual.resize(frame.size());
    } else {
      GetFrame(data.signal, frame_index, params.frame_shift, &frame,
               &pre_frame);
      GetFrame(data.signal, frame_index, params.frame_shift, &frame,
               &pre_frame_high_order);
    }

    ConcatenateFrames(pre_frame, frame, &frame_full);
    ConcatenateFrames(pre_frame_high_order, frame, &frame_full_high_order);

    if (params.warping_lambda_vt == 0.0) {
      Filter(data.poly_vocal_tract.get_col_vec(frame_index), b, frame_full,
             &frame_residual);
    } else {
      gsl::vector a_warp(data.poly_vocal_tract.get_col_vec(frame_index));
      // get warped filter linear frequency response via impulse response
      imp_response.set_zero();
      impulse.set_zero();
      // give pre-frame (only affects phase, not filter fit)
      impulse(a_lin_high_order.size()) = 1.0;
      // get inverse filter impulse response
      WFilter(a_warp, b, impulse, params.warping_lambda_vt, &imp_response);
      // Do high-order LP fit on the inverse filter (FIR polynomial)
      StabilizePoly(NFFT, imp_response, &a_lin_high_order);
      // Linear filtering
      Filter(a_lin_high_order, b, frame_full_high_order, &frame_residual);
    }

    double ola_gain =
        (double)params.frame_length / ((double)params.frame_shift * 2.0);
    // Scale by frame energy, TODO: remove?
    frame_residual *= LogEnergy2FrameEnergy(data.frame_energy(frame_index),
                                            frame_residual.size()) /
                      getEnergy(frame_residual) / ola_gain;
    ApplyWindowingFunction(params.default_windowing_function, &frame_residual);

    LPC(frame_residual, params.lpc_order_glot, &a_glot);
    size_t i;
    for (i = 0; i < a_glot.size(); i++) {
      if (gsl_isnan((a_glot)(i))) {
        (a_glot)(i) = (0.0);
      }
    }
    poly_glot->set_col_vec(frame_index, a_glot);

    OverlapAdd(frame_residual, frame_index * params.frame_shift,
               source_signal);  // center index = frame_index*params.frame_shift
  }

  return EXIT_SUCCESS;
}

int Find_nearest_pulse_index(const int &sample_index,
                             const gsl::vector &gci_inds, const Param &params,
                             const double &f0) {
  int j;
  // int i,k;
  int pulse_index = -1;  // Return value initialization

  if (!gci_inds.is_set())
    return PULSE_NOT_FOUND;

  int dist, min_dist, ppos;
  min_dist = INT_MAX;
  /* Find the shortest distance between sample index and gcis */
  for (j = 1; j < (int)gci_inds.size() - 1; j++) {
    ppos = gci_inds(j);
    dist = abs(sample_index - ppos);
    if (dist > min_dist) {
      break;
    }
    min_dist = dist;
    pulse_index = j;
  }

  /* Return the closest GCI if unvoiced */
  if (f0 == 0) return pulse_index;

  double pulselen, targetlen;
  targetlen = 2.0 * params.fs / f0;
  pulselen = round(gci_inds(pulse_index + 1) - gci_inds(pulse_index - 1)) + 1;

  int new_pulse_index;
  int prev_index = pulse_index - 1;
  int next_index = pulse_index + 1;
  int prev_gci, next_gci;

  double max_relative_len_diff = params.max_pulse_len_diff;
  double relative_len_diff = (fabs(pulselen - targetlen) / targetlen);

  /* Choose next closest while pulse length deviates too much from f0 */
  while (relative_len_diff > max_relative_len_diff) {
    /* Prevent illegal reads*/
    if (prev_index < 0) prev_index = 0;
    if (next_index > (int)gci_inds.size() - 1) next_index = gci_inds.size() - 1;

    prev_gci = gci_inds(prev_index);
    next_gci = gci_inds(next_index);

    /* choose closest below or above, increment for next iteration */
    if (abs(sample_index - next_gci) < abs(sample_index - prev_gci)) {
      new_pulse_index = next_index;
      next_index++;
    } else {
      new_pulse_index = prev_index;
      prev_index++;
    }

    /* break if out of range */
    if (new_pulse_index - 1 < 0 ||
        new_pulse_index + 1 > (int)gci_inds.size() - 1) {
      break;
    } else {
      pulse_index = new_pulse_index;
    }

    /* if pulse center gets too far from sample index, relax constraint and
     * start over */
    if (fabs(sample_index - gci_inds(pulse_index)) > 1.0 * targetlen) {
      max_relative_len_diff += 0.02;  // increase by 5 percent
      //std::cout << "could not find pulse in F0 range, relaxing constraint" << std::endl;
      if (max_relative_len_diff > 3.0) {
        break;
      }
      pulse_index = j;
      prev_index = pulse_index - 1;
      next_index = pulse_index + 1;
    }

    /* break if out of range */
    if (new_pulse_index - 1 < 0 ||
        new_pulse_index + 1 > (int)gci_inds.size() - 1) {
      break;
    } else {
      pulse_index = new_pulse_index;
    }

    /* calculate new pulse length */
    pulselen = round(gci_inds(pulse_index + 1) - gci_inds(pulse_index - 1)) + 1;

    relative_len_diff = (fabs(pulselen - targetlen) / targetlen);
  }

  if (relative_len_diff > 3.0 || pulselen < 3) {
     return PULSE_NOT_FOUND;
  } else {
     return pulse_index;
  }

}

void GetPulses(const Param &params, const gsl::vector &source_signal,
               const gsl::vector_int &gci_inds, gsl::vector &fundf,
               gsl::matrix *pulses_mat) {
  if (params.extract_pulses_as_features == false) return;

  std::cout << "Extracting excitation pulses ";

  size_t frame_index;
  for (frame_index = 0; frame_index < (size_t)params.number_of_frames;
       frame_index++) {
    size_t sample_index = frame_index * params.frame_shift;
    int pulse_index = Find_nearest_pulse_index(sample_index, gci_inds,
                                                  params, fundf(frame_index));

    gsl::vector paf_pulse(params.paf_pulse_length, true);
    gsl::vector pulse;

    int center_index;
    /* Use frame center directly for unvoiced */
    if (fundf(frame_index) == 0.0 || pulse_index == PULSE_NOT_FOUND) {
      center_index = sample_index;
    } else {
      center_index = gci_inds(pulse_index);

      /* Check that pulse center index is reasonably
       * close to frame center index
       */
      int THRESH = 100 * params.frame_length;
      if (abs(center_index - (int)sample_index) > THRESH) {
        std::cerr
        << "Warning: no suitable pulse in range,"
        << "treating frame as unvoiced"
        << std::endl;
        std::cerr
        << "Frame: " << frame_index
        << ", distance: " << abs(center_index - (int)sample_index)
        << std::endl;
        center_index = sample_index;
      }
    }

    int i;
    size_t j;
    if (params.use_pulse_interpolation == true) {
      size_t pulselen;
      if (fundf(frame_index) > 0.0 && pulse_index != PULSE_NOT_FOUND) {
        /* Pulse length is two pitch periods, defined here by distance of
         * previous and next GCI */
        pulselen =
            round(gci_inds(pulse_index + 1) - gci_inds(pulse_index - 1)) + 1;
      } else {
        pulselen = params.paf_pulse_length;
      }

      /* Stretch pulse segment to paf_pulse_length */
      gsl::vector pulse_orig(pulselen);
      for (j = 0; j < pulselen; j++) {
        i = center_index - round(pulselen / 2.0) + j;
        if (i >= 0 && i < (int)source_signal.size())
          pulse_orig(j) = source_signal(i);
      }
      /* Interpolation on windowed signal prevents Gibbs at edges */
      ApplyWindowingFunction(params.paf_analysis_window, &pulse_orig);
      InterpolateSpline(pulse_orig, params.paf_pulse_length, &paf_pulse);

    } else {
      /* No interpolation, window with selected window */
      if (params.paf_analysis_window != RECT) {
        /* Apply pitch-synchronous analysis window to pulse */

        size_t T;
        if (fundf(frame_index) != 0.0) {
          /* Voiced: use two pitch periods () */
          T = round(2.0 * (double)params.fs / fundf(frame_index));
          if (T > paf_pulse.size()) T = paf_pulse.size();
        } else {
          /* Unvoiced: use all available space */
          T = paf_pulse.size();
        }

        pulse = gsl::vector(T);
        for (j = 0; j < T; j++) {
          i = center_index - round(pulse.size() / 2.0) + j;
          if (i >= 0 && i < (int)source_signal.size())
            pulse(j) = source_signal(i);
        }
        ApplyWindowingFunction(params.paf_analysis_window, &pulse);

        for (j = 0; j < pulse.size(); j++) {
          paf_pulse(
              (round(paf_pulse.size() / 2.0) - round(pulse.size() / 2.0)) + j) =
              pulse(j);
        }
      } else {
        /* params.paf_analysis_window == RECT */
        /* No windowing, just copy to paf_pulse */
        for (j = 0; j < paf_pulse.size(); j++) {
          i = center_index - round(paf_pulse.size() / 2.0) + j;
          if (i >= 0 && i < (int)source_signal.size())
            paf_pulse(j) = source_signal(i);
        }
      }
    }

    /* Normalize energy */
    if (params.use_paf_energy_normalization) {
      paf_pulse /= getEnergy(paf_pulse);
    }

    /* Save to matrix */
    pulses_mat->set_col_vec(frame_index, paf_pulse);
  }
  std::cout << "done." << std::endl;
}

void HighPassFiltering(const Param &params, gsl::vector *signal) {
  if (!params.use_highpass_filtering) return;

  std::cout
      << "High-pass filtering input signal with a cutoff frequency of 50Hz."
      << std::endl;

  gsl::vector signal_cpy(signal->size());
  signal_cpy.copy(*signal);

  if (params.fs < 40000) {
    Filter(k16HPCUTOFF50HZ, std::vector<double>{1}, signal_cpy, signal);
    signal_cpy.copy(*signal);
    signal_cpy.reverse();
    Filter(k16HPCUTOFF50HZ, std::vector<double>{1}, signal_cpy, signal);
    (*signal).reverse();
  } else {
    Filter(k44HPCUTOFF50HZ, std::vector<double>{1}, signal_cpy, signal);
    signal_cpy.copy(*signal);
    signal_cpy.reverse();
    Filter(k16HPCUTOFF50HZ, std::vector<double>{1}, signal_cpy, signal);
    (*signal).reverse();
  }
}

void GetIaifResidual(const Param &params, const gsl::vector &signal,
                     gsl::vector *residual) {
  gsl::vector frame(params.frame_length, true);
  gsl::vector frame_residual(params.frame_length, true);
  gsl::vector frame_pre_emph(params.frame_length, true);
  gsl::vector pre_frame(params.lpc_order_vt, true);
  gsl::vector frame_full(params.lpc_order_vt + params.frame_length, true);
  gsl::vector A(params.lpc_order_vt + 1, true);
  gsl::vector B(1);
  B(0) = 1.0;
  gsl::vector G(params.lpc_order_glot_iaif + 1, true);
  gsl::vector weight_fn;

  if (!residual->is_set()) *residual = gsl::vector(signal.size());

  size_t frame_index;
  for (frame_index = 0; frame_index < (size_t)params.number_of_frames;
       frame_index++) {
    GetFrame(signal, frame_index, params.frame_shift, &frame, &pre_frame);

    /* Pre-emphasis and windowing */
    Filter(std::vector<double>{1.0, -params.gif_pre_emphasis_coefficient}, B,
           frame, &frame_pre_emph);
    ApplyWindowingFunction(params.default_windowing_function, &frame_pre_emph);

    ArAnalysis(params.lpc_order_vt, 0.0, NONE, weight_fn, frame_pre_emph, &A);
    ConcatenateFrames(pre_frame, frame, &frame_full);

    Filter(A, B, frame_full, &frame_residual);

    ApplyWindowingFunction(params.default_windowing_function, &frame_residual);
    ArAnalysis(params.lpc_order_glot_iaif, 0.0, NONE, weight_fn, frame_residual,
               &G);

    Filter(G, B, frame, &frame_pre_emph);  // Iterated pre-emphasis
    ApplyWindowingFunction(params.default_windowing_function, &frame_pre_emph);

    ArAnalysis(params.lpc_order_vt, 0.0, NONE, weight_fn, frame_pre_emph, &A);

    Filter(A, B, frame_full, &frame_residual);

    /* Set energy of residual equal to energy of frame */
    double ola_gain =
        (double)params.frame_length / ((double)params.frame_shift * 2.0);
    frame_residual *= getEnergy(frame) / getEnergy(frame_residual) / ola_gain;

    ApplyWindowingFunction(HANN, &frame_residual);

    OverlapAdd(frame_residual, frame_index * params.frame_shift, residual);
  }
}

void HnrAnalysis(const Param &params, const gsl::vector &source_signal,
                 const gsl::vector &fundf, gsl::matrix *hnr_glott) {
  std::cout << "HNR Analysis ...";

  /* Variables */
  int hnr_channels = params.hnr_order;
  gsl::vector frame(params.frame_length_long);
  ComplexVector frame_fft;
  size_t NFFT = 4096;  // Long FFT
  double MIN_LOG_POWER = -60.0;
  gsl::vector fft_mag(NFFT / 2 + 1);

  gsl::vector_int harmonic_index;
  gsl::vector hnr_values;

  gsl::vector harmonic_values;  // experimental, ljuvela
  gsl::vector upper_env_values;
  gsl::vector lower_env_values;
  gsl::vector fft_lower_env(NFFT / 2 + 1, true);
  gsl::vector fft_upper_env(NFFT / 2 + 1);

  double kbd_alpha = 2.3;
  gsl::vector kbd_window =
      getKaiserBesselDerivedWindow(frame.size(), kbd_alpha);

  /* Linearly spaced frequency axis */
  gsl::vector_int x_interp = LinspaceInt(0, 1, fft_mag.size() - 1);

  gsl::vector hnr_interp(fft_mag.size());
  gsl::vector hnr_erb(hnr_channels);

  size_t frame_index, i;
  double val;
  for (frame_index = 0; frame_index < (size_t)params.number_of_frames;
       frame_index++) {
    GetFrame(source_signal, frame_index, params.frame_shift, &frame, NULL);
    // ApplyWindowingFunction(params.default_windowing_function, &frame);
    frame *= kbd_window;
    FFTRadix2(frame, NFFT, &frame_fft);
    fft_mag = frame_fft.getAbs();
    for (i = 0; i < fft_mag.size(); i++) {
      val =
          20 *
          log10(fft_mag(i));  // save to temp to prevent evaluation twice in max
      fft_mag(i) = GSL_MAX(val, MIN_LOG_POWER);  // Min log-power = -60dB
    }

    if (fundf(frame_index) > 0) {
      UpperLowerEnvelope(fft_mag, fundf(frame_index), params.fs, &fft_upper_env,
                         &fft_lower_env);
    } else {
      /* Define the upper envelope as the maxima around pseudo-period of 100Hz
       */
      UpperLowerEnvelope(fft_mag, 100.0, params.fs, &fft_upper_env,
                         &fft_lower_env);
    }

    /* HNR as upper-lower envelope difference */
    for (i = 0; i < hnr_interp.size(); i++)
      hnr_interp(i) = fft_lower_env(i) - fft_upper_env(i);

    /* Convert to erb-bands */
    Linear2Erb(hnr_interp, params.fs, &hnr_erb);
    hnr_glott->set_col_vec(frame_index, hnr_erb);
  }
  std::cout << " done." << std::endl;
}

int GetPitchSynchFrame(const Param &params, const gsl::vector &signal,
                       const gsl::vector_int &gci_inds, const int &frame_index,
                       const int &frame_shift, const double &f0,
                       gsl::vector *frame, gsl::vector *pre_frame) {
  int i, ind;
  size_t T0;
  if (f0 == 0.0) T0 = (size_t)frame_shift;
  // T0 = (size_t)params.frame_length;
  else
    T0 = (size_t)rint(params.fs / f0);

  (*frame) = gsl::vector(2 * T0, true);
  int center_index = (int)frame_index * frame_shift;
  int pulse_index =
      (int)Find_nearest_pulse_index(center_index, gci_inds, params, f0);
  if (abs(center_index - pulse_index) <= frame_shift)
    center_index = pulse_index;

  // Get samples to frame
  if (frame != NULL) {
    for (i = 0; i < (int)frame->size(); i++) {
      ind = center_index - ((int)frame->size()) / 2 +
            i;  // SPTK compatible, ljuvela
      if (ind >= 0 && ind < (int)signal.size()) {
        (*frame)(i) = signal(ind);
      }
    }
  } else {
    return EXIT_FAILURE;
  }

  // Get pre-frame samples for smooth filtering
  if (pre_frame) {
    for (i = 0; i < (int)pre_frame->size(); i++) {
      ind = center_index - (int)frame->size() / 2 + i -
            pre_frame->size();  // SPTK compatible, ljuvela
      if (ind >= 0 && ind < (int)signal.size()) (*pre_frame)(i) = signal(ind);
    }
  }

  return EXIT_SUCCESS;
}



double GetRd(const Param &params, const gsl::vector &source_signal,
             const gsl::vector_int &gci_inds, gsl::vector *Rd_opt) {
    std::cout << "Rd analysis ";

    if (params.use_external_f0) {
        std::cout << "using external F0 file: " << params.external_f0_filename
                  << " ...";

        *Rd_opt = gsl::vector(gci_inds.size());



//        gsl::vector signal_frame = gsl::vector(params.frame_length);
//        // gsl::vector glottal_frame = gsl::vector(2*params.frame_length); // Longer
//        // frame
//        gsl::vector glottal_frame =
//                gsl::vector(params.frame_length_long);  // Longer frame
//        int frame_index;
//        double ff;
//        gsl::matrix Rd_opt_candidates(params.number_of_frames,
//                                      NUMBER_OF_F0_CANDIDATES);
//        gsl::vector candidates_vec(NUMBER_OF_F0_CANDIDATES);
//        for (frame_index = 0; frame_index < params.number_of_frames;
//             frame_index++) {
//            GetFrame(signal, frame_index, params.frame_shift, &signal_frame, NULL);
//            GetFrame(gcis, frame_index, params.frame_shift,
//                     &glottal_frame, NULL);
//
//            FundamentalFrequency(params, glottal_frame, signal_frame, &ff,
//                                 &candidates_vec);
//            (*Rd_opt)(frame_index) = ff;
//
//            Rd_opt_candidates.set_row_vec(frame_index, candidates_vec);
//        }
//
//        /* Copy original F0 */
//        gsl::vector Rd_opt_orig(*Rd_opt);
//
//        /* Process */
//        MedianFilter(3, Rd_opt);
//        FillF0Gaps(Rd_opt);
//        Rd_optPostProcessing(params, Rd_opt_orig, Rd_opt_candidates, Rd_opt);
//        MedianFilter(3, Rd_opt);
//        FillF0Gaps(Rd_opt);
//        Rd_optPostProcessing(params, Rd_opt_orig, Rd_opt_candidates, Rd_opt);
//        MedianFilter(3, Rd_opt);
    }
    std::cout << " done." << std::endl;
    return EXIT_SUCCESS;
}