#include "gradientboostclassifier.hpp"

using row_d = Row<GradientBoostClassifier::DataType>;
using row_t = Row<GradientBoostClassifier::LabelType>;
using cls = GradientBoostClassifier::ClassifierType;

using namespace PartitionSize;
using namespace LearningRate;

row_d 
GradientBoostClassifier::_constantLeaf() const {
  row_d r;
  r.zeros(dataset_.n_cols);
  return r;
}

row_d
GradientBoostClassifier::_randomLeaf(std::size_t numVals) const {
  
  // Look how clumsy this is
  row_d range = linspace<row_d>(-1, 1, numVals+2);
  std::default_random_engine eng;
  std::uniform_int_distribution<std::size_t> dist{1, numVals};
  row_d r(dataset_.n_cols, arma::fill::none);
  for (size_t i=0; i<dataset_.n_cols; ++i) {
    auto j = dist(eng);    
    r[i] = range[j];
  }  

  return r;
}

void
GradientBoostClassifier::init_() {
  
  std::cout << "INIT_(): PARTTITION SIZE: " << partitionSize_ << std::endl;

  // Note these are flipped
  n_ = dataset_.n_rows; 
  m_ = dataset_.n_cols;

  // Initialize rng  
  std::size_t a=1, b=std::max(1, static_cast<int>(m_ * col_subsample_ratio_));
  partitionDist_ = std::uniform_int_distribution<std::size_t>(a, b);
							      

  // Make labels members of {-1,1}
  assert(!(symmetrized_ && removeRedundantLabels_));

  if (symmetrized_) {
    symmetrizeLabels();
  } else if (removeRedundantLabels_) {
    auto uniqueVals = uniqueCloseAndReplace(labels_);
  }

  // Short-circuit recursive calls; likely only one iteration needed
  if (partitionSize_ == 1) {
    uvec subColMask = linspace<uvec>(0, -1+m_, m_);
    Row<GradientBoostClassifier::DataType> prediction;
    std::unique_ptr<cls> subClassifier;
    subClassifier.reset(new cls(dataset_,
				labels_,
				partitionSize_,
				minLeafSize_,
				minimumGainSplit_,
				maxDepth_));

    subClassifier->Classify_(dataset_, prediction);

    classifiers_.emplace_back(std::move(subClassifier));
    predictions_.emplace_back(std::move(prediction));

    steps_ = 0;


    /*
      std::size_t subPartitionSize = 1;
      uvec subColMask = linspace<uvec>(0, -1+m_, m_);
      Row<GradientBoostClassifier::DataType> prediction;
      std::unique_ptr<cls> subClassifier;
      
      // XXX
      // A little out of order here - we need a good prediction before we calculate coefficients
      std::pair<rowvec, rowvec> coeffs = generate_coefficients(labels_, subColMask);
      Leaves allLeaves = computeOptimalSplit(coeffs.first, coeffs.second, dataset_, 0, subPartitionSize, subColMask);
      
      subClassifier.reset(new cls(dataset_,
      allLeaves,
      1,
      minLeafSize_,
      minimumGainSplit_,
      maxDepth_));
      
      subClassifier->Classify_(dataset_, prediction);
      
      classifiers_.emplace_back(std::move(subClassifier));
      predictions_.emplace_back(std::move(prediction));
      current_classifier_ind_++;
    */

  }

  // partitions
  Partition partition = PartitionUtils::_fullPartition(m_);
  partitions_.push_back(partition);

  // classifiers
  row_d constantLabels = _constantLeaf();
  classifiers_.emplace_back(std::make_unique<GradientBoostClassifier::ClassifierType>(dataset_, labels_, partitionSize_, minLeafSize_, minimumGainSplit_, maxDepth_));
  // classifiers_.emplace_back(std::make_unique<GradientBoostClassifier::ClassifierType>(dataset_, labels_, partitionSize_+1, numTrees_, minLeafSize_));

  // first prediction
  Row<GradientBoostClassifier::DataType> prediction;
  classifiers_[current_classifier_ind_]->Classify_(dataset_, prediction);
  predictions_.emplace_back(prediction);

  uvec rowMask = linspace<uvec>(0, -1+n_, n_);
  uvec colMask = linspace<uvec>(0, -1+m_, m_);
  
  rowMasks_.emplace_back(rowMask);
  colMasks_.emplace_back(colMask);

  
  if (loss_ == lossFunction::BinomialDeviance) {
    lossFn_ = new BinomialDevianceLoss<double>();
  }
  else if (loss_ == lossFunction::MSE) {
    lossFn_ = new MSELoss<double>();
  }

  current_classifier_ind_ = 0;

}

