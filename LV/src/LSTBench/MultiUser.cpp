#include "MultiUser.hpp"

LHLST::MultiUser::MultiUser(TpcdsPaths paths) : Phase(paths, nullptr, 0, 0) {
  this->con_phases.clear();
}

void LHLST::MultiUser::add_phase(std::unique_ptr<Phase>&& p) {
  this->con_phases.push_back(std::move(p));
}

void LHLST::MultiUser::run(std::vector<double>& times) {
  std::vector<std::vector<double>> all_times;
  std::vector<std::thread> threads;

  for (int i = 0; i < this->con_phases.size(); i++) {
    std::vector<double> tmp = {};
    all_times.push_back(std::move(tmp));

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
