#include "MultiUser.hpp"

LHLST::MultiUser::MultiUser(TpcdsPaths paths) : Phase(paths, nullptr, 0, 0) {
  this->con_phases.clear();
}

void LHLST::MultiUser::add_phase(std::unique_ptr<Phase>&& p) {
  this->con_phases.push_back(std::move(p));
}

void LHLST::MultiUser::run(std::vector<double>& times) {
  // sized before the threads start: they keep references to their element
  std::vector<std::vector<double>> all_times(this->con_phases.size());
  std::vector<std::thread> threads;

  for (size_t i = 0; i < this->con_phases.size(); i++) {
    threads.emplace_back(
        [i, &all_times, this] { this->con_phases[i]->run(all_times[i]); });
    std::cerr << "thread " << i << " started" << std::endl;
  }

  std::cerr << threads.size() << std::endl;
  for (auto& ref : threads) {
    ref.join();
  }

  for (auto& ref : all_times) {
    for (auto& elem : ref) {
      times.push_back(elem);
    }
  }
}