void
GradientBoostClassifier::Predict(Row<GradientBoostClassifier::DataType>& prediction) {
  prediction = zeros<Row<GradientBoostClassifier::DataType>>(m_);
  for (const auto& step : predictions_) {
    prediction += step;
  }
}

void
GradientBoostClassifier::Predict(Row<GradientBoostClassifier::DataType>& prediction, const uvec& colMask) {

  Predict(prediction);
  prediction = prediction.submat(zeros<uvec>(1), colMask);

}

void
GradientBoostClassifier::Predict(const mat& dataset, Row<GradientBoostClassifier::DataType>& prediction) {

  prediction = zeros<Row<GradientBoostClassifier::DataType>>(dataset.n_cols);

  for (const auto& classifier : classifiers_) {
    Row<GradientBoostClassifier::DataType> predictionStep;
    classifier->Classify_(dataset, predictionStep);
    prediction += predictionStep;    
  }  

  if (symmetrized_) {
    deSymmetrize(prediction);
  }
}

void
GradientBoostClassifier::Predict(Row<GradientBoostClassifier::LabelType>& prediction) {
  row_d prediction_d = conv_to<row_d>::from(prediction);
  Predict(prediction_d);
  prediction = conv_to<row_t>::from(prediction_d);
}

void
GradientBoostClassifier::Predict(Row<GradientBoostClassifier::LabelType>& prediction, const uvec& colMask) {
  row_d prediction_d = conv_to<row_d>::from(prediction);
  Predict(prediction_d, colMask);
  prediction = conv_to<row_t>::from(prediction_d);
}

void
GradientBoostClassifier::Predict(const mat& dataset, Row<GradientBoostClassifier::LabelType>& prediction) {
  row_d prediction_d;
  Predict(dataset, prediction_d);

  if (symmetrized_) {
    deSymmetrize(prediction_d);
  }

  prediction = conv_to<row_t>::from(prediction_d);

}

uvec
GradientBoostClassifier::subsampleRows(size_t numRows) {
  uvec r = sort(randperm(n_, numRows));
  return r;
}

uvec
GradientBoostClassifier::subsampleCols(size_t numCols) {
  uvec r = sort(randperm(m_, numCols));
  return r;
}

Row<GradientBoostClassifier::DataType>
GradientBoostClassifier::uniqueCloseAndReplace(Row<GradientBoostClassifier::DataType>& labels) {
  
  Row<GradientBoostClassifier::DataType> uniqueVals = unique(labels);
  double eps = static_cast<double>(std::numeric_limits<float>::epsilon());
  
  std::vector<std::pair<GradientBoostClassifier::DataType, GradientBoostClassifier::DataType>> uniqueByEps;
  std::vector<GradientBoostClassifier::DataType> uniqueVals_;
  
  uniqueVals_.push_back(uniqueVals[0]);
  
  for (int i=1; i<uniqueVals.n_cols; ++i) {
    bool found = false;
    for (const auto& el : uniqueVals_) {
      if (fabs(uniqueVals[i] - el) <= eps) {
	  found = true;
	  uniqueByEps.push_back(std::make_pair(uniqueVals[i], el));
      }
    }
    if (!found) {
      uniqueVals_.push_back(uniqueVals[i]);
    }      
  }
  
  // Replace redundant values in labels_
  for(const auto& el : uniqueByEps) {
    uvec ind = find(labels_ == el.first);
    labels.elem(ind).fill(el.second);
  }

  // Now uniqueVals_ matches labels_ characteristics
  return uniqueVals_;
}

void
GradientBoostClassifier::symmetrizeLabels() {
  Row<GradientBoostClassifier::DataType> uniqueVals = uniqueCloseAndReplace(labels_);

  if (uniqueVals.n_cols == 1) {
    // a_ = fabs(1./uniqueVals(0)); b_ = 0.;
    // labels_ = sign(labels_);
    a_ = 1.; b_ = 1.;
    labels_ = ones<Row<double>>(labels_.n_elem);
  } else if (uniqueVals.size() == 2) {
    double m = *std::min_element(uniqueVals.cbegin(), uniqueVals.cend());
    double M = *std::max_element(uniqueVals.cbegin(), uniqueVals.cend());
    a_ = 2./static_cast<double>(M-m);
    b_ = static_cast<double>(m+M)/static_cast<double>(m-M);
    labels_ = sign(a_*labels_ + b_);
      // labels_ = sign(2 * labels_ - 1);      
  } else {
    assert(uniqueVals.size() == 2);
  }
    
}


