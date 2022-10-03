#ifndef __LOSS_HPP__
#define __LOSS_HPP__

#include <functional>
#include <exception>
#include <mlpack/core.hpp>
#include <autodiff/forward/real.hpp>
#include <autodiff/forward/real/eigen.hpp>

using namespace arma;
using namespace autodiff;
using namespace std::placeholders;

#define UNUSED(expr) do { (void)(expr); } while (0)
namespace LossMeasures { 
  enum class lossFunction {  MSE = 0,
			     BinomialDeviance = 1,
			     };

  struct lossFunctionException : public std::exception {
    const char* what() const throw() {
      return "General LossFunction exception";
    };
  };


Eigen::VectorXd static_cast_eigen(rowvec& rhs) {
  Eigen::VectorXd lhs = Eigen::Map<Eigen::VectorXd>(rhs.memptr(), rhs.n_cols);
  return lhs;
}

rowvec static_cast_arma(Eigen::VectorXd& rhs) {
  rowvec lhs = rowvec(rhs.data(), rhs.size(), false, false);
  return lhs;		      
}

template<typename DataType>
class LossFunction {
public:
  DataType loss(rowvec&, rowvec*);
private:
  virtual autodiff::real loss_reverse(const ArrayXreal&, const ArrayXreal&) = 0;
};

template<typename DataType>
class BinomialDevianceLoss : public LossFunction<DataType> {
public:
  BinomialDevianceLoss() = default;
private:
  autodiff::real loss_reverse(const ArrayXreal&, const ArrayXreal&) override;  
};

template<typename DataType>
class MSELoss : public LossFunction<DataType> {
public:
  MSELoss() = default;
private:
  autodiff::real loss_reverse(const ArrayXreal&, const ArrayXreal&) override;
};

}
#include "loss_impl.hpp"

#endif