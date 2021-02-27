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

#include <cmath>
#include <gslwrap/vector_double.h>
#include <gslwrap/matrix_double.h>
#include "definitions.h"
#include "SpFunctions.h"
#include "InverseFiltering.h"
#include "Utils.h"

/**
 * Function GetFrameGcis
 *
 * Get frame-specific GCI indeces from the full-signal GCI index vector

 * @param params : Reference to the Analysis parameter struct
 * @param frame_index : Current frame index
 * @param gci_inds : Reference to GCI index vector for full signal
 * @return frame_gci_inds : Frame-specific GCI index vector (index count starts from 0)
 * author: @ljuvela
 **/
gsl::vector_int GetFrameGcis(const Param &params, const int frame_index, const gsl::vector_int &gci_inds) {

	/* Get frame sample range */
	int center_index = params.frame_shift*frame_index;
	int minind = center_index - round(params.frame_length/2);
	int maxind = center_index + round(params.frame_length/2) - 1 ;
	size_t min_gci_ind, max_gci_ind;

	/* Find the range of gci inds */
	for (min_gci_ind=0; gci_inds(min_gci_ind) < minind; min_gci_ind++)
		if (min_gci_ind >= gci_inds.size()-1) break;

	for (max_gci_ind=gci_inds.size()-1; gci_inds(max_gci_ind)>maxind; max_gci_ind--)
		if (max_gci_ind == 0) break;

	/* Allocate gci index vector */
	int n_gci_inds = max_gci_ind-min_gci_ind+1;
	gsl::vector_int frame_gci_inds;
	if (n_gci_inds > 0)
		frame_gci_inds = gsl::vector_int(n_gci_inds);


	/* Set frame gcis*/
	size_t i;
	for (i=0;i<(size_t)n_gci_inds;i++) {
		frame_gci_inds(i) = gci_inds(min_gci_ind+i) - minind;
   }


	return frame_gci_inds;
}

void LpWeightAme(const Param &params, const gsl::vector_int &gci_inds,
		 const size_t frame_index, gsl::vector *weight) {

	if (!gci_inds.is_set()) {
		weight->set_all(1.0);
		return;
	}

	gsl::vector_int inds = GetFrameGcis(params, frame_index, gci_inds);

	if(!weight->is_set()) {
		*weight = gsl::vector(params.frame_length + params.lpc_order_vt);
	} else {
		if((int)weight->size() != params.frame_length + params.lpc_order_vt) {
			weight->resize(params.frame_length + params.lpc_order_vt);
		}
	}

   /* If unvoiced or GCIs not found, simply set weight to 1 */
	if(!inds.is_set()) {
		weight->set_all(1.0);
		return;
	}

	/* Algorithm parameters */
	double pq = params.ame_position_quotient;
	double dq = params.ame_duration_quotient;

	double d = 0.001;
	//int nramp = DEFAULT_NRAMP;
	int nramp = 6 * (double)params.fs/(double)16000;

	/* Sanity check */
	if(dq + pq > 1.0)
		dq = 1.0 - pq;

	/* Initialize */
	int i,j,t,t1 = 0,t2 = 0;

	/* Set weight according to GCIs */
	weight->set_all(d); // initialize to small value
	for(i=0;i<(int)inds.size()-1;i++) {
		t = inds(i+1)-inds(i);
		t1 = round(dq*t);
		t2 = round(pq*t);
		while(t1+t2 > t)
			t1 = t1-1;
		for(j=inds(i)+t2;j<inds(i)+t2+t1;j++)
			(*weight)(j) = 1.0;
		if(nramp > 0) {
			for(j=inds(i)+t2;j<inds(i)+t2+nramp;j++)
				(*weight)(j) = (j-inds(i)-t2+1)/(double)(nramp+1); // added double cast: ljuvela
			if(inds(i)+t2+t1-nramp >= 0)
				for(j=inds(i)+t2+t1-nramp;j<inds(i)+t2+t1;j++)
					(*weight)(j) = 1.0-(j-inds(i)-t2-t1+nramp+1)/(double)(nramp+1);
		}
	}
}