void
GradientBoostClassifier::symmetrize(Row<GradientBoostClassifier::DataType>& prediction) {
  prediction = sign(a_*prediction + b_);
  // prediction = sign(2 * prediction - 1);
}


void
GradientBoostClassifier::deSymmetrize(Row<GradientBoostClassifier::DataType>& prediction) {
  prediction = (sign(prediction) - b_)/ a_;
  // prediction = (1 + sign(prediction)) / 2.;
}


void
GradientBoostClassifier::fit_step(std::size_t stepNum) {
  int rowRatio = static_cast<size_t>(n_ * row_subsample_ratio_);
  int colRatio = static_cast<size_t>(m_ * col_subsample_ratio_);
  uvec rowMask = subsampleRows(rowRatio);
  uvec colMask = subsampleCols(colRatio);

  rowMasks_.push_back(rowMask);
  colMasks_.push_back(colMask);

  mat dataset_slice = dataset_.submat(rowMask, colMask);  
  row_d labels_slice = labels_.submat(zeros<uvec>(1), colMask);

  // Generate coefficients g, h
  std::pair<rowvec, rowvec> coeffs = generate_coefficients(labels_slice, colMask);

  // Compute partition size
  std::size_t partitionSize = computePartitionSize(stepNum, colMask);

  // Compute learning rate
  double learningRate = computeLearningRate(stepNum);

  // Find classifier fit for leaves choice
  Leaves allLeaves = zeros<row_d>(m_);

  Row<GradientBoostClassifier::DataType> prediction;
  std::unique_ptr<cls> classifier;

  Row<GradientBoostClassifier::DataType> prediction_slice;

  if (recursiveFit_) {

    // Reduce partition size
    std::size_t subPartitionSize = static_cast<std::size_t>(partitionSize/2);

    // When considering subproblems, colMask is full
    uvec subColMask = linspace<uvec>(0, -1+m_, m_);

    // Regenerate coefficients with full colMask
    coeffs = generate_coefficients(labels_, subColMask);
    
    // Compute optimal leaf choice on unrestricted dataset
    Leaves best_leaves = computeOptimalSplit(coeffs.first, coeffs.second, dataset_, stepNum, subPartitionSize, subColMask);
    
    // Fit classifier on {dataset, padded best_leaves}
    // Zero pad labels first
    allLeaves = best_leaves;

    ClassifierContext::Context context{};

    context.loss = loss_;
    context.partitionSize = subPartitionSize;
    context.partitionRatio = partitionRatio_;
    context.learningRate = learningRate_;
    context.steps = 1000;
    context.symmetrizeLabels = true;
    context.rowSubsampleRatio = row_subsample_ratio_;
    context.colSubsampleRatio = col_subsample_ratio_;
    context.recursiveFit = true;
    context.partitionSizeMethod = partitionSizeMethod_;
    context.learningRateMethod = learningRateMethod_;    
    context.minLeafSize = minLeafSize_;
    context.maxDepth = maxDepth_;
    context.minimumGainSplit = minimumGainSplit_;
    
    // allLeaves may not strictly fit the definition of labels here - 
    // aside from the fact that it is of double type, it may have more 
    // than one class. So we don't want to symmetrize, but we want 
    // to remap the redundant values.
    auto subClassifier = GradientBoostClassifier(dataset_, 
						 allLeaves,
						 context);
    
    subClassifier.fit();

    std::cout << "FIT\n";

    // Need to persist:
    // classifiers_.emplace_back(std::move(classifier));
    // predictions_.emplace_back(prediction);
    // current_classifier_ind_++;
    
    // subClassifier->Classify_(dataset_, prediction);

  } else {
  
    // Compute optimal leaf choice on unrestricted dataset
    Leaves best_leaves = computeOptimalSplit(coeffs.first, coeffs.second, dataset_, stepNum, partitionSize, colMask);
    
    // Fit classifier on {dataset, padded best_leaves}
    // Zero pad labels first
    allLeaves(colMask) = best_leaves;
    
    classifier.reset(new cls(dataset_, 
			     allLeaves, 
			     std::move(partitionSize+1), // Since 0 is an additional class value
			     std::move(minLeafSize_),
			     std::move(minimumGainSplit_),
			     std::move(maxDepth_)));
    classifier->Classify_(dataset_, prediction);

  }
  
  classifiers_.emplace_back(std::move(classifier));
  predictions_.emplace_back(prediction);
  current_classifier_ind_++;
  
  // colMask.print(std::cout);
  // dataset_slice.print(std::cout);
  
  /*
    std::cout << "EDICTIONS\n";
    for (size_t i=500; i<600; ++i) {
    std::cout << best_leaves(i) << " : " << prediction_slice(i) << std::endl;
    }
  */

}


