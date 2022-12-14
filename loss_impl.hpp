#ifndef __LOSS_IMPL_HPP__
#define __LOSS_IMPL_HPP__

using namespace LossMeasures;

const bool AUTODIFF_ON = false;

template<typename DataType>
DataType
LossFunction<DataType>::loss(const rowvec& yhat, const rowvec& y, rowvec* grad, rowvec* hess) {
  if (AUTODIFF_ON) { 
    // Slooooooowwww...
    // Since we only need gradient, hessian of quadratic approximation to loss and not
    // some deep neural network composition, we can usually compute and 
    // hard-code these for a given loss function, the approach we take here.
    autodiff::real u;
    ArrayXreal yhatr = LossUtils::static_cast_eigen(yhat).eval();
    ArrayXreal yr = LossUtils::static_cast_eigen(y).eval();
    
    Eigen::VectorXd grad_tmp;
    // std::function<autodiff::real(const ArrayXreal&)> loss_ = std::bind(&BinomialDevianceLoss<DataType>::loss_reverse, this, yr, _1);
    std::function<autodiff::real(const ArrayXreal&)> loss_ = std::bind(&LossFunction<DataType>::loss_reverse, this, yr, _1);
    grad_tmp = gradient(loss_, wrt(yhatr), at(yhatr), u);
    *grad = LossUtils::static_cast_arma(grad_tmp);
    return static_cast<DataType>(u.val());
  } else {
    DataType r = gradient_(yhat, y, grad);
    hessian_(yhat, y, hess);
    return r;
  }
}

template<typename DataType>
void
BinomialDevianceLoss<DataType>::hessian_(const rowvec& yhat, const rowvec& y, rowvec* hess) {
  rowvec f = exp(y % yhat);
  *hess = (pow(y, 2) % f)/pow(1 + f, 2);
}

template<typename DataType>
DataType
BinomialDevianceLoss<DataType>::gradient_(const rowvec& yhat, const rowvec& y, rowvec* grad) {
  rowvec f = exp(-y % yhat);
  *grad = (-y % f)/(1 + f);
  
  ArrayXreal yhatr = LossUtils::static_cast_eigen(yhat).eval();
  ArrayXreal yr = LossUtils::static_cast_eigen(y).eval();

  return static_cast<DataType>(loss_reverse(yr, yhatr).val());
}

template<typename DataType>
DataType
BinomialDevianceLoss<DataType>::loss_reverse_arma(const rowvec& yhat, const rowvec& y) {
  return sum(log(1 + exp(-y % yhat)));
}

template<typename DataType>
void
MSELoss<DataType>::hessian_(const rowvec& yhat, const rowvec& y, rowvec* hess) {
  rowvec f(y.n_cols, arma::fill::ones);
  *hess = 2 * f;
}

template<typename DataType>
DataType
MSELoss<DataType>::gradient_(const rowvec& yhat, const rowvec& y, rowvec* grad) {
  *grad = -2 * (y - yhat);

  ArrayXreal yhatr = LossUtils::static_cast_eigen(yhat).eval();
  ArrayXreal yr = LossUtils::static_cast_eigen(y).eval();

  return static_cast<DataType>(loss_reverse(yr, yhatr).val());
}

template<typename DataType>
autodiff::real
BinomialDevianceLoss<DataType>::loss_reverse(const ArrayXreal& y, const ArrayXreal& yhat) {
  return ((1 + (-y * yhat).exp()).log()).sum();
}
template<typename DataType>
autodiff::real
MSELoss<DataType>::loss_reverse(const ArrayXreal& y, const ArrayXreal& yhat) {
  return pow((y - yhat), 2).sum();
}

template<typename DataType>
DataType
MSELoss<DataType>::loss_reverse_arma(const rowvec& yhat, const rowvec& y) {
  return sum(pow((y - yhat), 2));
}


#endif