void LpWeightSte(const Param &params, const gsl::vector &frame, gsl::vector *weight) {
	if(!weight->is_set()) {
		*weight = gsl::vector(params.frame_length + params.lpc_order_vt);
	} else {
		if((int)weight->size() != params.frame_length + params.lpc_order_vt) {
			weight->resize(params.frame_length + params.lpc_order_vt);
		}
	}
	weight->set_all(0.0);
	int M = params.lpc_order_vt;
	int lag = 1;
   int i,j;
	for(i=0;i<(int)weight->size();i++) {
		for(j=GSL_MAX(i-lag-M+1,0);j<GSL_MIN(i-lag+1,(int)frame.size());j++) {
			(*weight)(i) += frame(j)*frame(j);
		}
		if((*weight)(i) == 0.0)
         (*weight)(i) += DBL_EPSILON; // Ensure non-zero weight
	}
}


void GetLpWeight(const Param &params, const LpWeightingFunction &weight_type,
						const gsl::vector_int &gci_inds, const gsl::vector &frame,
						const size_t &frame_index, gsl::vector *weight_function) {

	switch(weight_type) {
	case NONE:
		weight_function->set_all(1.0);
		break;
	case AME:
		LpWeightAme(params, gci_inds, frame_index, weight_function);
		break;
	case STE:
		LpWeightSte(params, frame, weight_function);
		break;
	}
}



/**
 * Function WWLP
 *
 * Calculate Warped Weighted Linear Prediction (WWLP) coefficients using
 * autocorrelation method.
 *
 * @param weigh_function : reference to the WWLP weighting function
 * @param warping_lambda : warping coefficient
 * @param weight_type : If NONE, compute solution with Levinson (more efficient)
 * @param lp_order : AR prediction order
 * @param frame : reference to the samples
 * @param A : pointer to coefficiets (write to)
 * author: @mairaksi
 */
void WWLP(const gsl::vector &weight_function, const double &warping_lambda, const LpWeightingFunction weight_type,
		const int &lp_order, const gsl::vector &frame, gsl::vector *A) {

   size_t i,j;
   size_t p = (size_t)lp_order;

  // Copy warped frame
   gsl::vector frame_w(frame.size()+p,true);
   for(i=0;i<frame.size();i++)
      frame_w(i) = frame(i);

   gsl::matrix Y(p+1,frame.size()+p,true); // Delayed and weighted versions of the signal

   // Matrix Y
   for(j=0;j<frame.size();j++) { // Set first (unwarped) row
      if(weight_type == 0) {
         Y(0,j) = frame(j);
      } else {
         Y(0,j) = sqrt(weight_function(j))*frame(j);
      }
   }
   for(i=1;i<p+1;i++) { // Set delayed (warped) rows
      AllPassDelay(warping_lambda, &frame_w);
      for(j=0;j<frame_w.size();j++) {
         if(weight_type == 0) {
            Y(i,j) = frame_w(j);
         } else {
            Y(i,j) = sqrt(weight_function(j))*frame_w(j);
         }
      }
   }
   gsl::matrix Rfull = Y*(Y.transpose());

   /** Use generic Ra=b solver if LP weighting is used **/
   if(weight_type != NONE) {
      // Autocorrelation matrix R (R = (YT*Y)/N, size p*p) and vector b (size p)
      double sum = 0.0;
      gsl::matrix R(p,p);
      gsl::matrix b(p,1);
      for(i=0;i<p;i++) {
         for(j=0;j<p;j++) {
            R(i,j) = Rfull(i+1,j+1);
         }
         b(i,0) = Rfull(i+1,0);
         sum += b(i,0);
      }

      //Ra=b solver (LU-decomposition) (Do not evaluate LU if sum = 0)
      gsl::matrix a_tmp(p,1,true);
      if(sum != 0.0)
         a_tmp = R.LU_invert() * b;

      if(!A->is_set()) {
         *A = gsl::vector(p+1);
      } else {
         if(A->size() != p+1) {
            A->resize(p+1);
         } else {
            A->set_all(0.0);
         }
      }

      // Set LP-coefficients to vector "A"
      for(i=1; i<A->size(); i++) {
         (*A)(i) =  (-1.0)*a_tmp(i-1,0);
      }
      (*A)(0) = 1.0;

      // Stabilize polynomial
      StabilizePoly(frame.size(),A);
      for(i=0;i<A->size();i++) {
         if(gsl_isnan((*A)(i))) {
            //std::cout << "Warning" << std::endl;
            (*A)(i) = (0.0);
         }
      }
   /** Use Levinson if no LP weighting **/
   } else {
      Levinson(Rfull.get_col_vec(0), A);
   }
}