typename GradientBoostClassifier::Leaves
GradientBoostClassifier::computeOptimalSplit(rowvec& g,
					     rowvec& h,
					     mat dataset,
					     std::size_t stepNum, 
					     std::size_t partitionSize,
					     const uvec& colMask) {

  // We should implement several methods here
  // XXX
  std::vector<double> gv = arma::conv_to<std::vector<double>>::from(g);
  std::vector<double> hv = arma::conv_to<std::vector<double>>::from(h);

  int n = colMask.n_rows, T = partitionSize;
  bool risk_partitioning_objective = true;
  bool use_rational_optimization = true;
  bool sweep_down = false;
  double gamma = 0.;
  double reg_power=1.;
  bool find_optimal_t = false;

  // std::cout << "PARTITION SIZE: " << T << std::endl;

  auto dp = DPSolver(n, T, gv, hv,
		     objective_fn::RationalScore,
		     risk_partitioning_objective,
		     use_rational_optimization,
		     gamma,
		     reg_power,
		     sweep_down,
		     find_optimal_t
		     );
  
  auto subsets = dp.get_optimal_subsets_extern();
  
  // Use subset of subsets
  // =====================
  bool USE_FIRST_SUBSET = false;
  size_t NUM_SUBSETS = 3;
  std::vector<std::vector<int>> v;
  if (USE_FIRST_SUBSET) {
    if (g.n_elem > 1) {
      if (stepNum%2) {
	for (size_t i=0; i<NUM_SUBSETS; ++i)
	  v.push_back(subsets[i]);
      } else {
      for (size_t i=partitionSize-1; i>(partitionSize-1-NUM_SUBSETS); --i) 
	v.push_back(subsets[i]);
      }
      subsets = v;
    }
  }
  // =====================

  rowvec leaf_values = arma::zeros<rowvec>(n);
  for (const auto& subset : subsets) {
    uvec ind = arma::conv_to<uvec>::from(subset);
    double val = -1. * learningRate_ * sum(g(ind))/sum(h(ind));
    for (auto i: ind) {
       leaf_values(i) = val;
    }
  }

  partitions_.emplace_back(subsets);

  return leaf_values;
    
}


void
GradientBoostClassifier::fit() {

  for (std::size_t stepNum=1; stepNum<=steps_; ++stepNum) {
    fit_step(stepNum);
    

    if ((stepNum%100) == 0) {
      Row<GradientBoostClassifier::DataType> yhat, yhat_sym;
      Predict(yhat);
      double r = lossFn_->loss(yhat, labels_);
      deSymmetrize(yhat); symmetrize(yhat);

      double error_is = accu(yhat != labels_) * 100. / labels_.n_elem;
      
      std::cout << "STEP: " << stepNum 
		<< " IS LOSS: " << r
		<< " IS ERROR: " << error_is << "%" << std::endl;
      
      if (hasOOSData_) {
	Row<GradientBoostClassifier::DataType> yhat_oos;
	Predict(dataset_oos_, yhat_oos);
	deSymmetrize(yhat_oos); symmetrize(yhat_oos);
	double error_oos = accu(yhat_oos != labels_oos_) * 100. / labels_oos_.n_elem;
	std::cout << "OOS ERROR: " << error_oos << "%" << std::endl;
      }
    }
  }

}


void
GradientBoostClassifier::Classify(const mat& dataset, Row<GradientBoostClassifier::DataType>& labels) {
  Predict(dataset, labels);
}


