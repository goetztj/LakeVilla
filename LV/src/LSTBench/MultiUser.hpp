#pragma once
#include "Phase.hpp"

namespace LHLST {

// MultiUser phase of LSTBench
struct MultiUser : Phase {
  std::vector<std::unique_ptr<Phase>> con_phases;

  MultiUser(TpcdsPaths paths);

  void add_phase(std::unique_ptr<Phase>&& p);

  void run(std::vector<double>& times) override;
};
};  // namespace LHLST