/**
 * Function LPC
 *
 * Calculate conventional Linear Prediction (LPC) coefficients using
 * autocorrelation method and Levinson-Durbin recursion.
 *
 * @param frame : reference to the samples
 * @param lpc_order : prediction order
 * @param A : pointer to coefficients
 * author: @mairaksi
 */
void LPC(const gsl::vector &frame, const int &lpc_order, gsl::vector *A) {
	gsl::vector r;
	Autocorrelation(frame,lpc_order,&r);
	Levinson(r,A);
}

void ArAnalysis(const int &lp_order,const double &warping_lambda, const LpWeightingFunction &weight_type,
                  const gsl::vector &lp_weight, const gsl::vector &frame, gsl::vector *A) {

   if(weight_type == NONE && warping_lambda == 0.0) {
      LPC(frame, lp_order, A);
   } /*else if(warping_lambda != 0.0 && weight_type != NONE) {
      int n_tmp = 60;
      gsl::vector A_tmp(n_tmp+1);
      //WWLP(lp_weight, 0.0, weight_type,(A->size()-1)*2,frame, &A_tmp );
      LPC(frame, n_tmp, &A_tmp);
      Lp2Walp(A_tmp,warping_lambda,A);
   }*/ else {
      WWLP(lp_weight, warping_lambda, weight_type, lp_order, frame, A);
   }

   /* Replace NaN-values with zeros in case of all-zero frames */
  // size_t i;
   //for(i=0;i<A->size();i++) {
   //   if(gsl_isnan((*A)(i)))
   //         (*A)(i) = (0.0);
   //}
}

void MeanBasedSignal(const gsl::vector &signal, const int &fs, const double &mean_f0, gsl::vector *mean_based_signal) {
	int N,winlen;
            
	/* Round window length to nearest odd integer */
	winlen = 2*lround((1.75*(double)fs/GSL_MAX(mean_f0,80) + 1)/2)-1;
    N = (winlen-1)/2;

	/* Calculate the mean-based signal */
	//gsl_vector *y = gsl_vector_calloc(signal->size);
	gsl::vector win(2*N+1);
	win.set_all(1.0);
	ApplyWindowingFunction(BLACKMAN,&win);
	int n, m, i;
	double sum;
	for (n=0;n<(int)signal.size();n++){
		sum = 0.0;
		i = 0;
		for(m=-N;m<=N;m++) {
			if((n+m>=0) && (n+m<(int)signal.size())){
				sum += signal(n+m)*win(i);
				i++;
			}
      }
		(*mean_based_signal)(n) = sum/(double)(2*N+1);
	}
}


void SedreamsGciDetection(const gsl::vector &residual, const gsl::vector &mean_based_signal, gsl::vector_int *gci_inds) {
   gsl::vector_int peak_inds;
   gsl::vector peak_values;

   int number_of_peaks = FindPeaks(mean_based_signal, 0.005, &peak_inds, &peak_values); // default
   //int number_of_peaks = FindPeaks(mean_based_signal, 0.001, &peak_inds, &peak_values);

   gsl::vector_int start(number_of_peaks);
   gsl::vector_int stop(number_of_peaks);
	int i,j, ii = 0;
	for(i=0;i<number_of_peaks;i++) {
		if(peak_values(i) < 0) { // only minima
			/* Find next zero-crossing */
			for(j=peak_inds(i);j<(int)mean_based_signal.size()-1;j++) {
				if(GSL_SIGN(mean_based_signal(j)) != GSL_SIGN(mean_based_signal(j+1))) {
					stop(ii) = j;
					start(ii) = peak_inds(i);
					ii++;
					break;
				}
         }
      }
   }
   /* If no GCIs found, set gci_inds as empty and return */
	if (ii == 0){
		std::cerr << "No GCIs found" << std::endl; 
      (*gci_inds) = gsl::vector_int();
		return ;
	}
   (*gci_inds) = gsl::vector_int(ii);

	/* Locate IAIF residual minima at the intervals and set them as GCI*/
	double minval;
	int minind;
	for(i=0;i<ii;i++){
		minval = DBL_MAX;
		minind = 0;
		for(j=start(i);j<stop(i);j++){ // loop through the interval
			if (residual(j) < minval){
				minval = residual(j);
				minind = j;
			}
		}
		(*gci_inds)(i) = minind+1; // Offset by 1 (caused dy differentiation)
	}
}