double
GradientBoostClassifier::computeLearningRate(std::size_t stepNum) {

  double learningRate;

  if (learningRateMethod_ == RateMethod::FIXED) {
    learningRate = learningRate_;
  } else if (learningRateMethod_ == RateMethod::DECREASING) {
    double A = learningRate_, B = -log(.5) / static_cast<double>(steps_);
    learningRate = A * exp(-B * (-1 + stepNum));
  } else if (learningRateMethod_ == RateMethod::INCREASING) {
    double A = learningRate_, B = log(2.) / static_cast<double>(steps_);
    learningRate = A * exp(B * (-1 + stepNum));
  }

  if ((stepNum%100)==0)
    std::cout << "stepNum: " << stepNum << "LEARNING RATE: " << learningRate << std::endl;

  return learningRate;
}

std::size_t
GradientBoostClassifier::computePartitionSize(std::size_t stepNum, const uvec& colMask) {

  // stepNum is in range [1,...,context.steps]

  std::size_t partitionSize;
  double lowRatio = .05;
  double highRatio = .95;
  int attach = 1000;

  if (partitionSizeMethod_ == SizeMethod::FIXED) {
    return partitionSize_;
  } else if (partitionSizeMethod_ == SizeMethod::FIXED_PROPORTION) {
    partitionSize = static_cast<std::size_t>(partitionRatio_ * row_subsample_ratio_ * colMask.n_rows);
  } else if (partitionSizeMethod_ == SizeMethod::DECREASING) {
    double A = colMask.n_rows, B = log(colMask.n_rows)/steps_;
    partitionSize = std::max(1, static_cast<int>(A * exp(-B * (-1 + stepNum))));
  } else if (partitionSizeMethod_ == SizeMethod::INCREASING) {
    double A = 2., B = log(colMask.n_rows)/static_cast<double>(steps_);
    partitionSize = std::max(1, static_cast<int>(A * exp(B * (-1 + stepNum))));
  } else if (partitionSizeMethod_ == SizeMethod::RANDOM) {
    partitionSize = partitionDist_(default_engine_);
    ;
  } else if (partitionSizeMethod_ == SizeMethod::MULTISCALE) {
    if ((stepNum%attach) < (attach/2)) {
      partitionSize = static_cast<std::size_t>(lowRatio * col_subsample_ratio_ * colMask.n_rows);
      partitionSize = partitionSize >= 1 ? partitionSize : 1;
    } else {
      partitionSize = static_cast<std::size_t>(highRatio * col_subsample_ratio_ * colMask.n_rows);
      partitionSize = partitionSize >= 1 ? partitionSize : 1;
    }
  }
  
  if ((stepNum%100)==0)
    std::cout << "stepNum: " << stepNum << " PARTITIONSIZE: " << partitionSize << std::endl;

  return partitionSize;
}


std::pair<rowvec, rowvec>
GradientBoostClassifier::generate_coefficients(const Row<GradientBoostClassifier::DataType>& labels, const uvec& colMask) {

  rowvec yhat;
  Predict(yhat, colMask);

  rowvec g, h;
  lossFn_->loss(yhat, labels, &g, &h);

  return std::make_pair(g, h);
  
  /* 
  std::cout << "GENERATE COEFFICIENTS\n";
  std::cout << "labels size: " << labels.n_rows << " x " << labels.n_cols << std::endl;
  labels.print(std::cout);
  std::cout << "yhat_slice size: " << yhat_slice.n_rows << " x " << yhat_slice.n_cols << std::endl;  
  yhat_slice.print(std::cout);
  std::cout << "g size: " << g.n_rows << " x " << g.n_cols << std::endl;
  g.print(std::cout);
  std::cout << "h size: " << h.n_rows << " x " << h.n_cols << std::endl;
  h.print(std::cout);
  */

}

std::pair<rowvec, rowvec>
GradientBoostClassifier::generate_coefficients(const Row<GradientBoostClassifier::DataType>& yhat,
					       const Row<GradientBoostClassifier::DataType>& y,
					       const uvec& colMask) {
  rowvec g, h;
  lossFn_->loss(yhat, y, &g, &h);

  return std::make_pair(g, h);
}
/*
  double
  GradientBoostClassifier::imbalance() {
  ;
  }
  // 2.0*((sum(y_train==0)/len(y_train) - .5)**2 + (sum(y_train==1)/len(y_train) - .5)**2)
  */

