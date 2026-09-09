#ifndef MCEL_REPORT_H
#define MCEL_REPORT_H
#include <string>
#include <vector>
#include "mcel.h"
namespace mcel {
/* Histogram: write_hist[k] = ile pikseli dostało dokładnie k zapisów;
 * ostatni kubełek zbiera "k i więcej". */
std::string stats_json(const Mcel &m, const std::vector<uint32_t> &write_hist);
}
#endif
