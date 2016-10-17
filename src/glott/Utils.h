#ifndef UTILS_H_
#define UTILS_H_
#include <vector>

gsl::vector StdVector2GslVector(const std::vector<double> &stdvec);

/* Debug functions */
void VPrint1(const gsl::vector &vector);
void VPrint2(const gsl::vector &vector);
void VPrint3(const gsl::vector &vector);
void VPrint4(const gsl::vector &vector);
void VPrint5(const gsl::vector &vector);

#endif /* UTILS_H_ */